//Makes ~PSP files as Sony's tools make them, so the decrypter's tests need no game: a program encrypted under one of
//the tags the decrypter knows, by the steps docs/psp-core.md (part 18) describes for its type, run backwards. Each
//of the decrypter's steps undoes one here; "encrypting" with KIRK's keys is what KIRK's command 4 does (AES-128-CBC,
//zero IV, the keyseed's key), and the pad of the 16-byte keys is made exactly as the decrypter makes it.
//
//It uses the core's AES-128, SHA-1 and key tables (crypto.hpp), which tests/psp/crypto.cpp checks on their own.
#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace psp_encrypt {

namespace psp = ares::PlayStationPortable;
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using Bytes = std::vector<u8>;

//What to make, and what to get wrong on purpose.
struct Options {
  u32 tag = 0xd916'05f0;
  bool secondKey = false;     //a tag with two keys (a 144-byte and a 16-byte one): the 16-byte one
  bool gzip = false;          //pack the program with gzip first (bit 0 of the header's packing)
  u32 packing = 0;            //the packing's method (bits 8-11), when gzip is set: 0 gzip, 1 2RLZ, 2 KL4E
  u32 padding = 0x80;         //what KIRK's header says comes between it and the program
  u32 dataSize = 0;           //what KIRK's header says the program's size is (0: its true size)
  u32 programSize = 0;        //what the ~PSP header says the program unpacks to (0: its true size)
  u8 signature = 0;           //type 6: the byte the signature's end at 0x10c-0x12c is filled with
  u8 typeFiveByte = 0x5a;     //type 5: the byte at 0xd4, which it leaves unchecked
  const u8* pauthKey = nullptr;  //type 5: scePauth's second key (pauth.cpp), mixed into the pad and the first step
  bool magic = true;          //the header in the clear ("~PSP", the name, the sizes); else scrambled bytes, as
                              //scePauth's data has
  std::string name = "TESTPROGRAM";
  void (*damage)(Bytes& data) = nullptr;  //changes the data (packed, if gzip) before it's encrypted
};

inline auto put32(u8* at, u32 value) -> void {
  for(u32 n = 0; n < 4; n++) at[n] = u8(value >> n * 8);
}

//Bytes that look random, the same for the same seed.
inline auto scrambled(u32 size, u32 seed) -> Bytes {
  Bytes bytes(size);
  u32 x = seed * 2654435761u + 3;
  for(auto& byte : bytes) x = x * 1103515245 + 12345, byte = u8(x >> 16);
  return bytes;
}

//KIRK command 4's way: AES-128-CBC, a zero IV, the keyseed's key.
inline auto encryptStatic(u8* data, u32 size, u8 keyseed) -> void {
  u8 iv[16] = {};
  psp::AES128{psp::Keys::kirk7[keyseed]}.encryptCBC(data, size, iv);
}

//The header's pieces as one run, and a run put back as those pieces.
struct Piece { u32 offset, size; };
inline auto gather(const u8* header, std::initializer_list<Piece> pieces) -> Bytes {
  Bytes run;
  for(auto [offset, size] : pieces) run.insert(run.end(), header + offset, header + offset + size);
  return run;
}
inline auto scatter(u8* header, std::initializer_list<Piece> pieces, const Bytes& run) -> void {
  u32 at = 0;
  for(auto [offset, size] : pieces) memcpy(header + offset, run.data() + at, size), at += size;
}

//Pieces encrypted as one run (KIRK command 4's way), then XORed with xorKey: what the decrypter's decrypt() undoes.
inline auto encryptPieces(u8* header, std::initializer_list<Piece> pieces, u8 keyseed, const u8* xorKey = nullptr)
  -> void {
  auto run = gather(header, pieces);
  encryptStatic(run.data(), run.size(), keyseed);
  if(xorKey) for(u32 n = 0; n < run.size(); n++) run[n] ^= xorKey[n % 16];
  scatter(header, pieces, run);
}

inline auto digest(const Bytes& bytes) -> Bytes {
  auto result = psp::sha1(bytes.data(), bytes.size());
  return Bytes(result.begin(), result.end());
}

//gzip (RFC 1952) through zlib: windowBits 15 + 16 asks deflate for gzip's wrapping.
inline auto gzip(const Bytes& data) -> Bytes {
  z_stream stream{};
  deflateInit2(&stream, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  Bytes packed(deflateBound(&stream, data.size()) + 64);
  stream.next_in = const_cast<u8*>(data.data());
  stream.avail_in = data.size();
  stream.next_out = packed.data();
  stream.avail_out = packed.size();
  deflate(&stream, Z_FINISH);
  packed.resize(stream.total_out);
  deflateEnd(&stream);
  return packed;
}

//The program encrypted under options.tag: the 0x150-byte ~PSP header, then KIRK command 1's data. Empty if the tag
//isn't in the decrypter's tables.
inline auto encrypt(const Bytes& program, const Options& options = {}) -> Bytes {
  const psp::PadTag* padTag = nullptr;
  const psp::SeedTag* seedTag = nullptr;
  for(auto& entry : psp::Keys::padTags) if(entry.tag == options.tag && !padTag) padTag = &entry;
  for(auto& entry : psp::Keys::seedTags) if(entry.tag == options.tag && !seedTag) seedTag = &entry;
  if(seedTag && options.secondKey) padTag = nullptr;
  if(padTag) seedTag = nullptr;
  if(!padTag && !seedTag) return {};

  //the data: the program, packed if asked; encrypted under a key of its own, a whole block at a time
  Bytes data = options.gzip ? gzip(program) : program;
  if(options.damage) options.damage(data);
  u32 size = data.size(), blocks = (size + 15) & ~15u;
  Bytes key = scrambled(16, options.tag ^ size);
  Bytes body = data;
  body.resize(blocks, 0);
  u8 iv[16] = {};
  psp::AES128{key.data()}.encryptCBC(body.data(), blocks, iv);

  //KIRK command 1's header: the data's key encrypted with command 1's key; signature bytes nothing checks; the
  //command; the sizes
  u8 kirk[0x90] = {};
  memcpy(kirk, key.data(), 16);
  psp::AES128{psp::Keys::kirk1}.encrypt(kirk);
  auto signature = scrambled(0x50, size);
  memcpy(kirk + 0x10, signature.data(), 0x50);
  put32(kirk + 0x60, 1);
  put32(kirk + 0x64, 0);
  put32(kirk + 0x68, 0);
  put32(kirk + 0x6c, 0);
  put32(kirk + 0x70, options.dataSize ? options.dataSize : size);
  put32(kirk + 0x74, options.padding);

  //the ~PSP header's parts in the clear
  u8 header[0x150] = {};
  if(options.magic) {
    memcpy(header, "~PSP", 4);
    header[6] = options.gzip;
    header[7] = options.gzip ? options.packing : 0;
    for(u32 n = 0; n < options.name.size() && n < 27; n++) header[0x0a + n] = options.name[n];
    put32(header + 0x28, options.programSize ? options.programSize : program.size());
    put32(header + 0x2c, 0x150 + blocks);
    header[0x7c] = 9;
  } else {
    auto filler = scrambled(0x80, options.tag + 2);
    memcpy(header, filler.data(), 0x80);
  }
  put32(header + 0xd0, options.tag);

  if(padTag) {
    //types 0 and 1: the header's first 0x70 bytes hidden (the decrypter's unscramble() run backwards), then stored
    //in two pieces; 0xe8-0x110 holds anything, and is hashed
    u8 pad[144];
    for(u32 n = 0; n < 144; n++) pad[n] = u8(padTag->pad[n / 4] >> n % 4 * 8);
    for(u32 n = 0; n < 0x70; n++) kirk[n] ^= pad[0x20 + n];
    encryptStatic(kirk, 0x70, padTag->keyseed);
    for(u32 n = 0; n < 0x70; n++) kirk[n] ^= pad[0x14 + n];
    memcpy(header + 0x110, kirk, 0x40);
    memcpy(header + 0x80, kirk + 0x40, 0x50);
    auto filler = scrambled(0x28, options.tag);
    memcpy(header + 0xe8, filler.data(), 0x28);
    Bytes hashed(pad, pad + 0x14);
    auto rest = gather(header, {{0xe8, 0x28}, {0x110, 0x40}, {0x80, 0x50}, {0x00, 0x80}});
    hashed.insert(hashed.end(), rest.begin(), rest.end());
    auto sum = digest(hashed);
    memcpy(header + 0xd4, sum.data(), 20);
    if(padTag->type == 1) encryptPieces(header, {{0xe0, 0x70}, {0x80, 0x30}}, padTag->keyseed);
  } else {
    //types 2, 5 and 6: the pad, as the decrypter makes it (nine copies of the key, decrypted as one)
    u8 pad[0x90];
    for(u32 n = 0; n < 9; n++) memcpy(pad + n * 16, seedTag->seed, 16), pad[n * 16] = n;
    u8 zero[16] = {};
    psp::AES128{psp::Keys::kirk7[seedTag->keyseed]}.decryptCBC(pad, sizeof(pad), zero);
    if(options.pauthKey) for(u32 n = 0; n < sizeof(pad); n++) pad[n] ^= options.pauthKey[n % 16];
    for(u32 n = 0; n < 0x40; n++) kirk[n] ^= pad[0x50 + n];
    encryptStatic(kirk, 0x40, seedTag->keyseed);
    for(u32 n = 0; n < 0x40; n++) kirk[n] ^= pad[0x10 + n];
    memcpy(header + 0x80, kirk, 0x30);
    memcpy(header + 0xc0, kirk + 0x30, 0x10);
    memcpy(header + 0xb0, kirk + 0x70, 0x10);  //its sizes, in the clear
    if(seedTag->type == 6) memset(header + 0x10c, options.signature, 0x20);
    auto id = scrambled(0x10, options.tag + 1);
    memcpy(header + 0x140, id.data(), 0x10);
    Bytes hashed = gather(header, {{0xd0, 0x04}});
    hashed.insert(hashed.end(), pad, pad + 0x10);
    auto rest = gather(header, {{0xd4, 0x58}, {0x140, 0x10}, {0x80, 0x30}, {0xc0, 0x10}, {0xb0, 0x10},
                                {0x00, 0x80}});
    hashed.insert(hashed.end(), rest.begin(), rest.end());
    auto sum = digest(hashed);
    memcpy(header + 0x12c, sum.data(), 20);
    const u8* xorKey = seedTag->type == 5 ? seedTag->xorKey : nullptr;
    encryptPieces(header, {{0x140, 0x10}, {0x12c, 0x14}, {0x80, 0x30}, {0xc0, 0x0c}}, seedTag->keyseed, xorKey);
    if(seedTag->type == 5) {
      u8 firstKey[16];
      for(u32 n = 0; n < 16; n++) firstKey[n] = xorKey[n] ^ (options.pauthKey ? options.pauthKey[n] : 0);
      encryptPieces(header, {{0x80, 0x30}, {0xc0, 0x10}, {0x12c, 0x10}}, seedTag->keyseed, firstKey);
      header[0xd4] = options.typeFiveByte;
    }
  }

  Bytes file(header, header + sizeof(header));
  file.insert(file.end(), body.begin(), body.end());
  return file;
}

}
