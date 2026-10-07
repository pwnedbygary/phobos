//Movies made up for the tests (movies.cpp, psmf.cpp): H.264 pictures whose every macroblock is I_PCM (its samples
//stored as they are, so what it decodes to is known to the value), access units of bare slice headers (what
//mpeg.cpp's keyframe() reads), and PSMF movies of them: a header with its stream table and EP maps, as part 31 of
//docs/psp-core.md describes it, then an MPEG-2 program stream of packs.
#pragma once

#include "kernel-machine.hpp"

namespace allegrex_test::psp {

//H.264's bits: unsigned and signed Exp-Golomb codes, plain bits, a NAL unit's emulation prevention.
struct Bits {
  std::vector<u8> bytes;
  u32 bit = 0;
  auto put(u32 value, u32 count) -> void {
    while(count--) {
      if(bit % 8 == 0) bytes.push_back(0);
      if(value >> count & 1) bytes.back() |= 0x80 >> bit % 8;
      bit++;
    }
  }
  auto ue(u32 value) -> void {
    u32 length = 0;
    while((value + 1) >> (length + 1)) length++;
    put(0, length);
    put(value + 1, length + 1);
  }
  auto se(s32 value) -> void { ue(value > 0 ? 2 * value - 1 : -2 * value); }
  auto align() -> void { while(bit % 8) put(0, 1); }
  auto trailing() -> void { put(1, 1); align(); }
};

inline auto nal(u32 type, const std::vector<u8>& payload) -> std::vector<u8> {
  std::vector<u8> out = {0, 0, 0, 1, u8(3 << 5 | type)};
  u32 zeros = 0;
  for(u8 byte : payload) {
    if(zeros >= 2 && byte <= 3) out.push_back(3), zeros = 0;
    out.push_back(byte);
    zeros = byte ? 0 : zeros + 1;
  }
  return out;
}

//A picture of width by height macroblocks, each of one colour (Y, Cb, Cr), as an access unit: delimiter, sequence and
//picture parameter sets, and an IDR slice of I_PCM macroblocks (deblocking off).
struct Colour { u8 y, cb, cr; };
inline auto picture(u32 width, u32 height, const std::vector<Colour>& colours, u32 idr) -> std::vector<u8> {
  std::vector<u8> unit = {0, 0, 0, 1, 0x09, 0x10};  //access unit delimiter: an I picture
  Bits sps;
  sps.put(66, 8); sps.put(0xc0, 8); sps.put(30, 8);  //baseline, constraint sets 0 and 1, level 3
  sps.ue(0); sps.ue(0); sps.ue(2); sps.ue(1); sps.put(0, 1);
  sps.ue(width - 1); sps.ue(height - 1); sps.put(1, 1); sps.put(1, 1); sps.put(0, 1); sps.put(0, 1);
  sps.trailing();
  Bits pps;
  pps.ue(0); pps.ue(0); pps.put(0, 1); pps.put(0, 1); pps.ue(0); pps.ue(0); pps.ue(0); pps.put(0, 1);
  pps.put(0, 2); pps.se(0); pps.se(0); pps.se(0); pps.put(1, 1); pps.put(0, 1); pps.put(0, 1);
  pps.trailing();
  Bits slice;
  slice.ue(0); slice.ue(7); slice.ue(0); slice.put(0, 4); slice.ue(idr);
  slice.put(0, 1); slice.put(0, 1);  //no output of prior pictures; not long-term
  slice.se(0); slice.ue(1);          //QP delta; deblocking off
  for(u32 mb = 0; mb < width * height; mb++) {
    slice.ue(25);  //I_PCM
    slice.align();
    auto c = colours[mb % colours.size()];
    for(u32 n = 0; n < 256; n++) slice.put(c.y, 8);
    for(u32 n = 0; n < 64; n++) slice.put(c.cb, 8);
    for(u32 n = 0; n < 64; n++) slice.put(c.cr, 8);
  }
  slice.trailing();
  for(auto& part : {nal(7, sps.bytes), nal(8, pps.bytes), nal(5, slice.bytes)}) {
    unit.insert(unit.end(), part.begin(), part.end());
  }
  return unit;
}

//An access unit of slices of these types (H.264's slice_type: 2 or 7 an I slice, 5 a P one; in NAL units of type
//5, IDR, or 1), each just its header's first numbers (its first macroblock first): what keyframe() reads.
inline auto slices(u32 type, const std::vector<u32>& kinds, u32 firstMacroblock = 0) -> std::vector<u8> {
  std::vector<u8> unit = {0, 0, 0, 1, 0x09, 0x10};
  for(u32 kind : kinds) {
    Bits slice;
    slice.ue(firstMacroblock); slice.ue(kind); slice.ue(0);
    slice.trailing();
    auto part = nal(type, slice.bytes);
    unit.insert(unit.end(), part.begin(), part.end());
  }
  return unit;
}

//A PSMF movie made up (psmf.cpp): its streams, each a PES stream ID (0xe0 plus a channel for video, 0xbd for
//sound) and private ID, a video stream's picture size and the units its EP map points at; video stream 0's access
//units (stream 1's, if there is one, the same), and ATRAC3plus frames for the sound streams (376 bytes, each filled
//with 0x40 plus its number, behind its 8-byte header), the first stamped soundStart.
struct MovieStream {
  u8 id = 0xe0, privateID = 0;
  u32 width = 0, height = 0;
  std::vector<u32> entries;  //the units the EP map points at (video)
};

inline auto videoStream(u8 id, u32 width, u32 height, std::vector<u32> entries = {}) -> MovieStream {
  MovieStream stream;
  stream.id = id, stream.width = width, stream.height = height, stream.entries = std::move(entries);
  return stream;
}

inline auto soundStream(u8 privateID) -> MovieStream {
  MovieStream stream;
  stream.id = 0xbd, stream.privateID = privateID;
  return stream;
}

struct Movie {
  std::vector<MovieStream> streams;
  std::vector<std::vector<u8>> units;
  u32 soundFrames = 0;
  u64 start = 90000;
  s64 soundStart = 90000;
  const char* version = "0015";

  //The bytes: a 2048-byte header (above), then a pack for each 2000 bytes of an access unit (its first PES packet
  //stamped with the unit's time, start + n * 3003) and a pack for each sound frame (its PES packet's data the 4-byte
  //header, the private ID first, then the frame; stamped soundStart + n * 4180), in time order.
  auto bytes() const -> std::vector<u8> {
    std::vector<u8> stream;
    std::vector<u32> unitPacks;
    auto pack = [&](u8 id, const std::vector<u8>& data, s64 time, bool stamped) {
      std::vector<u8> p = {0, 0, 1, 0xba, 0x44, 0, 4, 0, 4, 1, 1, 0x89, 0xc3, 0xf8};
      u32 header = stamped ? 5 : 0, length = 3 + header + data.size();
      for(u32 byte : {0u, 0u, 1u, u32(id), length >> 8, length & 0xff, 0x81u, stamped ? 0x80u : 0u, header}) {
        p.push_back(byte);
      }
      if(stamped) {
        u64 t = time;
        for(u64 byte : {0x21 | (t >> 29 & 0x0e), t >> 22 & 0xff, (t >> 14 & 0xfe) | 1, t >> 7 & 0xff,
                        (t << 1 & 0xfe) | 1}) {
          p.push_back(byte);
        }
      }
      p.insert(p.end(), data.begin(), data.end());
      u32 padding = 2048 - p.size() - 6;
      for(u32 byte : {0u, 0u, 1u, 0xbeu, padding >> 8, padding & 0xff}) p.push_back(byte);
      p.resize(2048, 0xff);
      stream.insert(stream.end(), p.begin(), p.end());
    };
    std::vector<u8> sounds;
    for(auto& s : streams) if(s.id == 0xbd && s.privateID < 0x10) sounds.push_back(s.privateID);
    bool second = std::count_if(streams.begin(), streams.end(), [](auto& s) { return (s.id & 0xf0) == 0xe0; }) > 1;
    u32 unit = 0, frame = 0;
    while(unit < units.size() || (!sounds.empty() && frame < soundFrames)) {
      s64 unitTime = start + unit * 3003, frameTime = soundStart + s64(frame) * 4180;
      if(unit < units.size() && (sounds.empty() || frame >= soundFrames || unitTime <= frameTime)) {
        unitPacks.push_back(stream.size() / 2048);
        for(u8 id : {u8(0xe0), u8(0xe1)}) {
          if(id == 0xe1 && !second) continue;
          auto& data = units[unit];
          for(u32 from = 0; from < data.size(); from += 2000) {
            pack(id, {data.begin() + from, data.begin() + std::min<u32>(data.size(), from + 2000)}, unitTime, !from);
          }
        }
        unit++;
      } else {
        for(u8 sub : sounds) {
          std::vector<u8> data = {sub, 0, 0, 0, 0x0f, 0xd0, 0x28, 0x2e, 0, 0, 0, 0};  //376-byte stereo frames
          data.resize(12 + 376, 0x40 + frame + sub * 0x20);
          pack(0xbd, data, frameTime, true);
        }
        frame++;
      }
    }
    std::vector<u8> out(2048, 0);
    auto put = [&](u32 at, u64 value, u32 count) {
      for(u32 n = 0; n < count; n++) out[at + n] = value >> (count - 1 - n) * 8;
    };
    memcpy(out.data(), "PSMF", 4);
    memcpy(out.data() + 4, version, 4);
    u32 count = streams.size();
    u64 end = start + units.size() * 3003;
    put(8, 0x800, 4); put(12, stream.size(), 4);
    put(0x50, 0x2e + 16 * count, 4); put(0x54, start, 6); put(0x5a, end, 6); put(0x60, 25000, 4);
    put(0x64, 90000, 4); out[0x68] = count, out[0x69] = 1;
    put(0x6a, 0x14 + 16 * count, 4); put(0x6e, start, 6); put(0x74, end, 6); out[0x7b] = 1;
    put(0x7c, 2 + 16 * count, 4); put(0x80, count, 2);
    u32 map = 0x82 + 16 * count;
    for(u32 n = 0; n < count; n++) {
      auto& s = streams[n];
      u32 at = 0x82 + 16 * n;
      bool video = (s.id & 0xf0) == 0xe0;
      out[at] = s.id, out[at + 1] = s.privateID;
      put(at + 2, video ? 0x20fb : 0x2004, 2);
      if(!s.entries.empty()) put(at + 4, map, 4), put(at + 8, s.entries.size(), 4);
      out[at + 12] = s.width / 16, out[at + 13] = s.height / 16;
      if(!video) out[at + 14] = 2, out[at + 15] = 2;
      for(u32 entry : s.entries) {
        out[map] = 0xc0;
        put(map + 2, start + entry * 3003, 4);
        put(map + 6, entry < unitPacks.size() ? unitPacks[entry] : 0, 4);
        map += 10;
      }
    }
    out.insert(out.end(), stream.begin(), stream.end());
    return out;
  }
};

}
