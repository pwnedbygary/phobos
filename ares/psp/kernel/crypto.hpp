#pragma once

#include <array>

//The PSP's cryptography, as far as loading a shop-bought game needs it.
//
//A retail game's programs come encrypted, so they run only on a PSP: Sony's kernel decrypts each with the KIRK
//engine, a crypto chip inside the PSP, before loading it. Phobos runs no Sony code, so it does the same work itself,
//with the keys published on the PSP Developer Wiki (docs/psp-core.md, part 18, describes all of it):
//  - aes.cpp: AES-128, the cipher everything here is built on;
//  - kirk.cpp: the KIRK engine's commands that decrypting a program needs, and SHA-1;
//  - keys.cpp: the keys.

namespace ares::PlayStationPortable {

//AES-128 (FIPS-197) with one key: 16-byte blocks encrypted or decrypted one at a time, or a run of them, either
//each on its own (ECB, "electronic codebook") or chained (CBC, "cipher block chaining": each block is XORed with the
//encrypted block before it, the first with an "initialization vector", IV, before it's encrypted).
struct AES128 {
  explicit AES128(const u8 key[16]);

  auto encrypt(u8 block[16]) const -> void;  //one block, in place
  auto decrypt(u8 block[16]) const -> void;
  //size is a whole number of blocks: any bytes after the last whole block are left as they are
  auto encryptECB(u8* data, u64 size) const -> void;
  auto decryptECB(u8* data, u64 size) const -> void;
  auto encryptCBC(u8* data, u64 size, const u8 iv[16]) const -> void;
  auto decryptCBC(u8* data, u64 size, const u8 iv[16]) const -> void;

private:
  struct Tables;  //the S-box and its inverse
  static auto tables() -> const Tables&;
  static auto twice(u8 value) -> u8;
  static auto shiftRows(u8 state[16]) -> void;
  static auto unshiftRows(u8 state[16]) -> void;
  static auto mixColumns(u8 state[16]) -> void;
  static auto unmixColumns(u8 state[16]) -> void;
  auto addRoundKey(u8 state[16], u32 round) const -> void;

  u8 roundKeys[11 * 16];  //the 11 round keys, one after another
};

//SHA-1 (FIPS 180-1): a 20-byte digest of any bytes. KIRK's command 0xb, and how NIDs are made (Kernel::nid()).
auto sha1(const u8* data, u64 size) -> std::array<u8, 20>;

//The KIRK engine's commands, as the PSP's kernel asks for them: an output buffer, and an input that starts with the
//command's header. Each returns 0, or KIRK's number for what was wrong.
namespace Kirk {
  enum : u32 {
    Success        = 0x00,
    InvalidMode    = 0x02,  //the header is for another command
    InvalidKeyseed = 0x0f,  //a decryption key KIRK doesn't have (or, made for each console, Phobos doesn't)
    InvalidSize    = 0x10,  //no data, or more than the buffers hold
  };
  auto decryptPrivate(u8* out, u32 outSize, const u8* in, u32 inSize) -> u32;  //command 1
  auto decryptStatic(u8* out, u32 outSize, const u8* in, u32 inSize) -> u32;   //command 7
  auto hash(u8* out, u32 outSize, const u8* in, u32 inSize) -> u32;            //command 0xb
  auto decryptInPlace(u8* data, u32 size, u8 keyseed) -> bool;  //command 7 on data without its header
}

//The keys (keys.cpp, which says where they come from).
namespace Keys {
  extern const u8 kirk1[16];       //KIRK command 1's
  extern const u8 kirk7[128][16];  //KIRK commands 4 and 7's, by keyseed
}

}
