//The PSP scene's other compressed disc images, made for the tests from an ISO's bytes (disc-image.hpp makes the ISO
//and the CSO): CSO version 2 (each block deflated, LZ4-packed or stored), ZSO (LZ4), DAX (zlib, 8 KiB frames, with
//areas left uncompressed), JSO (LZO or zlib), and CHD (MAME's hunks of data: uncompressed here, which libchdr reads
//with no codec at all).
//
//The LZ4 and LZO packers are small and plain: a greedy search for 4-byte repeats, written to each format's rules
//(LZ4's block format; LZO1X as Linux's Documentation/staging/lzo.rst describes it), so that any reader reads what
//they write. They needn't pack well: they only have to use the instructions a reader must follow.
#pragma once

#include "disc-image.hpp"

namespace disc_image {

//Bytes that end where the disc does, padded with zeros to a whole block (as the packers pad the last one).
inline auto blockOf(const std::vector<std::uint8_t>& iso, std::uint32_t number, std::uint32_t blockSize) {
  std::vector<std::uint8_t> whole(blockSize, 0);
  std::uint64_t start = std::uint64_t(number) * blockSize;
  if(start < iso.size()) {
    std::memcpy(whole.data(), iso.data() + start, std::min<std::uint64_t>(blockSize, iso.size() - start));
  }
  return whole;
}

//Deflate: raw (CSO's, windowBits -15) or wrapped in zlib's 2-byte header and checksum (DAX's and JSO's, 15).
inline auto deflateBlock(const std::vector<std::uint8_t>& in, int windowBits) {
  std::vector<std::uint8_t> packed(compressBound(in.size()));
  z_stream z{};
  deflateInit2(&z, 9, Z_DEFLATED, windowBits, 9, Z_DEFAULT_STRATEGY);
  z.next_in = (Bytef*)in.data();
  z.avail_in = in.size();
  z.next_out = packed.data();
  z.avail_out = packed.size();
  deflate(&z, Z_FINISH);
  packed.resize(z.total_out);
  deflateEnd(&z);
  return packed;
}

//Where each 4-byte run was last seen, for the packers' search.
struct Repeats {
  std::vector<std::int64_t> table = std::vector<std::int64_t>(1 << 16, -1);
  auto seen(const std::vector<std::uint8_t>& in, std::size_t at) -> std::int64_t {
    std::uint32_t word;
    std::memcpy(&word, &in[at], 4);
    auto& slot = table[(word * 2654435761u) >> 16];
    auto before = slot;
    slot = at;
    return before;
  }
};

//LZ4: sequences of a token (literals' count, match's length less 4), the literals, then the match's 2-byte distance.
//The format's rules for the end: the last match starts 12 or more bytes before it, and the last 5 bytes are literals.
inline auto packLZ4(const std::vector<std::uint8_t>& in) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> out;
  Repeats repeats;
  std::size_t at = 0, anchor = 0, size = in.size();
  auto more = [&](std::size_t n) {  //a count past 15, in bytes of up to 255
    while(n >= 255) out.push_back(255), n -= 255;
    out.push_back(n);
  };
  auto sequence = [&](std::size_t literals, std::size_t length, std::size_t distance) {
    out.push_back(std::min<std::size_t>(literals, 15) << 4 | (length ? std::min<std::size_t>(length - 4, 15) : 0));
    if(literals >= 15) more(literals - 15);
    out.insert(out.end(), in.begin() + anchor, in.begin() + anchor + literals);
    if(!length) return;
    out.push_back(distance);
    out.push_back(distance >> 8);
    if(length - 4 >= 15) more(length - 4 - 15);
  };
  while(at + 12 <= size) {
    std::int64_t from = repeats.seen(in, at);
    if(from < 0 || at - from > 65535 || std::memcmp(&in[from], &in[at], 4)) { at++; continue; }
    std::size_t length = 4;
    while(at + length + 5 < size && in[from + length] == in[at + length]) length++;
    sequence(at - anchor, length, at - from);
    at += length;
    anchor = at;
  }
  sequence(size - anchor, 0, 0);
  return out;
}

//LZO1X: literal runs and matches. A match up to 16 KiB back is 001LLLLL, then 2 bytes holding the distance less 1
//and, in their low two bits, the 0 to 3 literals after it; one 16 to 48 KiB back is 0001HLLL, the same 2 bytes
//holding the distance less 16384 (its 15th bit is H). 4 or more literals after a match (or first) are a run of their
//own, 0000LLLL. The first instruction may instead be a byte of 17 plus a count of literals. Lengths whose bits are 0
//go on in bytes: each 0 adds 255, and the first that isn't adds itself. 0x11 0x00 0x00 ends the stream.
inline auto packLZO(const std::vector<std::uint8_t>& in) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> out;
  Repeats repeats;
  std::size_t at = 0, anchor = 0, size = in.size(), last = SIZE_MAX;  //last: where the last match's literal bits are
  auto more = [&](std::size_t n) {
    while(n > 255) out.push_back(0), n -= 255;
    out.push_back(n);
  };
  auto literals = [&](std::size_t start, std::size_t count) {
    if(!count) return;
    if(out.empty() && count <= 238) {
      out.push_back(17 + count);
    } else if(count <= 3) {
      out[last] |= count;  //a match came before (literals never follow literals), and says how many follow it
    } else if(count - 3 <= 15) {
      out.push_back(count - 3);
    } else {
      out.push_back(0);
      more(count - 3 - 15);
    }
    out.insert(out.end(), in.begin() + start, in.begin() + start + count);
  };
  auto match = [&](std::size_t length, std::size_t distance) {
    bool far = distance > 16384;
    std::size_t bits = far ? 7 : 31, coded = far ? distance - 16384 : distance - 1;
    std::uint8_t code = far ? 16 | (coded >> 14 & 1) << 3 : 32;
    if(length - 2 <= bits) out.push_back(code | (length - 2));
    else out.push_back(code), more(length - 2 - bits);
    last = out.size();
    out.push_back((coded & 0x3fff) << 2);
    out.push_back(coded >> 6);
  };
  while(at + 4 <= size) {
    std::int64_t from = repeats.seen(in, at);
    if(from < 0 || at - from > 49151 || std::memcmp(&in[from], &in[at], 4)) { at++; continue; }
    std::size_t length = 4;
    while(at + length < size && in[from + length] == in[at + length]) length++;
    literals(anchor, at - anchor);
    match(length, at - from);
    at += length;
    anchor = at;
  }
  literals(anchor, size - anchor);
  out.insert(out.end(), {0x11, 0x00, 0x00});
  return out;
}

//A CSO version 2: each block deflated (raw) or LZ4-packed (the index entry's top bit), whichever is smaller (or LZ4
//for every block whose number is odd, so both are there); stored as it is when neither, padded, is smaller than a
//block. With an alignment, each block starts at a multiple of 2^alignment, the index holding its start shifted down
//by that, and the gap before it filled with `pad`, as maxcso allows (any byte: NUL, or ziso's 'X').
inline auto makeCso2(const std::vector<std::uint8_t>& iso, std::uint32_t blockSize = 2048,
                     std::uint32_t alignment = 0, std::uint8_t pad = 0) -> std::vector<std::uint8_t> {
  std::uint32_t blocks = (iso.size() + blockSize - 1) / blockSize, unit = 1u << alignment;
  std::vector<std::uint8_t> cso(24 + 4 * (blocks + 1), 0);
  std::memcpy(cso.data(), "CISO", 4);
  put32(cso.data() + 4, 24);
  put32(cso.data() + 8, std::uint32_t(iso.size()));
  put32(cso.data() + 12, std::uint32_t(std::uint64_t(iso.size()) >> 32));
  put32(cso.data() + 16, blockSize);
  cso[20] = 2;
  cso[21] = alignment;
  auto align = [&] { while(cso.size() % unit) cso.push_back(pad); };
  for(std::uint32_t n = 0; n < blocks; n++) {
    align();
    auto whole = blockOf(iso, n, blockSize);
    auto deflated = deflateBlock(whole, -15), lz4 = packLZ4(whole);
    bool useLz4 = n & 1 || lz4.size() < deflated.size();
    auto& packed = useLz4 ? lz4 : deflated;
    bool stored = (packed.size() + unit - 1) / unit * unit >= blockSize;
    std::uint32_t entry = std::uint32_t(cso.size() >> alignment) | (!stored && useLz4 ? 0x8000'0000 : 0);
    if(stored) cso.insert(cso.end(), whole.begin(), whole.end());
    else cso.insert(cso.end(), packed.begin(), packed.end());
    put32(cso.data() + 24 + 4 * n, entry);
  }
  align();
  put32(cso.data() + 24 + 4 * blocks, std::uint32_t(cso.size() >> alignment));
  return cso;
}

//A ZSO: a CSO version 1's layout ("ZISO"), each block LZ4-packed, or stored as it is (the top bit) when that's
//smaller; aligned and padded as makeCso2's.
inline auto makeZso(const std::vector<std::uint8_t>& iso, std::uint32_t blockSize = 2048, std::uint32_t alignment = 0,
                    std::uint8_t pad = 0) -> std::vector<std::uint8_t> {
  std::uint32_t blocks = (iso.size() + blockSize - 1) / blockSize, unit = 1u << alignment;
  std::vector<std::uint8_t> zso(24 + 4 * (blocks + 1), 0);
  std::memcpy(zso.data(), "ZISO", 4);
  put32(zso.data() + 4, 24);
  put32(zso.data() + 8, std::uint32_t(iso.size()));
  put32(zso.data() + 12, std::uint32_t(std::uint64_t(iso.size()) >> 32));
  put32(zso.data() + 16, blockSize);
  zso[20] = 1;
  zso[21] = alignment;
  auto align = [&] { while(zso.size() % unit) zso.push_back(pad); };
  for(std::uint32_t n = 0; n < blocks; n++) {
    align();
    auto whole = blockOf(iso, n, blockSize);
    auto packed = packLZ4(whole);
    std::uint32_t entry = std::uint32_t(zso.size() >> alignment);
    if(packed.size() >= blockSize) {
      entry |= 0x8000'0000;
      zso.insert(zso.end(), whole.begin(), whole.end());
    } else {
      zso.insert(zso.end(), packed.begin(), packed.end());
    }
    put32(zso.data() + 24 + 4 * n, entry);
  }
  align();
  put32(zso.data() + 24 + 4 * blocks, std::uint32_t(zso.size() >> alignment));
  return zso;
}

//A DAX (version 1): a 32-byte header ("DAX\0", the disc's size, the version, how many uncompressed areas, then 16
//bytes unused), then each 8 KiB frame's offset (32 bits) and size (16 bits), then the uncompressed areas (each its
//first frame and how many frames), then the frames: zlib-wrapped deflate, or as they are inside an uncompressed area.
using Areas = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
inline auto makeDax(const std::vector<std::uint8_t>& iso, const Areas& areas = {}) -> std::vector<std::uint8_t> {
  constexpr std::uint32_t Frame = 0x2000;
  std::uint32_t frames = (iso.size() + Frame - 1) / Frame;
  std::vector<std::uint8_t> dax(32 + 6 * frames + 8 * areas.size(), 0);
  std::memcpy(dax.data(), "DAX\0", 4);
  put32(dax.data() + 4, iso.size());
  put32(dax.data() + 8, 1);
  put32(dax.data() + 12, areas.size());
  for(std::size_t n = 0; n < areas.size(); n++) {
    put32(dax.data() + 32 + 6 * frames + 8 * n, areas[n].first);
    put32(dax.data() + 32 + 6 * frames + 8 * n + 4, areas[n].second);
  }
  for(std::uint32_t n = 0; n < frames; n++) {
    auto whole = blockOf(iso, n, Frame);
    bool plain = std::any_of(areas.begin(), areas.end(), [&](auto& a) {
      return n >= a.first && n - a.first < a.second;
    });
    auto packed = plain ? whole : deflateBlock(whole, 15);
    put32(dax.data() + 32 + 4 * n, dax.size());
    put16(dax.data() + 32 + 4 * frames + 2 * n, packed.size());
    dax.insert(dax.end(), packed.begin(), packed.end());
  }
  return dax;
}

//A JSO: a 48-byte header ("JISO", 3, 1, the block size (16 bits), no block headers, 0, the method (0 LZO, 1 zlib), 0,
//the disc's size, 16 bytes of MD5 (left 0 here), the header's size, then 12 bytes unused), then each block's offset
//and one for the end, then the blocks: packed, or stored as they are when packing doesn't make them smaller (a block
//that takes exactly the block size is stored). Some tools write zlib's blocks as raw deflate (raw), and some store a
//short last block as it is, at its own length (shortLast).
inline auto makeJso(const std::vector<std::uint8_t>& iso, std::uint32_t blockSize, bool lzo, bool raw = false,
                    bool shortLast = false) -> std::vector<std::uint8_t> {
  std::uint32_t blocks = (iso.size() + blockSize - 1) / blockSize;
  std::vector<std::uint8_t> jso(48 + 4 * (blocks + 1), 0);
  std::memcpy(jso.data(), "JISO", 4);
  jso[4] = 3;
  jso[5] = 1;
  put16(jso.data() + 6, blockSize);
  jso[10] = lzo ? 0 : 1;
  put32(jso.data() + 12, iso.size());
  put32(jso.data() + 32, 48);
  for(std::uint32_t n = 0; n < blocks; n++) {
    auto whole = blockOf(iso, n, blockSize);
    auto packed = lzo ? packLZO(whole) : deflateBlock(whole, raw ? -15 : 15);
    put32(jso.data() + 48 + 4 * n, jso.size());
    std::uint64_t rest = iso.size() - std::uint64_t(n) * blockSize;
    if(shortLast && rest < blockSize) jso.insert(jso.end(), whole.begin(), whole.begin() + rest);
    else if(packed.size() >= blockSize) jso.insert(jso.end(), whole.begin(), whole.end());
    else jso.insert(jso.end(), packed.begin(), packed.end());
  }
  put32(jso.data() + 48 + 4 * blocks, jso.size());
  return jso;
}

//A CHD (version 5), uncompressed: a 124-byte header (big-endian: "MComprHD", its length, the version, four codecs
//(none), the disc's size, where the map and the metadata are, the hunk size, the unit size, then three SHA-1 sums,
//left 0 here, which libchdr doesn't check in a version 5 header), the map (each hunk's place in the file, in hunks,
//or 0 for a hunk of zeros), then the hunks, each at a multiple of the hunk size. unitBytes 2448 makes a CD's.
inline auto makeChd(const std::vector<std::uint8_t>& iso, std::uint32_t hunkBytes = 2048,
                    std::uint32_t unitBytes = 2048) -> std::vector<std::uint8_t> {
  auto big32 = [](std::uint8_t* p, std::uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; };
  auto big64 = [&](std::uint8_t* p, std::uint64_t v) { big32(p, v >> 32); big32(p + 4, v); };
  std::uint32_t hunks = (iso.size() + hunkBytes - 1) / hunkBytes;
  std::vector<std::uint8_t> chd(124 + 4 * hunks, 0);
  std::memcpy(chd.data(), "MComprHD", 8);
  big32(chd.data() + 8, 124);
  big32(chd.data() + 12, 5);
  big64(chd.data() + 32, iso.size());
  big64(chd.data() + 40, 124);
  big32(chd.data() + 56, hunkBytes);
  big32(chd.data() + 60, unitBytes);
  while(chd.size() % hunkBytes) chd.push_back(0);
  for(std::uint32_t n = 0; n < hunks; n++) {
    auto whole = blockOf(iso, n, hunkBytes);
    std::uint32_t place = 0;
    if(std::any_of(whole.begin(), whole.end(), [](std::uint8_t b) { return b; })) {
      place = chd.size() / hunkBytes;
      chd.insert(chd.end(), whole.begin(), whole.end());
    }
    big32(chd.data() + 124 + 4 * n, place);
  }
  return chd;
}

}
