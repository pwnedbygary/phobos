//Makes PGD files (ares/psp/kernel/pgd.cpp) the way a game's disc has them, so the decrypter's tests need no game: a
//file's data, descriptor and header MACs are made by running the decrypter's steps backwards (docs/psp-core.md, the
//part that follows PGD, set out from the PSP Developer Wiki and "tpu"'s and Hykem's reverse engineering of amctrl.prx).
//Every step here undoes one there; "encrypting" the data and descriptor with BBCipher is the same as decrypting it,
//BBCipher being a keystream; and the MACs' extra lock (MAC type 3) is KIRK command 4's way (AES-128, one block).
//
//It uses the core's AES-128 and KIRK's keys (crypto.hpp), which tests/psp/crypto.cpp checks on its own. The header's
//amctrl constants (kDnasKey, kMacWhitening, kCipherIn, kCipherOut) are "tpu"'s, on the wiki (Hkem's KEY3, KEY4,
//KEY5); the KIRK keys (0x38, 0x39, 0x63) are Keys::kirk7's, the same values the reference uses.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace psp_pgd {

namespace psp = ares::PlayStationPortable;
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using Bytes = std::vector<u8>;
using Block = std::array<u8, 16>;

//What to make.
struct Options {
  u32 keyIndex = 0;  //MAC type 1 (0) or 3 (over 1): whether the header's MACs are encrypted with KIRK's key 0x63
  u32 dataSize = 0;  //the descriptor's: the data's size (0: the plaintext's own size)
  u32 blockSize = 0x4000;  //the descriptor's: the decrypting chunk size
  u32 dataOffset = 0x90;  //the descriptor's: where the data starts (after the header)
  const u8* dataKey = nullptr;  //the data's AES key (its own, if null: a deterministic one)
  const u8* versionKey = nullptr;  //the version key (the game's sceIoIoctl one; its own, if null)
  const u8* headerKey = nullptr;  //the header's 0x10 key, in the clear (its own, if null)
  void (*dataPattern)(u8*, u32) = nullptr;  //how the plaintext's bytes are made, if not 0, 1, 2, 3...
};

//Bytes written as hexadecimal digits, two to a byte, a NIST CMAC vector's like.
inline auto bytes(const char* text) -> Bytes {
  Bytes result;
  auto digit = [](char c) -> u32 { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for(; text[0] && text[1]; text += 2) result.push_back(digit(text[0]) << 4 | digit(text[1]));
  return result;
}

//A NIST CMAC vector's 16 bytes, as a block, from its hexadecimal digits.
inline auto hexBlock(const char* text) -> Block {
  Block block{};
  auto digit = [](char c) -> u32 { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for(u32 n = 0; n < 16 && text[2 * n] && text[2 * n + 1]; n++) block[n] = u8(digit(text[2 * n]) << 4 | digit(text[2 * n + 1]));
  return block;
}

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

//amctrl's constants.
inline const u8 kKirk38[16] = {0x12, 0x46, 0x8d, 0x7e, 0x1c, 0x42, 0x20, 0x9b, 0xba, 0x54, 0x26, 0x83, 0x5e, 0xb0, 0x33, 0x03};
inline const u8 kKirk39[16] = {0xc4, 0x3b, 0xb6, 0xd6, 0x53, 0xee, 0x67, 0x49, 0x3e, 0xa9, 0x5f, 0xbc, 0x0c, 0xed, 0x6f, 0x8a};
inline const u8 kKirk63[16] = {0x9c, 0x9b, 0x13, 0x72, 0xf8, 0xc6, 0x40, 0xcf, 0x1c, 0x62, 0xf5, 0xd5, 0x92, 0xdd, 0xb5, 0x82};
inline const u8 kDnasKey[16]   = {0xed, 0xe2, 0x5d, 0x2d, 0xbb, 0xf8, 0x12, 0xe5, 0x3c, 0x5c, 0x59, 0x32, 0xfa, 0xe3, 0xe2, 0x43};
inline const u8 kMacWhitening[16] = {0xe3, 0x50, 0xed, 0x1d, 0x91, 0x0a, 0x1f, 0xd0, 0x29, 0xbb, 0x1c, 0x3e, 0xf3, 0x40, 0x77, 0xfb};
inline const u8 kCipherIn[16]  = {0x67, 0x8d, 0x7f, 0xa3, 0x2a, 0x9c, 0xa0, 0xd1, 0x50, 0x8a, 0xd8, 0x38, 0x5e, 0x4b, 0x01, 0x7e};
inline const u8 kCipherOut[16] = {0x13, 0x5f, 0xa4, 0x7c, 0xab, 0x39, 0x5b, 0xa4, 0x76, 0xb8, 0xcc, 0xa9, 0x8f, 0x3a, 0x04, 0x45};

//AES-CMAC's subkey: the cipher's block doubled once in GF(2^128).
inline auto cmacDoubled(Block block) -> Block {
  bool carry = block[0] & 0x80;
  for(u32 n = 0; n < 15; n++) block[n] = u8(block[n] << 1 | block[n + 1] >> 7);
  block[15] = u8(block[15] << 1 ^ (carry ? 0x87 : 0));
  return block;
}

//BBMac: AES-CMAC (KIRK's key 0x38) of the data, XORed with a constant, and (when a key is given) XORed with it and
//encrypted once more. The same as the decrypter's, the one step the reference sets out.
inline auto bbmac(const u8* data, u64 size, const u8* key) -> Block {
  psp::AES128 aes{psp::Keys::kirk7[0x38]};
  Block state{}, last{};
  u64 tail = size % 16;
  if(tail == 0 && size) tail = 16;
  for(u64 at = 0; at + tail < size; at += 16) {
    for(u32 n = 0; n < 16; n++) state[n] ^= data[at + n];
    aes.encrypt(state.data());
  }
  Block subkey{};
  aes.encrypt(subkey.data());
  subkey = cmacDoubled(subkey);
  memcpy(last.data(), data + size - tail, tail);
  if(tail < 16) { subkey = cmacDoubled(subkey); last[tail] = 0x80; }
  for(u32 n = 0; n < 16; n++) state[n] ^= last[n] ^ subkey[n];
  aes.encrypt(state.data());
  for(u32 n = 0; n < 16; n++) state[n] ^= kMacWhitening[n];
  if(key) {
    for(u32 n = 0; n < 16; n++) state[n] ^= key[n];
    aes.encrypt(state.data());
  }
  return state;
}

//BBCipher's keystream base from a key, and its counter block n.
inline auto cipherBase(const u8* key) -> Block {
  Block block;
  for(u32 n = 0; n < 16; n++) block[n] = u8(key[n] ^ kCipherIn[n]);
  psp::AES128{psp::Keys::kirk7[0x39]}.decrypt(block.data());
  for(u32 n = 0; n < 16; n++) block[n] ^= kCipherOut[n];
  return block;
}
inline auto counterBlock(const Block& base, u32 n) -> Block {
  Block block = base;
  for(u32 i = 0; i < 4; i++) block[12 + i] = u8(n >> i * 8);
  return block;
}

//`size` bytes, BBCiphered one way (the data's, the descriptor's): a keystream, so this both encrypts and decrypts.
inline auto bbcipher(const u8* key, u32 seed, u8* data, u32 size) -> void {
  auto base = cipherBase(key);
  psp::AES128 k63{psp::Keys::kirk7[0x63]};
  u32 s = seed;
  Block previous = seed == 1 ? Block{} : counterBlock(base, seed - 1);
  u32 at = 0;
  while(at < size) {
    Block current = counterBlock(base, s);
    k63.decrypt(current.data());
    for(u32 n = 0; n < 16; n++) current[n] ^= previous[n];
    u32 take = size - at < 16 ? size - at : 16;
    for(u32 n = 0; n < take; n++) data[at + n] ^= current[n];
    previous = current;
    s++;
    at += take;
  }
}

//The data key, the version key and the header key, each its option or a deterministic one.
inline auto dataKeyOf(const Options& o) -> Bytes {
  if(o.dataKey) { Bytes b(16); memcpy(b.data(), o.dataKey, 16); return b; }
  return scrambled(16, 0x5000'0000);
}
inline auto versionKeyOf(const Options& o) -> Bytes {
  if(o.versionKey) { Bytes b(16); memcpy(b.data(), o.versionKey, 16); return b; }
  return scrambled(16, 0x7000'0000);
}
inline auto headerKeyOf(const Options& o) -> Bytes {
  if(o.headerKey) { Bytes b(16); memcpy(b.data(), o.headerKey, 16); return b; }
  return scrambled(16, 0x9000'0000);
}

//A PGD file (the data given) with options. Empty if it can't be made (the data past the end of a 0xffff'0000 file).
inline auto build(const Bytes& plaintext, const Options& options = {}) -> Bytes {
  if(options.dataSize && options.dataSize != plaintext.size()) return {};
  const u32 dataSize = options.dataSize ? options.dataSize : u32(plaintext.size());
  const Bytes dKey = dataKeyOf(options);
  const Bytes vKey = versionKeyOf(options);
  const Bytes hKey = headerKeyOf(options);

  //the descriptor: the data's key, its size, the chunk, where it starts, then zeros; BBCiphered with the header key.
  u8 descriptor[0x30] = {};
  memcpy(descriptor, dKey.data(), 16);
  put32(descriptor + 0x14, dataSize);
  put32(descriptor + 0x18, options.blockSize);
  put32(descriptor + 0x1c, options.dataOffset);
  u8 descKey[16];
  for(u32 n = 0; n < 16; n++) descKey[n] = u8(hKey[n] ^ vKey[n]);
  bbcipher(descKey, 1, descriptor, sizeof(descriptor));

  //the data, BBCiphered with the data key XORed with the version key.
  Bytes data = plaintext;
  u8 dKeyVKey[16];
  for(u32 n = 0; n < 16; n++) dKeyVKey[n] = u8(dKey[n] ^ vKey[n]);
  bbcipher(dKeyVKey, 1, data.data(), u32(data.size()));

  //the header: the magic, the key index (MAC type), the mode, the header key, the descriptor, then the MACs over its
  //first 0x70 and 0x80 bytes (the MAC-3 ones encrypted with KIRK's key 0x63). The 0x70 MAC is made first: the 0x80
  //one reads its bytes.
  Bytes header(0x90, 0);
  header[0] = 0, header[1] = 'P', header[2] = 'G', header[3] = 'D';
  put32(header.data() + 4, options.keyIndex);
  put32(header.data() + 8, 1);
  memcpy(header.data() + 0x10, hKey.data(), 16);
  memcpy(header.data() + 0x30, descriptor, 0x30);

  auto mac = [&](u32 at, u32 over, const u8* key) {
    Block raw = bbmac(header.data(), over, key);
    if(options.keyIndex > 1) psp::AES128{psp::Keys::kirk7[0x63]}.encrypt(raw.data());
    memcpy(header.data() + at, raw.data(), 16);
  };
  mac(0x70, 0x70, vKey.data());
  mac(0x80, 0x80, kDnasKey);

  Bytes file(header);
  file.insert(file.end(), data.begin(), data.end());
  return file;
}

}
