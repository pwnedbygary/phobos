//PGD (Protected Game Data), in which a game's UMD data files and its NPDRM EDATA files keep their data encrypted:
//ares/psp/kernel/pgd.hpp. The PSP decrypts them as they're read, in amctrl.prx's two routines (BBMac and BBCipher),
//which this follows. Written from the PSP Developer Wiki's "PGD" page and Hykem's and "tpu"'s reverse engineering of
//amctrl.prx (the emunewz "PGD Decryption" thread), as the reference documents, and set down here in our own words;
//nothing is copied from PPSSPP or JPCSP.
//
//Both routines are AES-128 (ares's aes.cpp) with three keys from KIRK's vault (kirk7's 0x38, 0x39 and 0x63, which are
//the same values the reference uses), and the header's other constants are amctrl's own: kDnasKey, kMacWhitening,
//kCipherIn and kCipherOut, "tpu"'s, on the wiki (Hkem's KEY3, KEY4 and KEY5).

#include "pgd.hpp"
#include "crypto.hpp"

#include <cstring>

namespace {

//A 16-byte block, as AES sees it.
using Block = std::array<u8, 16>;

//A little-endian word.
static auto le32(const u8* at) -> u32 {
  return at[0] | at[1] << 8 | u32(at[2]) << 16 | u32(at[3]) << 24;
}

//amctrl's own constants (kirk7's three keys come from Keys:: below).
static const u8 kDnasKey[16]    = {0xed, 0xe2, 0x5d, 0x2d, 0xbb, 0xf8, 0x12, 0xe5, 0x3c, 0x5c, 0x59, 0x32, 0xfa, 0xe3, 0xe2, 0x43};
static const u8 kMacWhitening[16] = {0xe3, 0x50, 0xed, 0x1d, 0x91, 0x0a, 0x1f, 0xd0, 0x29, 0xbb, 0x1c, 0x3e, 0xf3, 0x40, 0x77, 0xfb};
static const u8 kCipherIn[16]   = {0x67, 0x8d, 0x7f, 0xa3, 0x2a, 0x9c, 0xa0, 0xd1, 0x50, 0x8a, 0xd8, 0x38, 0x5e, 0x4b, 0x01, 0x7e};
static const u8 kCipherOut[16]  = {0x13, 0x5f, 0xa4, 0x7c, 0xab, 0x39, 0x5b, 0xa4, 0x76, 0xb8, 0xcc, 0xa9, 0x8f, 0x3a, 0x04, 0x45};

//AES-CMAC's subkey: the cipher's block doubled once in GF(2^128) (a bit left, 0x87 where one falls off).
static auto cmacDoubled(Block block) -> Block {
  bool carry = block[0] & 0x80;
  for(u32 n = 0; n < 15; n++) block[n] = u8(block[n] << 1 | block[n + 1] >> 7);
  block[15] = u8(block[15] << 1 ^ (carry ? 0x87 : 0));
  return block;
}

//BBMac: AES-CMAC (KIRK's key 0x38) of the data, then XORed with a constant, and, when a key is given, XORed with it
//and encrypted once more. That extra step is what ties the header's MACs to the game (the DNAS key) or to the version
//key the game gave.
static auto bbmac(const u8* data, u64 size, const u8* key) -> Block {
  AES128 aes{Keys::kirk7[0x38]};
  Block state{}, last{};
  u64 tail = size % 16;
  if(tail == 0 && size) tail = 16;  //a whole number of blocks still keeps its own last block
  for(u64 at = 0; at + tail < size; at += 16) {
    for(u32 n = 0; n < 16; n++) state[n] ^= data[at + n];
    aes.encrypt(state.data());
  }
  Block subkey{};
  aes.encrypt(subkey.data());
  subkey = cmacDoubled(subkey);
  memcpy(last.data(), data + size - tail, tail);
  if(tail < 16) { subkey = cmacDoubled(subkey); last[tail] = 0x80; }  //the short block, padded as CMAC asks
  for(u32 n = 0; n < 16; n++) state[n] ^= last[n] ^ subkey[n];
  aes.encrypt(state.data());
  for(u32 n = 0; n < 16; n++) state[n] ^= kMacWhitening[n];
  if(key) {
    for(u32 n = 0; n < 16; n++) state[n] ^= key[n];
    aes.encrypt(state.data());
  }
  return state;
}

//BBCipher's keystream base from a key: XORed with kCipherIn, decrypted with KIRK's key 0x39, XORed with kCipherOut.
static auto cipherBase(const u8* key) -> Block {
  Block block;
  for(u32 n = 0; n < 16; n++) block[n] = u8(key[n] ^ kCipherIn[n]);
  AES128{Keys::kirk7[0x39]}.decrypt(block.data());
  for(u32 n = 0; n < 16; n++) block[n] ^= kCipherOut[n];
  return block;
}

//BBCipher's counter block n: the base with its bytes 12-15 set to n, little-endian.
static auto counterBlock(const Block& base, u32 n) -> Block {
  Block block = base;
  for(u32 i = 0; i < 4; i++) block[12 + i] = u8(n >> i * 8);
  return block;
}

//The version key recovered from the header's 0x70 MAC (amctrl's bbmac_getkey). That MAC is the BBMac over the first
//0x70 bytes with the version key XORed in and encrypted once more, so the same BBMac run with no key, put against
//the stored MAC undone, gives the version key back. Possible only because the key enters as one XOR and one AES.
static auto versionKeyFromMac(const u8* file, const Block& storedMac) -> Block {
  Block key, mac = bbmac(file, 0x70, nullptr), encrypted = storedMac;
  AES128{Keys::kirk7[0x38]}.decrypt(encrypted.data());  //undo the MAC's last encrypt
  for(u32 n = 0; n < 16; n++) key[n] = u8(mac[n] ^ encrypted[n]);
  return key;
}

//A MAC the header holds at `at`. MAC type 3 (key index over 1) stores its MACs encrypted with KIRK's key 0x63, so
//they're decrypted before they're compared.
static auto storedMac(const u8* file, u32 at) -> Block {
  Block mac;
  memcpy(mac.data(), file + at, 16);
  if(le32(file + 4) > 1) AES128{Keys::kirk7[0x63]}.decrypt(mac.data());
  return mac;
}

}

auto Pgd::intact(const u8* file) -> bool {
  return bbmac(file, 0x80, kDnasKey) == storedMac(file, 0x80);
}

auto Pgd::keyMatches(const u8* file, const u8 versionKey[16]) -> bool {
  return bbmac(file, 0x70, versionKey) == storedMac(file, 0x70);
}

//The data key XORed with the version key, one BBCipher key, is how both the descriptor and the data are keyed.
static auto bbcipherKey(const u8 dataKey[16], const u8 versionKey[16]) -> Block {
  Block key;
  for(u32 n = 0; n < 16; n++) key[n] = u8(dataKey[n] ^ versionKey[n]);
  return key;
}

auto Pgd::decryptData(u8* data, u32 offset, u32 size, const u8 dataKey[16], const u8 versionKey[16]) -> void {
  if(size == 0) return;
  auto base = cipherBase(bbcipherKey(dataKey, versionKey).data());
  AES128 k63{Keys::kirk7[0x63]};
  //BBCipher builds one keystream over the whole data region out of the counter blocks: each is CBC-decrypted with
  //KIRK's key 0x63, and KIRK's cipher-block-chaining chains on the INPUT block, not on the keystream it produces.
  //So K(s) = AES_0x63(counter(base,s)) XOR counter(base,s-1), counting counter(base,0) as 0. The data block at
  //region index b uses K(b+1). The block the data starts in is region index offset/16, so the keystream for it
  //chains on counter(base, offset/16); the piece is taken from there, at its place within the block.
  u32 firstBlock = offset / 16;
  Block previous = firstBlock ? counterBlock(base, firstBlock) : Block{};  //counter(base,0)
  u32 at = 0, s = firstBlock, within = offset % 16;
  while(at < size) {
    Block input = counterBlock(base, s + 1);  //the counter for the data block at region index s
    Block stream = input;
    k63.decrypt(stream.data());
    for(u32 n = 0; n < 16; n++) stream[n] ^= previous[n];
    u32 take = size - at < 16 ? size - at : 16;
    u32 shift = at ? 0 : within;  //the first block is entered partway; the rest from its start
    if(take > 16 - shift) take = 16 - shift;  //stay within the first block
    for(u32 n = 0; n < take; n++) data[at + n] ^= stream[n + shift];
    previous = input;  //the next block chains on this counter, not on the keystream
    s++;
    at += take;
  }
}

auto Pgd::descriptor(const u8* file, u64 size, const u8 versionKey[16], Descriptor& out) -> std::string {
  if(size < 0x90) return "a PGD file cut short in its header";

  //The header's two MACs: the file with the DNAS key over its first 0x80 bytes, and with the version key over its
  //first 0x70. The first failing says the header isn't what its keys say. For the second, a key that doesn't match
  //this file's MAC comes back out of the MAC itself, so a header that checks against the recovered key still unlocks:
  //what an NPDRM EDATA readied with its licensee key needs (io.cpp's sceNpDrmEdataSetupKey: that key isn't its PGD's
  //version key). A game's own key is checked before it gets here (io.cpp's pgdUnlock(), keyMatches()), as the PSP's
  //PGD layer refuses any other. Which key was used is reported, because the data has to be decrypted with the same one.
  if(!intact(file)) return "a PGD file whose header doesn't match its DNAS MAC";
  Block key;
  memcpy(key.data(), versionKey, 16);
  if(!keyMatches(file, versionKey)) key = versionKeyFromMac(file, storedMac(file, 0x70));
  memcpy(out.versionKey, key.data(), 16);

  //The descriptor (0x30 bytes at 0x30), BBCiphered with the 0x10 key XORed with the version key.
  u8 descriptor[0x30];
  memcpy(descriptor, file + 0x30, sizeof(descriptor));
  decryptData(descriptor, 0, sizeof(descriptor), file + 0x10, key.data());

  memcpy(out.dataKey, descriptor, 16);
  out.dataSize = le32(descriptor + 0x14);
  out.blockSize = le32(descriptor + 0x18);
  out.dataOffset = le32(descriptor + 0x1c);
  if(out.blockSize == 0 || out.dataOffset > size || u64(out.dataSize) > size - out.dataOffset) {
    return "a PGD file whose descriptor says its data is somewhere it isn't";
  }
  return {};
}
