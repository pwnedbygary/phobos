//The stubs and odds and ends of docs/psp-core.md's part 20: sceMpeg setting a movie up (mpeg.cpp; since part 25 a
//movie fed and taken apart into its access units), the network libraries with the wireless LAN off (net.cpp), and
//the small functions games asked for: the local time, the OpenPSID, the display's line count and rate, the CPU's
//interrupts and the kernel's memset and memcpy, later SDKs' clock setter, the AV modules, the thread priority
//functions, holding off dispatch (waits refused before they change anything), and the lightweight mutex's CB lock
//(and its mutex deleted by its callback). Each group's machine, saved at its end, loads into another that makes the
//same state. Programs run on both engines. sceAtrac3plus has atrac.cpp.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results, Buffer = 0x0893'0000;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//A movie as a PSMF file holds it, made up for the tests: a header packet (not a pack: "PSMF0015" and zeros), then
//2048-byte packs, each a pack header, a PES packet of video (stream 0xe0) and a padding packet filling the rest (the
//first pack a system header and a private stream 2 packet too, before its video). The video is access units of the
//sizes given, each an access unit delimiter and a filler NAL unit; one with a time stamp starts a pack, whose PES
//packet carries its presentation time and a decoding time 3003 before.
struct Movie {
  static constexpr u64 None = ~0ull;
  std::vector<u8> bytes;
  std::vector<u32> starts;     //where each access unit begins in the video, then where the video ends
  std::vector<u32> videoEnds;  //where each packet's video ends in the video (the header packet's: 0)
  std::vector<u64> stamps;     //each access unit's presentation time, or None

  Movie(const std::vector<u32>& sizes, const std::vector<u64>& stamps) : stamps(stamps) {
    std::vector<u8> video;
    for(u32 size : sizes) {
      starts.push_back(video.size());
      for(u8 byte : {0, 0, 0, 1, 0x09, 0xf0, 0, 0, 0, 1, 0x0c}) video.push_back(byte);
      video.resize(starts.back() + size, 0xff);
    }
    starts.push_back(video.size());
    bytes.assign(2048, 0);
    memcpy(bytes.data(), "PSMF0015", 8);
    videoEnds.push_back(0);
    for(u32 n = 0; n < sizes.size();) {
      u32 last = n + 1;
      while(last < sizes.size() && stamps[last] == None) last++;
      for(u32 from = starts[n], to = starts[last], first = 1; from < to; first = 0) {
        std::vector<u8> pack = {0, 0, 1, 0xba, 0x44, 0, 4, 0, 4, 1, 1, 0x89, 0xc3, 0xf8};
        if(bytes.size() == 2048) {
          for(u8 byte : {0, 0, 1, 0xbb, 0, 6, 0x80, 0, 1, 0, 0x21, 0xff}) pack.push_back(byte);
          for(u8 byte : {0, 0, 1, 0xbf, 0, 4, 1, 0xe0, 0, 0}) pack.push_back(byte);
        }
        u32 stampBytes = first && stamps[n] != None ? 10 : 0;
        u32 length = std::min<u32>(2048 - pack.size() - 9 - stampBytes - 6, to - from);
        u32 pes = 3 + stampBytes + length;
        for(u32 byte : {0u, 0u, 1u, 0xe0u, pes >> 8, pes & 0xff, 0x81u, stampBytes ? 0xc0u : 0u, stampBytes}) {
          pack.push_back(byte);
        }
        if(stampBytes) stamp(pack, 3, stamps[n]), stamp(pack, 1, stamps[n] - 3003);
        pack.insert(pack.end(), video.begin() + from, video.begin() + from + length);
        from += length;
        u32 padding = 2048 - pack.size() - 6;
        for(u32 byte : {0u, 0u, 1u, 0xbeu, padding >> 8, padding & 0xff}) pack.push_back(byte);
        pack.resize(2048, 0xff);
        bytes.insert(bytes.end(), pack.begin(), pack.end());
        videoEnds.push_back(from);
      }
      n = last;
    }
  }

  //A time stamp as a PES header holds it: 33 bits in five bytes, behind a prefix, with marker bits between.
  static auto stamp(std::vector<u8>& out, u32 prefix, u64 time) -> void {
    for(u64 byte : {prefix << 4 | (time >> 29 & 0x0e) | 1, time >> 22 & 0xff, (time >> 14 & 0xfe) | 1,
                    time >> 7 & 0xff, (time << 1 & 0xfe) | 1}) {
      out.push_back(byte);
    }
  }
  auto packets() const -> u32 { return bytes.size() / 2048; }
};

//Where the movie tests keep things: the movie itself, the ringbuffer, its packets, the library's memory, its
//handle, an access unit, the callback's place in the movie and its log (how many calls, then where each was asked
//to put its packets, from the ring's start, and how many), and the results.
constexpr u32 MovieAt = 0x0900'0000, Ring = R + 0x100, RingData = 0x0940'0000, Library = 0x0960'0000;
constexpr u32 Handle = R + 0x40, Au = R + 0x60, Attribute = R + 0x80, Got = R + 0x84, Place = R + 0x88;
constexpr u32 Log = R + 0x1000, Results = R + 0x4000, CallbackCode = 0x0880'3000, NoData = 0x8061'8001;

//The movie in memory, a ringbuffer of so many packets whose callback (at CallbackCode) copies the movie's packets
//into it from memory, and the library reading from it.
auto mpegSetUp(KernelMachine& m, const Movie& movie, u32 packets) -> void {
  m.system.memory.copyIn(MovieAt, movie.bytes.data(), movie.bytes.size());
  m.system.memory.write(4, Place, 0);
  m.system.memory.write(4, Log, 0);
  m.call("sceMpegInit", {});
  m.call("sceMpegRingbufferConstruct", {Ring, packets, RingData, packets * 0x868, CallbackCode, Place});
  m.call("sceMpegCreate", {Handle, Library, 0x10000, Ring, 512, 0, 0});
}

//The ringbuffer's callback (where, packets, its argument: the place in the movie): it logs the call, then copies as
//many of the movie's packets as are asked for and left (from its place, which moves on), and returns how many. With
//delay, it waits 500 microseconds first, as a read would; with looping, its place goes back to the movie's start at
//its end (as Space Invaders Extreme's does).
auto mpegCallback(KernelMachine& m, const Movie& movie, bool delay, bool looping) -> void {
  Assembler c{m, CallbackCode};
  if(delay) {
    c.put(addiu(sp, sp, -32)); c.put(sw(ra, 28, sp)); c.put(sw(a0, 24, sp)); c.put(sw(a1, 20, sp));
    c.put(sw(a2, 16, sp));
    c.li(a0, 500);
    c.call("sceKernelDelayThread");
    c.put(lw(a2, 16, sp)); c.put(lw(a1, 20, sp)); c.put(lw(a0, 24, sp)); c.put(lw(ra, 28, sp));
    c.put(addiu(sp, sp, 32));
  }
  c.li(t6, Log); c.put(lw(t7, 0, t6)); c.put(sll(t8, t7, 3)); c.put(addu(t8, t8, t6));
  c.li(t9, RingData); c.put(subu(t9, a0, t9)); c.put(sw(t9, 4, t8)); c.put(sw(a1, 8, t8));
  c.put(addiu(t7, t7, 1)); c.put(sw(t7, 0, t6));
  c.put(lw(t1, 0, a2));  //the place
  c.put(sll(t2, a1, 11));
  c.li(t3, movie.bytes.size());
  c.put(subu(t3, t3, t1));
  c.put(min(t2, t2, t3));
  c.li(t5, MovieAt); c.put(addu(t5, t5, t1));
  c.put(addu(t1, t1, t2));
  if(looping) {
    c.li(t4, movie.bytes.size());
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

//What a frame of the movie test gave: the free packets before and after feeding, what was put in, sceMpegGetAvcAu's
//result and the access unit then (its time stamps and size), and whether decoding it gave a picture.
struct MovieFrame {
  u32 before, put, after, result, presentedHigh, presented, decodedHigh, decoded, size, picture;
  auto operator==(const MovieFrame&) const -> bool = default;
};

//The program of the movie test: so many frames, each feeding the ringbuffer (asking for so many packets), asking for
//an access unit and decoding it, as video/mpeg/basic did; then it exits.
auto movieProgram(KernelMachine& m, u32 code, u32 frames, u32 ask) -> void {
  Assembler a{m, code};
  a.li(a0, Handle); a.li(a1, 1); a.li(a2, Au); a.call("sceMpegInitAu");
  a.li(t0, Got); a.li(t1, 7); a.put(sw(t1, 0, t0));
  a.li(s0, 0); a.li(s1, Results);
  u32 loop = a.here();
  a.li(a0, Ring); a.call("sceMpegRingbufferAvailableSize"); a.put(sw(v0, 0, s1)); a.put(addu(s2, v0, zero));
  a.li(a0, Ring); a.li(a1, ask); a.put(addu(a2, s2, zero)); a.call("sceMpegRingbufferPut"); a.put(sw(v0, 4, s1));
  a.li(a0, Ring); a.call("sceMpegRingbufferAvailableSize"); a.put(sw(v0, 8, s1));
  a.li(a0, Handle); a.li(a1, 0x12c0); a.li(a2, Au); a.li(a3, Attribute); a.call("sceMpegGetAvcAu");
  a.put(sw(v0, 12, s1));
  a.li(t0, Au);
  for(u32 n : {0u, 1u, 2u, 3u, 5u}) a.put(lw(t1, n * 4, t0)), a.put(sw(t1, 16 + (n == 5 ? 4 : n) * 4, s1));
  a.li(a0, Handle); a.li(a1, Au); a.li(a2, 512); a.li(a3, Buffer); a.li(t0, Got); a.call("sceMpegAvcDecode");
  a.li(t0, Got); a.put(lw(t1, 0, t0)); a.put(sw(t1, 36, s1));
  a.put(addiu(s1, s1, 40)); a.put(addiu(s0, s0, 1)); a.li(t0, frames);
  u32 at = a.here();
  a.put(bne(s0, t0, s32(loop - at - 4) / 4)); a.put(nop);
  a.call("sceKernelExitGame");
}

auto movieFrames(KernelMachine& m, u32 frames) -> std::vector<MovieFrame> {
  std::vector<MovieFrame> out(frames);
  for(u32 n = 0; n < frames; n++) {
    u32 w[10];
    for(u32 i = 0; i < 10; i++) w[i] = word(m, Results + n * 40 + i * 4);
    out[n] = {w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9]};
  }
  return out;
}

//What video/mpeg/basic's recording says the movie test should give, worked out from how the movie was made: feeding
//asks the callback for runs that stop at the ring's end, again after a short one, until a callback gives none; an
//access unit comes once the next one's delimiter is in (or at the file's end, after a feeding came up short, with
//its data); its time stamps if a PES packet begins with it; the packets whose video it took the last of free again;
//a picture from the second decoded on. The callback's log comes out too.
auto movieModel(const Movie& movie, u32 ring, u32 frames, u32 ask, std::vector<std::pair<u32, u32>>& log)
  -> std::vector<MovieFrame> {
  std::vector<MovieFrame> out;
  u32 written = 0, freed = 0, writeAt = 0, next = 0, taken = 0;
  bool ended = false, holding = false;
  u32 units = movie.starts.size() - 1;
  MovieFrame au{};
  for(u32 frame = 0; frame < frames; frame++) {
    MovieFrame f{};
    f.before = ring - (written - freed);
    u32 wanted = std::min(ask, f.before), left = wanted;
    while(left) {
      u32 run = std::min(left, ring - writeAt);
      log.push_back({writeAt * 2048, run});
      u32 given = std::min(run, movie.packets() - written);
      if(!given) break;
      writeAt = (writeAt + given) % ring, written += given, left -= given, f.put += given;
    }
    if(wanted) ended = left > 0;
    f.after = ring - (written - freed);
    u32 videoIn = written ? movie.videoEnds[written - 1] : 0;
    bool ready = next < units && (next + 1 < units ? movie.starts[next + 1] + 5 <= videoIn
                                                    : ended && written == movie.packets());
    if(ready) {
      f.result = 0;
      u64 time = Movie::None, decode = Movie::None;
      for(u32 p = 1; p < movie.videoEnds.size(); p++) {
        if(movie.videoEnds[p - 1] == movie.starts[next] && movie.stamps[next] != Movie::None) {
          time = movie.stamps[next], decode = time - 3003;
        }
      }
      au.presentedHigh = time >> 32, au.presented = time, au.decodedHigh = decode >> 32, au.decoded = decode;
      au.size = movie.starts[next + 1] - movie.starts[next];
      taken = movie.starts[next + 1];
      next++;
      while(freed < written && movie.videoEnds[freed] <= taken) freed++;
    } else {
      f.result = NoData;
    }
    f.presentedHigh = au.presentedHigh, f.presented = au.presented, f.decodedHigh = au.decodedHigh;
    f.decoded = au.decoded, f.size = au.size;
    f.picture = 0;
    if(au.size) f.picture = holding, holding = true, au.size = 0;
    out.push_back(f);
  }
  return out;
}

//Where the feeding tests keep their callback's answers, three words a call (what to add to what it was asked for,
//what to return instead if that isn't 0, and how long to wait first: microseconds, or Sleep to sleep), and its log
//(the count, then three words a call: where it was asked to put its packets, from the ring's start, how many, and
//its global pointer); and where the random packs test keeps its steps and what each of its calls gave.
constexpr u32 Answers = 0x0897'0000, FeedLog = 0x0898'0000, Steps = 0x0899'0000, Records = 0x089a'0000;
constexpr u32 Sleep = ~0u;

//A ringbuffer of so many packets fed by feedingCallback(), the library reading from it, and the callback's answers,
//call by call (past those given: what it was asked for, at once). Its argument points at its place in a pool.
auto feedingSetUp(KernelMachine& m, u32 packets, const std::vector<std::array<u32, 3>>& answers) -> void {
  for(u32 n = 0; n < answers.size(); n++) {
    for(u32 i = 0; i < 3; i++) m.system.memory.write(4, Answers + n * 12 + i * 4, answers[n][i]);
  }
  m.call("sceMpegInit", {});
  m.call("sceMpegRingbufferConstruct", {Ring, packets, RingData, packets * 0x868, CallbackCode, Place});
  m.call("sceMpegCreate", {Handle, Library, 0x10000, Ring, 512, 0, 0});
}

//The feeding tests' ringbuffer callback (where, packets, its argument), at CallbackCode: it logs the call, waits as
//its answer says (sceKernelDelayThread, or sceKernelSleepThread), and returns what it was asked for plus the
//answer's first word, or the second if that isn't 0. Given a pool, it first copies as many of the packets at MovieAt
//as it was asked for, from its place (in packets), which starts over at the pool's start when they'd run past it.
auto feedingCallback(KernelMachine& m, u32 pool = 0) -> void {
  Assembler c{m, CallbackCode};
  c.put(addiu(sp, sp, -32)); c.put(sw(ra, 28, sp)); c.put(sw(a1, 20, sp));
  c.li(t6, FeedLog); c.put(lw(t7, 0, t6));
  c.put(sll(t8, t7, 2)); c.put(sll(t9, t7, 3)); c.put(addu(t8, t8, t9)); c.put(addu(t8, t8, t6));
  c.li(t9, RingData); c.put(subu(t9, a0, t9)); c.put(sw(t9, 4, t8)); c.put(sw(a1, 8, t8)); c.put(sw(gp, 12, t8));
  c.put(andi(t9, t7, 1023)); c.put(sll(t8, t9, 2)); c.put(sll(t9, t9, 3)); c.put(addu(t8, t8, t9));
  c.li(t9, Answers); c.put(addu(t8, t8, t9)); c.put(sw(t8, 16, sp));  //this call's answer
  c.put(addiu(t7, t7, 1)); c.put(sw(t7, 0, t6));
  if(pool) {
    c.put(lw(t1, 0, a2)); c.put(addu(t2, t1, a1));
    c.li(t3, pool); c.put(sltu(t3, t3, t2)); c.put(movn(t1, zero, t3)); c.put(addu(t2, t1, a1));
    c.put(sw(t2, 0, a2));
    c.put(sll(t1, t1, 11)); c.li(t3, MovieAt); c.put(addu(a1, t1, t3)); c.put(lw(a2, 20, sp)); c.put(sll(a2, a2, 11));
    c.call("sceKernelMemcpy");
  }
  c.put(lw(t8, 16, sp)); c.put(lw(a0, 8, t8));
  u32 toAnswer = c.here();
  c.put(nop); c.put(nop);
  c.put(addiu(t9, zero, -1));
  u32 toDelay = c.here();
  c.put(nop); c.put(nop);
  c.call("sceKernelSleepThread");
  u32 toAnswerAfterSleep = c.here();
  c.put(nop); c.put(nop);
  u32 delay = c.here();
  c.call("sceKernelDelayThread");
  u32 answer = c.here();
  m.system.memory.write(4, toAnswer, beq(a0, zero, s32(answer - toAnswer - 4) / 4));
  m.system.memory.write(4, toDelay, bne(a0, t9, s32(delay - toDelay - 4) / 4));
  m.system.memory.write(4, toAnswerAfterSleep, beq(zero, zero, s32(answer - toAnswerAfterSleep - 4) / 4));
  c.put(lw(t8, 16, sp)); c.put(lw(t1, 0, t8)); c.put(lw(t2, 4, t8)); c.put(lw(a1, 20, sp));
  c.put(addu(v0, a1, t1)); c.put(movn(v0, t2, t2));
  c.put(lw(ra, 28, sp)); c.put(addiu(sp, sp, 32));
  c.put(jr(ra)); c.put(nop);
}

//The feeding callback's log: where (from the ring's start, in packets) and how many each call was asked for.
auto feedings(KernelMachine& m) -> std::vector<std::pair<u32, u32>> {
  std::vector<std::pair<u32, u32>> calls;
  for(u32 n = 0; n < word(m, FeedLog) && n < 4096; n++) {
    calls.push_back({word(m, FeedLog + 4 + n * 12) / 2048, word(m, FeedLog + 8 + n * 12)});
  }
  return calls;
}

//A program that, step by step, feeds the ring (Put asking for so many packets, so many available) or, a step asking
//for ~0, flushes it (sceMpegFlushAllStream), each step's result at Results; then it exits.
auto feedingProgram(KernelMachine& m, u32 code, const std::vector<std::pair<u32, u32>>& steps) -> void {
  Assembler a{m, code};
  for(u32 n = 0; n < steps.size(); n++) {
    if(steps[n].first == ~0u) {
      a.li(a0, Handle); a.call("sceMpegFlushAllStream");
    } else {
      a.li(a0, Ring); a.li(a1, steps[n].first); a.li(a2, steps[n].second); a.call("sceMpegRingbufferPut");
    }
    a.li(t0, Results + n * 4); a.put(sw(v0, 0, t0));
  }
  a.call("sceKernelExitGame");
}

//Random packets, from a seed: mostly packs (a pack header, now and then not MPEG-2's, and up to 7 stuffing bytes)
//of PES packets of random streams and lengths (some running past the packet), the video's (stream 0xe0) with time
//stamps or without, now and then a header that doesn't fit, and access unit delimiters at a rate of the packet's
//(one cut off where a PES packet ends going on in the next one of video); else random bytes.
struct RandomPacks {
  std::mt19937 random;
  u32 cut = 0;  //how much of a delimiter the video so far ends with

  explicit RandomPacks(u32 seed) : random(seed) {}
  auto below(u32 n) -> u32 { return random() % n; }

  auto packet() -> std::vector<u8> {
    static constexpr u8 Delimiter[6] = {0, 0, 0, 1, 0x09, 0xf0}, Others[6] = {0xbd, 0xbe, 0xbf, 0xc0, 0xbb, 0xe1};
    static constexpr u8 Flags[3] = {0x00, 0x80, 0xc0};
    std::vector<u8> p(2048);
    for(auto& byte : p) byte = random();
    if(!below(12)) return p;
    p[0] = 0, p[1] = 0, p[2] = 1, p[3] = 0xba;
    p[4] = below(10) ? 0x44 : random();
    p[13] = 0xf8 | below(8);
    u32 at = 14 + (p[13] & 7), rate = 100 + below(3000);
    while(at + 6 <= 2048 && below(10)) {
      u32 stream = below(3) ? 0xe0 : Others[below(6)], length = below(8) ? below(1200) : below(0x10000);
      p[at] = 0, p[at + 1] = 0, p[at + 2] = 1, p[at + 3] = stream, p[at + 4] = length >> 8, p[at + 5] = length;
      u32 body = at + 6, next = std::min<u32>(body + length, 2048);
      at = next;
      if(stream != 0xe0 || body + 3 > next) continue;
      p[body] = below(12) ? 0x81 : random();
      p[body + 1] = below(10) ? Flags[below(3)] : random();
      u32 stamps = (p[body + 1] >> 6) == 3 ? 10 : (p[body + 1] >> 7) ? 5 : 0;
      p[body + 2] = below(10) ? stamps : below(64);
      for(u32 n = body + 3 + p[body + 2]; n < next; n++) {
        if(cut || !below(rate)) p[n] = Delimiter[cut], cut = (cut + 1) % 6;
      }
    }
    return p;
  }
};

//The random packs test's program: step by step (three words each at Steps), the ring fed (Put asking for the
//step's first word, what's free available), then the step's second word of access units asked for, then the ring
//flushed if its third isn't 0. After each call, four words at Records: its result, then for Put where the ring
//writes next, how many packets hold data and how many calls the callback has had; for sceMpegGetAvcAu the next to
//read, how many hold data and the access unit's size; for sceMpegFlushAllStream the next to read, how many hold data
//and where the ring writes next.
auto randomPacksProgram(KernelMachine& m, u32 code, u32 steps) -> void {
  Assembler a{m, code};
  auto record = [&](u32 base, u32 first, u32 second, u32 third, u32 thirdOffset) {
    a.li(t0, base); a.put(lw(t1, first, t0)); a.put(lw(t2, second, t0));
    a.li(t0, third); a.put(lw(t3, thirdOffset, t0));
    a.put(sw(v0, 0, s1)); a.put(sw(t1, 4, s1)); a.put(sw(t2, 8, s1)); a.put(sw(t3, 12, s1));
    a.put(addiu(s1, s1, 16));
  };
  a.li(a0, Handle); a.li(a1, 1); a.li(a2, Au); a.call("sceMpegInitAu");
  a.li(s0, Steps); a.li(s1, Records); a.li(s2, Steps + steps * 12);
  u32 loop = a.here();
  a.li(a0, Ring); a.call("sceMpegRingbufferAvailableSize");
  a.put(addu(a2, v0, zero)); a.li(a0, Ring); a.put(lw(a1, 0, s0)); a.call("sceMpegRingbufferPut");
  record(Ring, 8, 12, FeedLog, 0);
  a.put(lw(s3, 4, s0));
  u32 toFlush = a.here();
  a.put(nop); a.put(nop);
  u32 take = a.here();
  a.li(a0, Handle); a.li(a1, 0x12c0); a.li(a2, Au); a.li(a3, Attribute); a.call("sceMpegGetAvcAu");
  record(Ring, 4, 12, Au, 20);
  a.put(addiu(s3, s3, -1));
  u32 at = a.here();
  a.put(bne(s3, zero, s32(take - at - 4) / 4)); a.put(nop);
  u32 flush = a.here();
  m.system.memory.write(4, toFlush, beq(s3, zero, s32(flush - toFlush - 4) / 4));
  a.put(lw(t0, 8, s0));
  u32 toNext = a.here();
  a.put(nop); a.put(nop);
  a.li(a0, Handle); a.call("sceMpegFlushAllStream");
  record(Ring, 4, 12, Ring, 8);
  u32 next = a.here();
  m.system.memory.write(4, toNext, beq(t0, zero, s32(next - toNext - 4) / 4));
  a.put(addiu(s0, s0, 12));
  at = a.here();
  a.put(bne(s0, s2, s32(loop - at - 4) / 4)); a.put(nop);
  a.call("sceKernelExitGame");
}
}

//sceMpeg: the sizes video/mpeg's tests recorded, a ringbuffer filled in as ringbuffer/construct recorded (and its
//refusals), the library's signature, then a header that can't be read and streams with nothing in them.
static auto mpegStubs() -> void {
  KernelMachine m;
  for(auto [packets, size] : {std::pair{0u, 0u}, {1u, 0x868u}, {0x200u, 0x10'd000u}, {0x1000u, 0x86'8000u},
                              {u32(-1), 0xffff'f798u}, {0x8000'0000u, 0u}}) {
    check(__LINE__, "a ringbuffer's memory", m.call("sceMpegRingbufferQueryMemSize", {packets}), size);
  }
  CHECK(m.call("sceMpegInit", {}), 0);
  CHECK(m.call("sceMpegQueryMemSize", {0}), 0x10000);
  constexpr u32 Ring = R + 0x100, Data = 0x0894'0000;
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 512, Data, 0x10'd000, 0x0880'4000, 0xdead'beef}), 0);
  CHECK(word(m, Ring) == 512 && word(m, Ring + 4) == 0 && word(m, Ring + 8) == 0 && word(m, Ring + 12) == 0, true);
  CHECK(word(m, Ring + 20) == Data && word(m, Ring + 24) == 0x0880'4000 && word(m, Ring + 28) == 0xdead'beef, true);
  CHECK(word(m, Ring + 32), Data + 512 * 2048);
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 4097, Data, 0x1000, 0, 0}), 0x8061'0022);
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 1, Data, u32(-1), 0, 0}), 0x8061'0022);
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 512, Data, 0x10'd000, 0x0880'4000, 0}), 0);
  CHECK(m.call("sceMpegRingbufferAvailableSize", {Ring}), 512);
  constexpr u32 Handle = R + 0x40, Memory = 0x0895'0000;
  CHECK(m.call("sceMpegCreate", {Handle, Memory, 0x10000, Ring, 512, 0, 0}), 0);
  CHECK(m.system.memory.readString(word(m, Handle), 8) == "LIBMPEG", true);
  CHECK(m.call("sceMpegCreate", {Handle, Memory, 0xffff, Ring, 512, 0, 0}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceMpegQueryStreamOffset", {Handle, Buffer, R}), 0x8061'0022);
  CHECK(m.call("sceMpegQueryStreamSize", {Buffer, R}), 0x8061'0022);
  u32 stream = m.call("sceMpegRegistStream", {Handle, 0, 0});
  CHECK(stream != 0 && stream < 0x8000'0000, true);
  CHECK(m.call("sceMpegMallocAvcEsBuf", {Handle}), 1);
  CHECK(m.call("sceMpegRingbufferPut", {Ring, 0x80, 512}), 0);
  CHECK(m.call("sceMpegGetAvcAu", {Handle, stream, R, R + 4}), 0x8061'8001);
  CHECK(m.call("sceMpegGetAtracAu", {Handle, stream, R, R + 4}), 0x8061'8001);
  CHECK(m.call("sceMpegAtracDecode", {Handle, R, Buffer, 0}), 0x807f'00fd);
  CHECK(m.call("sceMpegQueryAtracEsSize", {Handle, R, R + 4}), 0);
  CHECK(word(m, R) == 0x840 && word(m, R + 4) == 0x2000, true);
  m.system.memory.write(4, R + 8, 1);
  CHECK(m.call("sceMpegAvcDecode", {Handle, R, 512, Buffer, R + 8}), 0);
  CHECK(word(m, R + 8), 0);  //no frame
  CHECK(m.call("sceMpegAvcQueryYCbCrSize", {Handle, 1, 480, 272, R + 12}), 0);
  CHECK(word(m, R + 12) >= 480 * 272 * 3 / 2, true);
  CHECK(m.call("sceMpegAvcQueryYCbCrSize", {Handle, 1, 0x2000, 272, R + 12}), 0x8061'0022);
  for(auto name : {"sceMpegAvcInitYCbCr", "sceMpegAvcDecodeFlush", "sceMpegFlushAllStream", "sceMpegFreeAvcEsBuf",
                   "sceMpegUnRegistStream", "sceMpegDelete", "sceMpegRingbufferDestruct", "sceMpegFinish"}) {
    check(__LINE__, name, m.call(name, {Handle, 1, 480, 272, Buffer}), 0);
  }
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//A movie fed and taken apart as video/mpeg/basic recorded (worked out by movieModel()): a ringbuffer of 16 packets
//fed 3 at a time, so that runs wrap at its end and the file ends part way through one; twelve access units, three
//with time stamps (one past 32 bits), each taken once the next one's delimiter is in, the last once the file has
//ended; packets free again as their video is taken, the header packet with the first; a picture from the second
//decoded on; then "no data", the ring empty. On both engines, and the state round trip. With the game's own copy of
//the library loaded first (a module named sceMpeg_library, stood in for), every access unit decoded gives its
//picture, the first too (mpeg.cpp's mpegDecoded(): Killzone's movie needs it).
static auto mpegMovie(bool ownLibrary) -> void {
  constexpr u64 None = Movie::None;
  Movie movie({3000, 700, 1500, 2600, 900, 4100, 500, 1800, 2200, 1200, 3500, 800},
              {90000, None, None, None, None, 90000 + 5 * 3003, None, None, None, 0x1'0000'0000ull + 3003, None,
               None});
  constexpr u32 Frames = 20, Ask = 3, Packets = 16;
  std::vector<std::pair<u32, u32>> log;
  auto expected = movieModel(movie, Packets, Frames, Ask, log);
  //the model itself, as basic recorded: the access units in order, their time stamps, the pictures, the ring at the
  //end
  std::vector<u32> sizes;
  for(auto& frame : expected) if(frame.result == 0) sizes.push_back(frame.size);
  CHECK(sizes == std::vector<u32>({3000, 700, 1500, 2600, 900, 4100, 500, 1800, 2200, 1200, 3500, 800}), true);
  u32 first = 0;
  while(expected[first].result) first++;
  CHECK(expected[first].presented == 90000 && expected[first].decoded == 90000 - 3003, true);
  CHECK(expected[first].picture == 0 && expected[first + 1].picture == 1, true);
  CHECK(expected[first + 1].presented == ~0u && expected[first + 1].decodedHigh == ~0u, true);
  CHECK(expected.back().result == NoData && expected.back().before == Packets, true);
  CHECK(log.front().first == 0 && log.front().second == 3 && log.back().second > 0, true);
  if(ownLibrary) {
    for(auto& frame : expected) frame.picture = frame.size != 0;
    CHECK(expected[first].picture == 1, true);
  }
  for(bool recompile : {false, true}) {
    KernelMachine m;
    if(ownLibrary) {
      CHECK(m.kernel.standIn("sceMpeg_library", 0, "disc0:/MPEG.PRX") != 0, true);
      m.notes.clear();  //(its "Sony's sceMpeg_library" note)
    }
    mpegSetUp(m, movie, Packets);
    mpegCallback(m, movie, false, false);
    movieProgram(m, 0x0880'1000, Frames, Ask);
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto frames = movieFrames(m, Frames);
    for(u32 n = 0; n < Frames; n++) {
      if(frames[n] == expected[n]) continue;
      auto& f = frames[n];
      auto& e = expected[n];
      std::printf("  frame %u: %u %u %u %08x %u %u %u (expected %u %u %u %08x %u %u %u)\n", n, f.before, f.put,
                  f.after, f.result, f.presented, f.size, f.picture, e.before, e.put, e.after, e.result, e.presented,
                  e.size, e.picture);
      CHECK(frames[n] == expected[n], true);
    }
    CHECK(word(m, Log), log.size());
    for(u32 n = 0; n < log.size() && n < word(m, Log); n++) {
      check(__LINE__, "a callback's call", word(m, Log + 4 + n * 8) == log[n].first && word(m, Log + 8 + n * 8) ==
            log[n].second, true);
    }
    CHECK(word(m, Attribute), 1);
    CHECK(word(m, Ring + 12) == 0 && m.kernel.mpegCalls.empty(), true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Space Invaders Extreme's movie thread: of the best priority, it asks for an access unit, feeding the ringbuffer
//while there's none (its callback copies the movie from memory, from the start again at its end), and decodes until
//a picture comes, which it asks the details of (sceMpegAvcDecodeDetail) and tells of; then it waits for the next
//request, or to be told to stop. The program's main thread, worse, waits for the first picture, then tells the
//movie thread to stop and waits for it to end. (With nothing ever put in, the movie thread had asked for ever and
//main never ran; with no details to be had, main waited for good.) On both engines, and the state round trip.
static auto mpegMovieThread() -> void {
  Movie movie({1500, 2500, 900, 3000}, {90000, Movie::None, Movie::None, Movie::None});
  constexpr u32 FlagID = R + 0x90, Out = R + 0x94, Taken = R + 0x98;
  for(bool recompile : {false, true}) {
    KernelMachine m;
    mpegSetUp(m, movie, 64);
    mpegCallback(m, movie, false, true);
    Assembler t{m, 0x0880'2000};
    u32 wait = t.here();
    t.li(t0, FlagID); t.put(lw(a0, 0, t0)); t.li(a1, 5); t.li(a2, 0x21); t.li(a3, Out); t.li(t0, 0);
    t.call("sceKernelWaitEventFlag");
    t.li(t0, Out); t.put(lw(t1, 0, t0)); t.put(andi(t1, t1, 4));
    u32 toQuit = t.here();
    t.put(nop); t.put(nop);
    u32 take = t.here();
    t.li(a0, Handle); t.li(a1, 0x12c0); t.li(a2, Au); t.li(a3, 0);
    t.call("sceMpegGetAvcAu");
    u32 toDecode = t.here();
    t.put(nop); t.put(nop);
    t.li(t0, NoData);
    u32 here = t.here();
    t.put(bne(v0, t0, s32(wait - here - 4) / 4)); t.put(nop);
    t.li(a0, Ring); t.call("sceMpegRingbufferAvailableSize");
    t.li(a0, Ring); t.li(a1, 4); t.put(addu(a2, v0, zero)); t.call("sceMpegRingbufferPut");
    here = t.here();
    t.put(beq(zero, zero, s32(take - here - 4) / 4)); t.put(nop);
    u32 decode = t.here();
    m.system.memory.write(4, toDecode, beq(v0, zero, s32(decode - toDecode - 4) / 4));
    t.li(a0, Handle); t.li(a1, Au); t.li(a2, Buffer); t.li(a3, Got); t.call("sceMpegAvcDecodeYCbCr");
    t.li(t0, Taken); t.put(lw(t1, 0, t0)); t.put(addiu(t1, t1, 1)); t.put(sw(t1, 0, t0));
    t.li(t0, Got); t.put(lw(t1, 0, t0));
    here = t.here();
    t.put(beq(t1, zero, s32(take - here - 4) / 4)); t.put(nop);
    t.li(a0, Handle); t.li(a1, R + 0xa0); t.call("sceMpegAvcDecodeDetail");
    t.li(t0, R + 0x9c); t.put(sw(v0, 0, t0));
    here = t.here();
    t.put(bne(v0, zero, s32(wait - here - 4) / 4)); t.put(nop);
    t.print("picture\n");
    t.li(t0, FlagID); t.put(lw(a0, 0, t0)); t.li(a1, 2); t.call("sceKernelSetEventFlag");
    here = t.here();
    t.put(beq(zero, zero, s32(wait - here - 4) / 4)); t.put(nop);
    u32 quit = t.here();
    m.system.memory.write(4, toQuit, bne(t1, zero, s32(quit - toQuit - 4) / 4));
    t.li(a0, 0); t.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("movie")); main.li(a1, 0x200); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.li(t0, FlagID); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("movie")); main.li(a1, 0x0880'2000); main.li(a2, 0x11); main.li(a3, 0x4000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0); main.call("sceKernelStartThread");
    main.li(t0, FlagID); main.put(lw(a0, 0, t0)); main.li(a1, 2); main.li(a2, 1); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelWaitEventFlag");
    main.print("main\n");
    main.li(t0, FlagID); main.put(lw(a0, 0, t0)); main.li(a1, 4); main.call("sceKernelSetEventFlag");
    main.put(addu(a0, s0, zero)); main.li(a1, 0); main.call("sceKernelWaitThreadEnd");
    main.print("ended\n");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "picture\nmain\nended\n", true);
    if(m.output != "picture\nmain\nended\n") std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, Taken), 2);  //the first access unit decoded gave no picture
    CHECK(word(m, Got) == 1 && word(m, R + 0x9c) == 0, true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//States: a feeding part way through, its callback waiting (as a read would), carries on in another machine as it
//would have in the first: the same frames, the same calls of the callback. On both engines.
static auto mpegCallbackStates() -> void {
  constexpr u64 None = Movie::None;
  Movie movie({3000, 700, 1500, 2600, 900, 4100}, {90000, None, None, None, 90000 + 4 * 3003, None});
  constexpr u32 Frames = 12, Ask = 3, Packets = 16;
  auto prepare = [&](KernelMachine& m, bool recompile) {
    mpegSetUp(m, movie, Packets);
    mpegCallback(m, movie, true, false);
    movieProgram(m, 0x0880'1000, Frames, Ask);
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  };
  auto logged = [&](KernelMachine& m) {
    std::vector<u32> words;
    for(u32 n = 0; n <= word(m, Log) * 2 && n < 256; n++) words.push_back(word(m, Log + n * 4));
    return words;
  };
  for(bool recompile : {false, true}) {
    KernelMachine whole;
    prepare(whole, recompile);
    whole.kernel.run(Kernel::CPUFrequency / 3);
    CHECK(whole.kernel.exited, true);
    auto frames = movieFrames(whole, Frames);
    for(u64 at : {Kernel::CPUFrequency / 5000, Kernel::CPUFrequency / 1000, Kernel::CPUFrequency / 300}) {
      KernelMachine part;
      prepare(part, recompile);
      part.kernel.run(at);
      CHECK(part.kernel.mpegCalls.size(), 1);  //waiting in the callback
      auto state = saveState(part);
      KernelMachine fresh;
      fresh.system.recompiler.enabled = recompile;
      CHECK(loadState(fresh, state), true);
      CHECK(saveState(fresh) == state, true);
      fresh.kernel.run(Kernel::CPUFrequency / 3);
      CHECK(fresh.kernel.exited, true);
      CHECK(movieFrames(fresh, Frames) == frames && logged(fresh) == logged(whole), true);
      CHECK(fresh.kernel.mpegCalls.empty() && fresh.notes.empty(), true);
    }
  }
}

//A ring whose fields, the game's to write, couldn't be a ring's is given nothing (sceMpegGetAvcAu's test), its
//fields left as they were: 8192 packets, -200 or 17 of 16 holding data, the next to write at 16 of 16, none, and
//0x7fffffff packets with 0x80000000 holding data. (The last had overflowed; 8192 packets, and -200 holding data, had
//the callback asked for 5000 and 4296, more than a state holds.) A state saved where the callback, which waits,
//would have been waiting loads into another machine. A ring that is one, as a check, is fed as it waits. On both
//engines.
static auto mpegRingbufferNotARing() -> void {
  struct Fields { u32 packets, written, filled; bool fed; };
  for(bool recompile : {false, true}) {
    for(auto f : {Fields{16, 0, 0, true}, {8192, 0, 0, false}, {4096, 0, u32(-200), false}, {16, 0, 17, false},
                  {16, 16, 0, false}, {0, 0, 0, false}, {0x7fff'ffff, 0, 0x8000'0000, false}}) {
      KernelMachine m;
      feedingSetUp(m, 16, {{0, 0, 500}});
      feedingCallback(m);
      m.system.memory.write(4, Ring, f.packets);
      m.system.memory.write(4, Ring + 8, f.written);
      m.system.memory.write(4, Ring + 12, f.filled);
      feedingProgram(m, 0x0880'1000, {{5000, 5000}});
      m.system.recompiler.enabled = recompile;
      m.system.power(0x0880'1000);
      s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
      m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
      m.kernel.run(Kernel::CPUFrequency / 5000);  //200 microseconds
      CHECK(m.kernel.mpegCalls.size(), f.fed ? 1 : 0);
      auto state = saveState(m);
      KernelMachine fresh;
      fresh.system.recompiler.enabled = recompile;
      CHECK(loadState(fresh, state) && saveState(fresh) == state, true);
      fresh.kernel.run(Kernel::CPUFrequency / 3);
      CHECK(fresh.kernel.exited, true);
      CHECK(word(fresh, Results) == (f.fed ? 16 : 0) && word(fresh, FeedLog) == (f.fed ? 1 : 0), true);
      if(!f.fed) CHECK(word(fresh, Ring + 8) == f.written && word(fresh, Ring + 12) == f.filled, true);
      CHECK(fresh.notes.size(), 0);
    }
  }
}

//The callback runs with the global pointer of Put's caller, not the word after a ring of pspsdk's 44 bytes (here
//someone else's, written after the ring was made). On both engines.
static auto mpegCallbackGlobalPointer() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    feedingSetUp(m, 16, {});
    feedingCallback(m);
    m.system.memory.write(4, Ring + 44, 0xdead'beef);
    Assembler a{m, 0x0880'1000};
    a.li(gp, 0x0881'2340);
    a.li(a0, Ring); a.li(a1, 4); a.li(a2, 16); a.call("sceMpegRingbufferPut");
    a.li(t0, Results); a.put(sw(v0, 0, t0));
    a.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(word(m, Results), 4);
    CHECK(word(m, FeedLog), 1);
    CHECK(word(m, FeedLog + 12), 0x0881'2340);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//A callback that says it gave 100 more than it was asked for gave what it was asked for: Put counts that, the ring
//moving on by it (a run up to the ring's end, then one from its start), and asks for no more. (Counting the 100 had
//the ring hold more than its packets, and what was left to ask for wrap round.) On both engines, and the state
//round trip.
static auto mpegCallbackGivesMore() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    feedingSetUp(m, 16, std::vector<std::array<u32, 3>>(8, {100, 0, 0}));
    feedingCallback(m);
    feedingProgram(m, 0x0880'1000, {{13, 16}, {~0u, 0}, {10, 16}, {100, 100}, {5, 5}});
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(word(m, Results) == 13 && word(m, Results + 8) == 10 && word(m, Results + 12) == 6, true);
    CHECK(word(m, Results + 16), 0);  //the ring full
    CHECK((feedings(m) == std::vector<std::pair<u32, u32>>{{0, 13}, {13, 3}, {0, 7}, {7, 6}}), true);
    CHECK(word(m, Ring + 4) == 13 && word(m, Ring + 8) == 13 && word(m, Ring + 12) == 16, true);
    CHECK(m.kernel.mpegCalls.empty() && m.notes.empty(), true);
    CHECK(roundTrip(m), true);
  }
}

//A callback that returns an error (0x80020001, or -1) gave nothing, and isn't asked again: Put returns what the
//calls before it gave, as when a callback gives none (an error at once: 0). (Counted as what it was asked for, the
//error had put in packets that never came.) On both engines, and the state round trip.
static auto mpegCallbackError() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    feedingSetUp(m, 16, {{0, 0, 0}, {0, 0, 0}, {0, 0x8002'0001, 0}, {0, 0x8002'0001, 0}, {u32(-2), 0, 0},
                         {0, ~0u, 0}});
    feedingCallback(m);
    feedingProgram(m, 0x0880'1000, {{13, 16}, {~0u, 0}, {10, 16}, {4, 16}, {4, 16}});
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(word(m, Results) == 13 && word(m, Results + 8) == 3, true);
    CHECK(word(m, Results + 12) == 0 && word(m, Results + 16) == 2, true);
    CHECK((feedings(m) == std::vector<std::pair<u32, u32>>{{0, 13}, {13, 3}, {0, 7}, {0, 4}, {0, 4}, {2, 2}}), true);
    CHECK(word(m, Ring + 4) == 13 && word(m, Ring + 8) == 2 && word(m, Ring + 12) == 5, true);
    CHECK(m.kernel.mpegCalls.empty() && m.notes.empty(), true);
    CHECK(roundTrip(m), true);
  }
}

//A feeder thread terminated as it sleeps in its callback (sceKernelTerminateThread) ends its feeding with it: what
//came stays put in, and started again it feeds anew (its feeding had been left behind, and Put, finding it, gave
//nothing); terminated and deleted (sceKernelTerminateDeleteThread), it leaves nothing behind. A state saved as it
//sleeps carries on in another machine as in the first. And a state holding a dormant thread's feeding, which the
//loader takes (it checks only that the thread is there) though no feeding leaves one: deleting the thread drops it,
//or the next state would be refused. On both engines.
static auto mpegFeederTerminated() -> void {
  constexpr u32 Runs = R + 0x200;
  auto prepare = [&](KernelMachine& m, bool recompile) {
    feedingSetUp(m, 16, {{u32(-2), 0, 0}, {0, 0, Sleep}, {0, 0, 0}, {0, 0, Sleep}});
    feedingCallback(m);
    for(u32 run = 1; run <= 3; run++) m.system.memory.write(4, Results + run * 4, 0x1337);
    //the feeder: counts its runs, asks for 4 packets and keeps what Put returned by its run
    Assembler feeder{m, 0x0880'2000};
    feeder.li(t0, Runs); feeder.put(lw(t1, 0, t0)); feeder.put(addiu(t1, t1, 1)); feeder.put(sw(t1, 0, t0));
    feeder.li(a0, Ring); feeder.li(a1, 4); feeder.li(a2, 16); feeder.call("sceMpegRingbufferPut");
    feeder.li(t0, Runs); feeder.put(lw(t1, 0, t0)); feeder.put(sll(t1, t1, 2));
    feeder.li(t0, Results); feeder.put(addu(t0, t0, t1)); feeder.put(sw(v0, 0, t0));
    feeder.li(a0, 0); feeder.call("sceKernelExitThread");
    //main: starts the feeder three times (of a better priority, it runs at once), waiting a millisecond after each;
    //it terminates the first run, and terminates and deletes the third
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("feeder")); main.li(a1, 0x0880'2000); main.li(a2, 0x10); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    for(u32 run = 0; run < 3; run++) {
      main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0); main.call("sceKernelStartThread");
      main.li(t0, Results + 0x20 + run * 4); main.put(sw(v0, 0, t0));
      main.li(a0, 1000); main.call("sceKernelDelayThread");
      if(run == 1) continue;
      main.put(addu(a0, s0, zero)); main.call(run ? "sceKernelTerminateDeleteThread" : "sceKernelTerminateThread");
      main.li(t0, Results + (run ? 0x34 : 0x30)); main.put(sw(v0, 0, t0));
    }
    main.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  };
  auto results = [&](KernelMachine& m) {
    std::vector<u32> words;
    for(u32 n = 0; n < 14; n++) words.push_back(word(m, Results + n * 4));
    return words;
  };
  for(bool recompile : {false, true}) {
    KernelMachine whole;
    prepare(whole, recompile);
    whole.kernel.run(Kernel::CPUFrequency / 3);
    CHECK(whole.kernel.exited, true);
    auto got = results(whole);
    CHECK(got[1] == 0x1337 && got[2] == 4 && got[3] == 0x1337, true);  //Put returned only from the second run
    CHECK(got[8] == 0 && got[9] == 0 && got[10] == 0 && got[12] == 0 && got[13] == 0, true);
    CHECK((feedings(whole) == std::vector<std::pair<u32, u32>>{{0, 4}, {2, 2}, {2, 4}, {6, 4}}), true);
    CHECK(word(whole, Ring + 8) == 6 && word(whole, Ring + 12) == 6, true);
    CHECK(whole.kernel.mpegCalls.empty() && whole.notes.empty(), true);
    CHECK(roundTrip(whole), true);
    for(u64 at : {Kernel::CPUFrequency / 2000, Kernel::CPUFrequency * 5 / 2000}) {
      KernelMachine part;
      prepare(part, recompile);
      part.kernel.run(at);
      CHECK(part.kernel.mpegCalls.size(), 1);  //asleep in its callback
      auto state = saveState(part);
      KernelMachine fresh;
      fresh.system.recompiler.enabled = recompile;
      CHECK(loadState(fresh, state) && saveState(fresh) == state, true);
      fresh.kernel.run(Kernel::CPUFrequency / 3);
      CHECK(fresh.kernel.exited, true);
      CHECK(results(fresh) == got && feedings(fresh) == feedings(whole), true);
      CHECK(fresh.kernel.mpegCalls.empty() && fresh.notes.empty(), true);
    }
  }
  KernelMachine m;
  feedingSetUp(m, 16, {});
  s32 uid = m.kernel.createThread("feeder", 0x0880'2000, 0x10, 0x1000, 0, 0);
  Kernel::MpegCall call;
  call.ringbuffer = Ring, call.left = 4, call.asked = 4;
  m.kernel.mpegCalls[uid] = call;
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceKernelDeleteThread", {u32(uid)}), 0);
  CHECK(m.kernel.mpegCalls.empty(), true);
  CHECK(roundTrip(m), true);
}

//Random packs taken apart, from a seed: rings of 8, 24 and 64 packets fed from 256 random packets (RandomPacks) by
//a callback that now and then gives more than it was asked for, fewer, none or an error; after each feeding, a few
//access units asked for, and now and then the ring flushed. Whatever the packs, the next packet to read stays among
//the ring's, and those holding data no more than it has: an access unit frees packets from the next to read on,
//"no data" changes nothing; Put gives what the callback's calls gave, each counted as no more than it was asked for,
//up to the first that gave none or an error, or packets past a movie's end (once the ring has had a pack, one that
//isn't: those before it counted; a flush starts that afresh), each asked for a run from where the ring writes next.
//On both engines.
static auto mpegRandomPacks() -> void {
  constexpr u32 StepCount = 250, Pool = 256;
  for(u32 round = 0; round < 3; round++) {
    u32 packets = std::array<u32, 3>{8, 24, 64}[round];
    RandomPacks packs(0x5eed + round);
    std::vector<u8> pool;
    for(u32 n = 0; n < Pool; n++) {
      auto packet = packs.packet();
      pool.insert(pool.end(), packet.begin(), packet.end());
    }
    std::vector<std::array<u32, 3>> answers(1024);
    for(auto& answer : answers) {
      u32 roll = packs.below(100);
      u32 add = roll < 70 ? 0 : roll < 78 ? 100 : roll < 83 ? 1 : roll < 90 ? u32(-1) : roll < 95 ? u32(-3) : 0;
      answer = {add, roll < 95 ? 0 : roll < 98 ? 0x8002'0001 : ~0u, 0};
    }
    std::vector<std::array<u32, 3>> steps(StepCount);  //asked for, access units asked for, flushed
    for(auto& step : steps) {
      step = {packs.below(10) ? 1 + packs.below(packets + 4) : 0, packs.below(4), !packs.below(20)};
    }
    for(bool recompile : {false, true}) {
      KernelMachine m;
      m.system.memory.copyIn(MovieAt, pool.data(), pool.size());
      feedingSetUp(m, packets, answers);
      feedingCallback(m, Pool);
      for(u32 n = 0; n < StepCount; n++) {
        for(u32 i = 0; i < 3; i++) m.system.memory.write(4, Steps + n * 12 + i * 4, steps[n][i]);
      }
      randomPacksProgram(m, 0x0880'1000, StepCount);
      m.runProgram(0x0880'1000, recompile);
      CHECK(m.kernel.exited, true);
      u32 read = 0, written = 0, filled = 0, calls = 0, units = 0, at = Records, source = 0;
      bool packed = false, headed = false;  //the ring's been given a pack; the movie's header before the first
      std::string wrong;
      for(u32 n = 0; n < StepCount && wrong.empty(); n++) {
        auto next = [&](u32 offset) { return word(m, at + offset); };
        //the feeding
        u32 wanted = std::min(steps[n][0], packets - filled), gave = 0, place = written;
        bool done = !wanted;
        for(u32 call = calls; call < next(12) && wrong.empty(); call++) {
          u32 where = word(m, FeedLog + 4 + call * 12), asked = word(m, FeedLog + 8 + call * 12);
          if(done || where != place * 2048 || !asked || asked > wanted - gave || place + asked > packets) {
            wrong = "a callback asked for the wrong packets";
          }
          u32 from = source + asked > Pool ? 0 : source;  //where the callback copied from: its place in the pool
          source = from + asked;
          auto& answer = answers[call & 1023];
          s32 returned = answer[1] ? s32(answer[1]) : s32(asked + answer[0]);
          if(returned <= 0) { done = true; continue; }
          u32 given = std::min<u32>(returned, asked), taken = 0;
          for(; taken < given; taken++) {
            u32 first = 0;
            for(u32 i = 0; i < 4; i++) first |= u32(pool[(from + taken) * 2048 + i]) << i * 8;
            bool pack = first == 0xba01'0000, header = first == 0x464d'5350;
            if(packed && !pack) {
              if(!header || !headed) break;
              packed = false;
            }
            headed |= header && !packed;
            packed |= pack;
          }
          gave += taken, place = (gave + written) % packets;
          done = gave == wanted || taken < given;
        }
        if(wrong.empty() && (!done || next(0) != gave || next(4) != place || next(8) != filled + gave)) {
          wrong = "Put gave what its callback's calls didn't";
        }
        if(next(8) > packets) wrong = "more packets holding data than the ring has";
        calls = next(12), written = next(4), filled = next(8);
        at += 16;
        //the access units
        for(u32 unit = 0; unit < steps[n][1] && wrong.empty(); unit++, at += 16) {
          u32 result = next(0), nowRead = next(4), nowFilled = next(8), size = next(12), freed = filled - nowFilled;
          if(nowRead >= packets || nowFilled > packets) {
            wrong = "the next to read, or those holding data, out of the ring";
          } else if(result == NoData && (nowRead != read || nowFilled != filled)) {
            wrong = "no data, but the ring changed";
          } else if(result != NoData && result != 0) {
            wrong = "neither an access unit nor no data";
          } else if(!result && (nowFilled > filled || (read + freed) % packets != nowRead || size < 5 ||
                                size > filled * 2048)) {
            wrong = "an access unit freed the wrong packets";
          }
          units += !result;
          read = nowRead, filled = nowFilled;
        }
        //the flush
        if(steps[n][2] && wrong.empty()) {
          if(next(0) != 0 || next(4) != written || next(8) != 0 || next(12) != written) wrong = "a flush left data";
          read = written, filled = 0, packed = headed = false;
          at += 16;
        }
        if(!wrong.empty()) std::printf("  ring of %u, step %u: %s\n", packets, n, wrong.c_str());
      }
      CHECK(wrong.empty(), true);
      CHECK(units >= 50, true);
      CHECK(m.notes.size(), 0);
    }
  }
}

//The network libraries with the switch off: they start and stop, nothing connects, lists are empty, the PSP's own
//address and its text.
static auto networkOff() -> void {
  KernelMachine m;
  CHECK(m.call("sceWlanGetSwitchState", {}), 0);
  for(auto name : {"sceNetInit", "sceNetAdhocInit", "sceNetAdhocctlInit", "sceNetAdhocMatchingInit",
                   "sceNetInetInit", "sceNetApctlInit", "sceNetResolverInit", "sceNetAdhocctlAddHandler",
                   "sceNetAdhocctlDisconnect", "sceNetAdhocMatchingTerm", "sceNetAdhocctlTerm", "sceNetAdhocTerm",
                   "sceNetTerm"}) {
    check(__LINE__, name, m.call(name, {0x2000, 0x30, 0x1000, 0x30, 0x1000}), 0);
  }
  for(auto name : {"sceNetAdhocctlConnect", "sceNetAdhocctlCreate", "sceNetAdhocctlScan", "sceNetAdhocPdpCreate",
                   "sceNetAdhocPtpOpen", "sceNetAdhocMatchingCreate", "sceNetAdhocMatchingStart",
                   "sceNetApctlConnect", "sceNetResolverCreate"}) {
    check(__LINE__, name, m.call(name, {Buffer, 0, 0, 0}), Kernel::ErrorNotSupported);
  }
  m.system.memory.write(4, R, 0x1234);
  CHECK(m.call("sceNetAdhocctlGetState", {R}), 0);
  CHECK(word(m, R), 0);
  m.system.memory.write(4, R, 0x1234);
  CHECK(m.call("sceNetAdhocctlGetScanInfo", {R, Buffer}), 0);
  CHECK(word(m, R), 0);
  CHECK(m.call("sceNetGetLocalEtherAddr", {R}), 0);
  CHECK(m.call("sceNetEtherNtostr", {R, R + 0x10}), 0);
  CHECK(m.system.memory.readString(R + 0x10, 32) == "02:00:00:50:53:50", true);
  m.system.memory.copyIn(R + 0x40, "1a:2B:3c:4D:5e:6F", 18);
  CHECK(m.call("sceNetEtherStrton", {R + 0x40, R + 0x60}), 0);
  CHECK(word(m, R + 0x60) == 0x4d3c'2b1a && m.system.memory.read(2, R + 0x64) == 0x6f5e, true);
  CHECK(m.call("sceNetInetSocket", {2, 1, 0}), 0xffff'ffff);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//The small functions: the date now (UTC, in a time zone, the host's local time), a date as a time_t, the OpenPSID,
//the display's lines since power on and its rate, interrupts let through, memset and memcpy (overlapping), later
//SDKs' clock setter, the AV modules, the priority functions' refusals.
static auto oddsAndEnds() -> void {
  KernelMachine m;
  m.kernel.startTime = 1'791'290'096'000'000ull;  //2026-10-06 12:34:56 UTC
  m.kernel.cycles = 2'500'000 * (Kernel::CPUFrequency / 1'000'000);  //2.5 s on
  m.kernel.nextVblank = m.kernel.cycles + 1000;
  auto date = [&](u32 at) {
    std::array<u32, 7> fields;
    for(u32 n = 0; n < 6; n++) fields[n] = m.system.memory.read(2, at + n * 2);
    fields[6] = word(m, at + 12);
    return fields;
  };
  CHECK(m.call("sceRtcGetCurrentClock", {R, 0}), 0);
  CHECK((date(R) == std::array<u32, 7>{2026, 10, 6, 12, 34, 58, 500'000}), true);
  CHECK(m.call("sceRtcGetCurrentClock", {R, u32(-90)}), 0);  //90 minutes west
  CHECK((date(R) == std::array<u32, 7>{2026, 10, 6, 11, 4, 58, 500'000}), true);
  CHECK(m.call("sceRtcGetCurrentClockLocalTime", {R}), 0);
  std::time_t seconds = 1'791'290'098;
  std::tm local{};
  localtime_r(&seconds, &local);
  CHECK(date(R) == (std::array<u32, 7>{u32(local.tm_year + 1900), u32(local.tm_mon + 1), u32(local.tm_mday),
                                       u32(local.tm_hour), u32(local.tm_min), u32(local.tm_sec), 500'000}), true);
  CHECK(m.call("sceRtcGetCurrentClockLocalTime", {0}), Kernel::ErrorInvalidPointer);
  for(auto [address, value] : {std::pair{0u, 2000u}, {2, 1}, {4, 1}, {6, 0}, {8, 0}, {10, 1}}) {
    m.system.memory.write(2, R + 0x20 + address, value);
  }
  CHECK(m.call("sceRtcGetTime_t", {R + 0x20, R + 0x40}), 0);
  CHECK(word(m, R + 0x40), 946'684'801);
  //DOS times, as rtc/convert recorded: 2107-09-11 24:00:00 is 4281057280; 1979 and 2108 don't fit
  for(auto [address, value] : {std::pair{0u, 2107u}, {2, 9}, {4, 11}, {6, 24}, {8, 0}, {10, 0}}) {
    m.system.memory.write(2, R + 0x20 + address, value);
  }
  CHECK(m.call("sceRtcGetDosTime", {R + 0x20, R + 0x40}), 0);
  CHECK(word(m, R + 0x40), 4'281'057'280u);
  m.system.memory.write(2, R + 0x20, 1979);
  CHECK(m.call("sceRtcGetDosTime", {R + 0x20, R + 0x40}), 0xffff'ffff);
  m.system.memory.write(2, R + 0x20, 2108);
  CHECK(m.call("sceRtcGetDosTime", {R + 0x20, R + 0x40}), 0xffff'ffff);
  for(auto [time, expected] : {std::pair{100u, std::array<u32, 7>{1980, 0, 0, 0, 3, 8, 0}},
                               {10'000'000u, std::array<u32, 7>{1980, 4, 24, 18, 52, 0, 0}}}) {
    CHECK(m.call("sceRtcSetDosTime", {R + 0x60, time}), 0);
    check(__LINE__, "a DOS time's date", date(R + 0x60) == expected, true);
  }
  CHECK(m.call("sceOpenPSIDGetOpenPSID", {R + 0x50}), 0);
  CHECK(m.system.memory.readString(R + 0x52, 6) == "PHOBOS" && m.system.memory.read(1, R + 0x5f) == 1, true);
  m.kernel.vblanks = 3;
  m.kernel.cycles = m.kernel.nextVblank - Kernel::VblankCycles + 10 * Kernel::LineCycles + 5;
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 3 * 286 + 10);
  m.call("sceDisplayGetFramePerSec", {});
  float rate;
  u32 bits = m.system.fpu.r[0];
  memcpy(&rate, &bits, 4);
  CHECK(rate == 59.94f, true);
  CHECK(m.call("sceKernelIsCpuIntrEnable", {}), 1);
  m.call("sceKernelCpuSuspendIntr", {});
  CHECK(m.call("sceKernelIsCpuIntrEnable", {}), 0);
  m.call("sceKernelCpuResumeIntr", {1});
  for(u32 n = 0; n < 16; n++) m.system.memory.write(1, Buffer + n, n);
  CHECK(m.call("sceKernelMemcpy", {Buffer + 4, Buffer, 8}), Buffer + 4);  //overlapping, as memmove
  CHECK(word(m, Buffer + 4) == 0x0302'0100 && word(m, Buffer + 8) == 0x0706'0504, true);
  CHECK(m.call("sceKernelMemset", {Buffer, 0xaa, 3}), Buffer);
  CHECK(word(m, Buffer), 0x03aa'aaaa);
  m.system.ipu.r[4] = 333, m.system.ipu.r[5] = 333, m.system.ipu.r[6] = 166;
  m.kernel.syscall(m.kernel.importCode("scePower", 0x4699'89ad));
  CHECK(m.system.ipu.r[2], 0);
  CHECK(m.kernel.powerState.pll == 333 && m.kernel.powerState.cpu == 333 && m.kernel.powerState.bus == 166, true);
  m.system.ipu.r[4] = 222, m.system.ipu.r[5] = 222, m.system.ipu.r[6] = 111;
  m.kernel.syscall(m.kernel.importCode("scePower", 0xebd1'77d6));
  CHECK(m.system.ipu.r[2] == 0 && m.kernel.powerState.cpu == 222, true);
  CHECK(m.call("sceKernelGetGPI", {}), 0);
  CHECK(m.call("sceUtilityLoadAvModule", {1}), 0);
  CHECK(m.kernel.utilityModules == std::vector<u32>{0x301}, true);
  CHECK(m.call("sceUtilityLoadAvModule", {1}), 0x8011'1102);
  CHECK(m.call("sceUtilityLoadAvModule", {8}), 0x8011'1101);
  CHECK(m.call("sceUtilityUnloadAvModule", {1}), 0);
  CHECK(m.call("sceUtilityUnloadAvModule", {1}), 0x8011'1103);
  for(u32 priority : {u32(-1), 1u, 7u, 0x78u}) {
    CHECK(m.call("sceKernelRotateThreadReadyQueue", {priority}), Kernel::ErrorIllegalPriority);
  }
  for(u32 priority : {8u, 0x77u}) CHECK(m.call("sceKernelRotateThreadReadyQueue", {priority}), 0);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Threads: a thread of the caller's priority runs when the caller rotates its line, and the caller reads its priority;
//with dispatch held off, a higher-priority thread started doesn't run, and a wait is refused, until dispatch is
//resumed, when it runs at once; a free lightweight mutex locked with callbacks doesn't run the one notified (it
//never enters the kernel), sceKernelCheckCallback then does. On both engines.
static auto threadOddsAndEnds() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler equal{m, 0x0880'2000};
    equal.print("equal\n");
    equal.call("sceKernelExitThread");
    Assembler high{m, 0x0880'2100};
    high.print("high\n");
    high.call("sceKernelExitThread");
    Assembler callback{m, 0x0880'2200};
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.print("callback\n");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry, u32 priority) {
      main.li(a0, m.string("thread")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    start(0x0880'2000, 0x20);  //equal: it waits its turn
    main.print("main\n");
    main.li(a0, 0);
    main.call("sceKernelRotateThreadReadyQueue");
    main.print("rotated\n");
    main.call("sceKernelGetThreadCurrentPriority");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.call("sceKernelSuspendDispatchThread");
    main.put(addu(s0, v0, zero));
    main.li(t0, R); main.put(sw(v0, 4, t0));
    start(0x0880'2100, 0x10);  //higher: held off
    main.print("held\n");
    main.li(a0, 100);
    main.call("sceKernelDelayThread");
    main.li(t0, R); main.put(sw(v0, 8, t0));
    main.put(addu(a0, s0, zero));
    main.call("sceKernelResumeDispatchThread");
    main.print("resumed\n");
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'2200); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.put(addu(a0, v0, zero)); main.li(a1, 0);
    main.call("sceKernelNotifyCallback");
    main.li(a0, R + 0x40); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    main.li(a0, R + 0x40); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelLockLwMutexCB");
    main.li(t0, R); main.put(sw(v0, 12, t0));
    main.print("locked\n");
    main.call("sceKernelCheckCallback");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "main\nequal\nrotated\nheld\nhigh\nresumed\nlocked\ncallback\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R), 0x20);
    CHECK(word(m, R + 4), 1);
    CHECK(word(m, R + 8), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 12), 0);
    CHECK(m.kernel.dispatchSuspended, false);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//With dispatching held off, functions that wait are refused before they change anything (CAN_NOT_WAIT), whether
//they'd have had to wait or not, as pspautotests' intr/waits recorded: a lightweight mutex another thread holds (its
//count of waiters left at 0) and a free one (left free); a receive that would take a pipe's buffer and wait for more
//(the buffer left full, no count written), a send that would hand a waiting receiver part of its message (the
//receiver left waiting for all of it); an event flag wait whose bits are set already (left set). A negative pipe size
//and a bad event flag mode are still refused as such, ahead of that. Rotating the caller's line leaves it the CPU: a
//thread of its priority runs only once the caller waits, after dispatching resumed. (The refusals came only as a
//thread would block, after the waiter count, the pipe's bytes and the flag's bits had changed; and rotating switched
//threads.) On both engines.
static auto dispatchHeldOff() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Held = R + 0x200, Free = R + 0x220, Out = 0x0894'0000;
    m.system.memory.write(4, R + 0x48, 0x1337);
    m.system.memory.write(4, R + 0x50, 0x1337);
    m.system.memory.write(4, R + 0x70, 0x1337);
    Assembler worker{m, 0x0880'2000};  //holds the first mutex
    worker.li(a0, Held); worker.li(a1, 1); worker.li(a2, 0);
    worker.call("sceKernelLockLwMutex");
    worker.call("sceKernelSleepThread");
    Assembler receiver{m, 0x0880'2100};  //waits for 0x10 bytes on the pipe without a buffer
    receiver.li(t0, R + 0x14); receiver.put(lw(a0, 0, t0)); receiver.li(a1, Out + 0x100); receiver.li(a2, 0x10);
    receiver.li(a3, 0); receiver.li(t0, R + 0x74); receiver.li(t1, 0);
    receiver.call("sceKernelReceiveMsgPipe");
    receiver.li(t0, R); receiver.put(sw(v0, 0x70, t0));
    receiver.call("sceKernelExitThread");
    Assembler other{m, 0x0880'2200};  //of main's priority
    other.print("other ran\n");
    other.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry, u32 priority) {
      main.li(a0, m.string("thread")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    auto store = [&](u32 offset) { main.li(t0, R); main.put(sw(v0, offset, t0)); };
    for(u32 work : {Held, Free}) {
      main.li(a0, work); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
      main.call("sceKernelCreateLwMutex");
    }
    for(u32 size : {0x10u, 0u}) {
      main.li(a0, m.string("pipe")); main.li(a1, 2); main.li(a2, 0); main.li(a3, size); main.li(t0, 0);
      main.call("sceKernelCreateMsgPipe");
      store(size ? 0x10 : 0x14);
    }
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, Buffer); main.li(a2, 0x10); main.li(a3, 0);
    main.li(t0, 0);
    main.call("sceKernelTrySendMsgPipe");  //the buffer full
    main.li(a0, m.string("flag")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    store(0x18);
    start(0x0880'2000, 0x30);
    start(0x0880'2100, 0x30);
    main.li(a0, 1000); main.call("sceKernelDelayThread");  //the mutex held, the receiver waiting
    start(0x0880'2200, 0x20);
    main.call("sceKernelSuspendDispatchThread");
    main.put(addu(s0, v0, zero));
    for(auto [work, offset] : {std::pair{Held, 0x40u}, {Free, 0x64u}}) {
      main.li(a0, work); main.li(a1, 1); main.li(a2, 0);
      main.call("sceKernelLockLwMutex");
      store(offset);
    }
    auto pipeCall = [&](const char* function, u32 pipe, u32 address, u32 size, u32 counted) {
      main.li(t0, pipe); main.put(lw(a0, 0, t0)); main.li(a1, address); main.li(a2, size); main.li(a3, 0);
      main.li(t0, counted); main.li(t1, 0);
      main.call(function);
    };
    pipeCall("sceKernelReceiveMsgPipe", R + 0x10, Out, 0x20, R + 0x48);
    store(0x44);
    pipeCall("sceKernelSendMsgPipe", R + 0x14, Buffer, 0x20, R + 0x50);
    store(0x4c);
    pipeCall("sceKernelSendMsgPipe", R + 0x14, Buffer, u32(-1), 0);
    store(0x54);
    for(auto [mode, offset] : {std::pair{0xffu, 0x58u}, {0x20u, 0x5cu}}) {  //a bad mode; AND, clearing
      main.li(t0, R + 0x18); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, mode); main.li(a3, 0);
      main.li(t0, 0);
      main.call("sceKernelWaitEventFlag");
      store(offset);
    }
    main.li(a0, 0);
    main.call("sceKernelRotateThreadReadyQueue");
    store(0x60);
    main.print("main rotated\n");
    main.put(addu(a0, s0, zero));
    main.call("sceKernelResumeDispatchThread");
    main.print("main resumed\n");
    main.li(a0, 1000); main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "main rotated\nmain resumed\nother ran\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R + 0x40), Kernel::ErrorCanNotWait);
    CHECK(word(m, Held) == 1 && word(m, Held + 12) == 0, true);  //held by the worker, nobody waiting
    CHECK(word(m, R + 0x64), Kernel::ErrorCanNotWait);
    CHECK(word(m, Free), 0);
    CHECK(word(m, R + 0x44) == Kernel::ErrorCanNotWait && word(m, R + 0x48) == 0x1337, true);
    CHECK(m.kernel.pipes.at(word(m, R + 0x10)).used, 0x10);
    CHECK(word(m, R + 0x4c) == Kernel::ErrorCanNotWait && word(m, R + 0x50) == 0x1337, true);
    CHECK(word(m, R + 0x70), 0x1337);  //the receiver still waits, with nothing
    bool waiting = false;
    for(auto& [uid, thread] : m.kernel.threads) {
      if(thread->wait == Kernel::Wait::PipeReceive) waiting = thread->waitDone == 0;
    }
    CHECK(waiting, true);
    CHECK(word(m, R + 0x54), Kernel::ErrorIllegalAddress);
    CHECK(word(m, R + 0x58), Kernel::ErrorIllegalMode);
    CHECK(word(m, R + 0x5c), Kernel::ErrorCanNotWait);
    CHECK(m.kernel.eventFlags.at(word(m, R + 0x18)).pattern, 1);
    CHECK(word(m, R + 0x60), 0);
    CHECK(m.kernel.dispatchSuspended, false);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//A thread waiting with callbacks for a lightweight mutex another holds runs its callback, which deletes the mutex:
//back from it, the wait ends, deleted (the wait had gone on for good, as the mutex's deleting found no thread
//waiting). The holder's unlock then finds no mutex. On both engines.
static auto lwMutexDeletedInCallback() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Work = R + 0x200;
    Assembler callback{m, 0x0880'3000};
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.li(a0, Work);
    callback.call("sceKernelDeleteLwMutex");
    callback.li(t0, R); callback.put(sw(v0, 0x68, t0));
    callback.print("callback deleted it\n");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    Assembler worker{m, 0x0880'2000};  //holds the mutex, then notifies main's callback
    worker.li(a0, Work); worker.li(a1, 1); worker.li(a2, 0);
    worker.call("sceKernelLockLwMutex");
    worker.li(a0, 2000); worker.call("sceKernelDelayThread");
    worker.li(t0, R + 0x100); worker.put(lw(a0, 0, t0)); worker.li(a1, 1);
    worker.call("sceKernelNotifyCallback");
    worker.li(a0, 2000); worker.call("sceKernelDelayThread");
    worker.li(a0, Work); worker.li(a1, 1);
    worker.call("sceKernelUnlockLwMutex");
    worker.li(t0, R); worker.put(sw(v0, 0x60, t0));
    worker.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.li(t0, R + 0x100); main.put(sw(v0, 0, t0));
    main.li(a0, Work); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x10); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //it runs at once, and holds the mutex
    main.li(a0, Work); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelLockLwMutexCB");
    main.li(t0, R); main.put(sw(v0, 0x64, t0));
    main.print("main's lock returned\n");
    main.li(a0, 3000); main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "callback deleted it\nmain's lock returned\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R + 0x68), 0);
    CHECK(word(m, R + 0x64), Kernel::ErrorWaitDeleted);
    CHECK(word(m, R + 0x60), Kernel::ErrorLwMutexNotFound);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//sceLibFont with no fonts installed: the library starts, lists none, finds and opens none (the error written where
//asked), a font's details refused; points and pixels at 128 dots an inch, then at a resolution set. Resolutions a
//state couldn't hold (0 or less, 10^9 or more, not a number) are refused, the last one kept (sceFontSetResolution
//had kept 1e10 and the infinities, and the machine's own state was refused).
static auto fontsMissing() -> void {
  KernelMachine m;
  m.system.memory.write(4, R, 0x1337);
  u32 library = m.call("sceFontNewLib", {Buffer, R});
  CHECK(library != 0 && word(m, R) == 0, true);
  CHECK(m.call("sceFontGetNumFontList", {library, R}), 0);
  CHECK(m.call("sceFontFindOptimumFont", {library, Buffer, R}), 0xffff'ffff);
  CHECK(word(m, R), Kernel::ErrorNotFound);
  m.system.memory.write(4, R, 0);
  CHECK(m.call("sceFontOpen", {library, 0, 0, R}), 0);
  CHECK(word(m, R), Kernel::ErrorNotFound);
  CHECK(m.call("sceFontGetFontInfo", {0, Buffer}), Kernel::ErrorNotFound);
  CHECK(m.call("sceFontGetCharInfo", {0, 'A', Buffer}), Kernel::ErrorNotFound);
  auto scale = [&](const char* name, float value) {
    u32 bits;
    memcpy(&bits, &value, 4);
    m.system.fpu.r[12] = bits;
    m.call(name, {library, R});
    memcpy(&value, &m.system.fpu.r[0], 4);
    return value;
  };
  CHECK(scale("sceFontPointToPixelH", 72.0f) == 128.0f, true);
  CHECK(scale("sceFontPixelToPointV", 128.0f) == 72.0f, true);
  float resolution = 144.0f;
  memcpy(&m.system.fpu.r[12], &resolution, 4);
  memcpy(&m.system.fpu.r[13], &resolution, 4);
  CHECK(m.call("sceFontSetResolution", {library}), 0);
  CHECK(scale("sceFontPointToPixelV", 72.0f) == 144.0f, true);
  auto setResolution = [&](float horizontal, float vertical) {
    memcpy(&m.system.fpu.r[12], &horizontal, 4);
    memcpy(&m.system.fpu.r[13], &vertical, 4);
    return m.call("sceFontSetResolution", {library});
  };
  float infinity = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
  for(auto [horizontal, vertical] : {std::pair{1e10f, 144.0f}, {144.0f, infinity}, {nan, 144.0f}, {144.0f, 0.0f},
                                     {-1.0f, 144.0f}, {1e9f, 144.0f}, {144.0f, -infinity}}) {
    check(__LINE__, "a resolution refused", setResolution(horizontal, vertical), Kernel::ErrorInvalidValue);
  }
  CHECK(m.kernel.fontResolution[0] == 144.0f && m.kernel.fontResolution[1] == 144.0f, true);
  CHECK(setResolution(5e8f, 72.0f), 0);
  CHECK(scale("sceFontPointToPixelV", 72.0f) == 72.0f, true);
  CHECK(m.call("sceFontClose", {0}), 0);
  CHECK(m.call("sceFontDoneLib", {library}), 0);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//A thread starts with the kernel's 256 bytes at the top of its stack zeroed (k0 points at them), the rest of a new
//stack filled with 0xff as before.
static auto kernelArea() -> void {
  KernelMachine m;
  s32 uid = m.kernel.createThread("t", 0x0880'1000, 0x20, 0x1000, 0, 0);
  auto& thread = *m.kernel.threads[uid];
  u32 top = thread.stackBlock + thread.stackSize;
  CHECK(word(m, top - 0x100), 0xffff'ffff);
  m.kernel.startThread(thread, 0, 0);
  CHECK(thread.context.gpr[26], top - 0x100);
  bool zeroed = true;
  for(u32 at = top - 0x100; at < top; at += 4) zeroed = zeroed && word(m, at) == 0;
  CHECK(zeroed, true);
  CHECK(word(m, top - 0x104) == 0xffff'ffff && word(m, thread.stackBlock + 16) == 0xffff'ffff, true);
  CHECK(roundTrip(m), true);
}

//Dates as Win32 file times, and ticks as dates, as pspautotests' rtc/convert recorded: 100-nanosecond steps since
//1601 (2005-11-31 13:01:00 and 1 microsecond, a day past November's end, is 127779156600000010; 1601-01-01 is 0);
//dates before 1601, a zeroed one among them, 0 and INVALID_VALUE; no place for the result, INVALID_VALUE, nothing
//written. A tick of 835072 is 0001-01-01 and 835072 microseconds, 62135596800000000 is 1970-01-01, and a date's tick
//(2012-09-20, a leap day, the last microsecond of 1999, the last of 9999) comes back as that date, over another.
static auto rtcFileTimesAndTicks() -> void {
  KernelMachine m;
  auto put = [&](u32 at, std::array<u32, 7> fields) {
    for(u32 n = 0; n < 6; n++) m.system.memory.write(2, at + n * 2, fields[n]);
    m.system.memory.write(4, at + 12, fields[6]);
  };
  auto date = [&](u32 at) {
    std::array<u32, 7> fields;
    for(u32 n = 0; n < 6; n++) fields[n] = m.system.memory.read(2, at + n * 2);
    fields[6] = word(m, at + 12);
    return fields;
  };
  auto wide = [&](u32 at) { return word(m, at) | u64(word(m, at + 4)) << 32; };
  auto setWide = [&](u32 at, u64 value) {
    m.system.memory.write(4, at, u32(value));
    m.system.memory.write(4, at + 4, u32(value >> 32));
  };
  auto fileTime = [&](std::array<u32, 7> fields, u32 expected, u64 time) {
    put(R, fields);
    setWide(R + 0x20, u64(-1337));
    check(__LINE__, "a file time's result", m.call("sceRtcGetWin32FileTime", {R, R + 0x20}), expected);
    check(__LINE__, "a file time", wide(R + 0x20), time);
  };
  put(R, {1600, 1, 1, 0, 0, 0, 0});
  CHECK(m.call("sceRtcGetWin32FileTime", {R, 0}), Kernel::ErrorInvalidValue);
  fileTime({0, 0, 0, 0, 0, 0, 0}, Kernel::ErrorInvalidValue, 0);
  fileTime({2005, 11, 31, 13, 1, 0, 1}, 0, 127'779'156'600'000'010ull);
  fileTime({1601, 1, 1, 0, 0, 0, 0}, 0, 0);
  fileTime({1600, 1, 1, 0, 0, 0, 0}, Kernel::ErrorInvalidValue, 0);
  fileTime({1, 1, 1, 0, 0, 0, 0}, Kernel::ErrorInvalidValue, 0);
  auto ticked = [&](u64 tick, std::array<u32, 7> expected) {
    setWide(R + 0x20, tick);
    put(R + 0x40, {2010, 9, 20, 7, 12, 15, 500});
    check(__LINE__, "a tick's result", m.call("sceRtcSetTick", {R + 0x40, R + 0x20}), 0);
    check(__LINE__, "a tick's date", date(R + 0x40) == expected, true);
  };
  ticked(835'072, {1, 1, 1, 0, 0, 0, 835'072});
  ticked(62'135'596'800'000'000ull, {1970, 1, 1, 0, 0, 0, 0});
  for(auto fields : {std::array<u32, 7>{2012, 9, 20, 7, 12, 15, 500}, {2000, 2, 29, 12, 0, 0, 0},
                     {1999, 12, 31, 23, 59, 59, 999'999}, {9999, 12, 31, 23, 59, 59, 999'999}}) {
    put(R, fields);
    CHECK(m.call("sceRtcGetTick", {R, R + 0x20}), 0);
    ticked(wide(R + 0x20), fields);
  }
  CHECK(m.call("sceRtcSetTick", {0, R + 0x20}), Kernel::ErrorInvalidPointer);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Part 28's small functions, called directly: the controller's idle thresholds (-1 to 128 each, either out of range
//refused with neither kept; read back, null places skipped, a kernel address refused), as ctrl/idle recorded; the
//GE's translation width (0, 0x200, 0x400, 0x800 or 0x1000, the one there was returned, 0x400 first), as gpu/ge/edram
//recorded; later SDKs' memory blocks (a name needed, types 0 and 1, options 4 bytes long, sizes in 256-byte steps
//from the top for type 1, freed by either function, a pointer for any block and 0 for none), as sysmem/memblock
//recorded; the HOME menu's language and button as set; the battery icon; the firmware version; microseconds as a
//system clock. A state with each changed loads into another machine, which makes the same state.
static auto oddsOfPart28() -> void {
  KernelMachine m;
  auto words = [&](u32 address) { return std::array<u32, 2>{word(m, address), word(m, address + 4)}; };
  m.system.memory.write(4, R, 0x55), m.system.memory.write(4, R + 4, 0x55);
  CHECK(m.call("sceCtrlGetIdleCancelThreshold", {R, R + 4}), 0);
  CHECK((words(R) == std::array<u32, 2>{0xffff'ffff, 0xffff'ffff}), true);
  CHECK(m.call("sceCtrlSetIdleCancelThreshold", {128, u32(-1)}), 0);
  for(auto [reset, back] : {std::pair{-2, 0}, {0, 129}, {-65535, 65536}, {1, 129}}) {
    CHECK(m.call("sceCtrlSetIdleCancelThreshold", {u32(reset), u32(back)}), Kernel::ErrorInvalidValue);
  }
  m.system.memory.write(4, R + 4, 0x55);
  CHECK(m.call("sceCtrlGetIdleCancelThreshold", {R, 0}), 0);
  CHECK((words(R) == std::array<u32, 2>{128, 0x55}), true);
  CHECK(m.call("sceCtrlGetIdleCancelThreshold", {0, R + 4}), 0);
  CHECK(word(m, R + 4), 0xffff'ffff);
  CHECK(m.call("sceCtrlGetIdleCancelThreshold", {0xdead'beef, 0xdead'beef}), Kernel::ErrorPrivilegeRequired);

  std::vector<std::pair<u32, u32>> widths = {{0, 0x400}, {0, 0}, {0x200, 0}, {0x400, 0x200}, {0x800, 0x400},
                                              {0x1000, 0x800}};
  for(auto [width, was] : widths) CHECK(m.call("sceGeEdramSetAddrTranslation", {width}), was);
  for(u32 width : {0xffff'ffffu, 1u, 0x100u, 0x1ffu, 0xc00u, 0x1800u, 0x2000u}) {
    CHECK(m.call("sceGeEdramSetAddrTranslation", {width}), Kernel::ErrorInvalidValue);
  }
  CHECK(m.kernel.geTranslation, 0x1000);

  //(the memory block functions are known by their NIDs alone: kernel.cpp's addNID)
  auto byNID = [&](u32 nid, std::initializer_list<u32> arguments) {
    u32 n = 0;
    for(u32 value : arguments) m.system.ipu.r[4 + n++] = value;
    m.kernel.syscall(m.kernel.importCode("SysMemUserForUser", nid));
    return m.system.ipu.r[2];
  };
  constexpr u32 AllocNID = 0xfe70'7fdf, FreeNID = 0x50f6'1d8a, PointerNID = 0xdb83'a952;
  u32 name = m.string("block");
  auto alloc = [&](u32 type, u32 size, u32 options = 0) { return byNID(AllocNID, {name, type, size, options}); };
  CHECK(byNID(AllocNID, {0, 1, 0x100, 0}), Kernel::ErrorError);
  for(u32 type : {0xffff'ffffu, 2u, 5u, 0x11u}) CHECK(alloc(type, 0x100), Kernel::ErrorIllegalAllocationType);
  for(u32 size : {0u, 0xffff'ffffu}) CHECK(alloc(1, size), Kernel::ErrorAllocationFailed);
  for(u32 optionsSize : {0u, 3u, 5u, 0x100u}) {
    m.system.memory.write(4, R + 8, optionsSize);
    CHECK(alloc(1, 0x100, R + 8), Kernel::ErrorIllegalArgument);
  }
  m.system.memory.write(4, R + 8, 4);
  u32 top = alloc(1, 0x100, R + 8), below = alloc(1, 0x1001), low = alloc(0, 0x10);
  CHECK(byNID(PointerNID, {top, R + 0x10}), 0);
  CHECK(byNID(PointerNID, {below, R + 0x14}), 0);
  CHECK(byNID(PointerNID, {low, R + 0x18}), 0);
  CHECK(word(m, R + 0x10) - word(m, R + 0x14), 0x1100);  //0x1001 in 256-byte steps, just below
  CHECK(word(m, R + 0x18), Kernel::UserMemory);
  CHECK(m.call("sceKernelGetBlockHeadAddr", {below}), word(m, R + 0x14));
  CHECK(m.call("sceKernelFreePartitionMemory", {top}), 0);
  CHECK(byNID(FreeNID, {below}), 0);
  CHECK(byNID(FreeNID, {below}), Kernel::ErrorUnknownUID);
  m.system.memory.write(4, R + 0x14, 0x77);
  CHECK(byNID(PointerNID, {below, R + 0x14}), 0);  //none: 0, nothing written
  CHECK(word(m, R + 0x14), 0x77);

  CHECK(m.call("sceImposeGetLanguageMode", {R + 0x20, R + 0x24}), 0);
  CHECK((words(R + 0x20) == std::array<u32, 2>{1, 1}), true);
  CHECK(m.call("sceImposeSetLanguageMode", {2, 0}), 0);
  CHECK(m.call("sceImposeGetLanguageMode", {R + 0x20, R + 0x24}), 0);
  CHECK((words(R + 0x20) == std::array<u32, 2>{2, 0}), true);
  CHECK(m.call("sceImposeGetBatteryIconStatus", {R + 0x28, R + 0x2c}), 0);
  CHECK((words(R + 0x28) == std::array<u32, 2>{0, 3}), true);
  CHECK(m.call("sceKernelDevkitVersion", {}), 0x0606'0110);
  CHECK(m.call("sceKernelUSec2SysClock", {123'456, R + 0x30}), 0);
  CHECK((words(R + 0x30) == std::array<u32, 2>{123'456, 0}), true);
  m.call("sceKernelUSec2SysClockWide", {0xffff'fffe});
  CHECK(m.system.ipu.r[2] == 0xffff'fffe && m.system.ipu.r[3] == 0, true);
  CHECK(roundTrip(m), true);
}

auto mediaTests() -> Tests {
  return {{"part 28's odds and ends", oddsOfPart28},
          {"mpeg stubs", mpegStubs}, {"mpeg movie fed and taken apart", [] { mpegMovie(false); }},
          {"mpeg movie with the game's own library", [] { mpegMovie(true); }},
          {"mpeg movie thread waits for its picture", mpegMovieThread},
          {"mpeg ringbuffer callback states", mpegCallbackStates},
          {"mpeg ringbuffer that isn't one given nothing", mpegRingbufferNotARing},
          {"mpeg ringbuffer callback with the caller's global pointer", mpegCallbackGlobalPointer},
          {"mpeg ringbuffer callback giving more than asked", mpegCallbackGivesMore},
          {"mpeg ringbuffer callback returning an error", mpegCallbackError},
          {"mpeg ringbuffer feeder terminated in its callback", mpegFeederTerminated},
          {"mpeg random packs taken apart", mpegRandomPacks},
          {"network off", networkOff},
          {"odds and ends of part 20", oddsAndEnds}, {"rtc file times and ticks", rtcFileTimesAndTicks},
          {"threads odds and ends of part 20", threadOddsAndEnds},
          {"threads dispatching held off", dispatchHeldOff},
          {"threads lightweight mutex deleted in a callback", lwMutexDeletedInCallback},
          {"fonts missing", fontsMissing}, {"threads kernel area zeroed", kernelArea}};
}

}
