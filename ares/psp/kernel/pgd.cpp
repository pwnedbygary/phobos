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
  //BBCipher is one keystream K over the whole data region: K(0)=0, and K(s)=AES_0x63(counter(base,s)) XOR K(s-1). The
  //data block at region index b uses K(b+1). The block just before the data starts is region index offset/16, so its
  //keystream K(offset/16) seeds the chain; the piece is then taken from that stream, at its place within the block.
  u32 firstBlock = offset / 16;
  Block previous = Block{};  //K(0)
  for(u32 s = 1; s <= firstBlock; s++) {  //K(1)..K(offset/16): the chain up to the block before the data
    Block c = counterBlock(base, s);
    k63.decrypt(c.data());
    for(u32 n = 0; n < 16; n++) c[n] ^= previous[n];
    previous = c;
  }
  u32 at = 0, s = firstBlock, within = offset % 16;
  while(at < size) {
    Block current = counterBlock(base, s + 1);  //K(s+1) for the data block at region index s
    k63.decrypt(current.data());
    for(u32 n = 0; n < 16; n++) current[n] ^= previous[n];
    u32 take = size - at < 16 ? size - at : 16;
    u32 shift = at ? 0 : within;  //the first block is entered partway; the rest from its start
    if(take > 16 - shift) take = 16 - shift;  //stay within the first block
    for(u32 n = 0; n < take; n++) data[at + n] ^= current[n + shift];
    previous = current;
    s++;
    at += take;
  }
}

auto Pgd::descriptor(const u8* file, u64 size, const u8 versionKey[16], Descriptor& out) -> std::string {
  if(size < 0x90) return "a PGD file cut short in its header";

  //MAC type 3 (key index over 1) stores its MACs encrypted with KIRK's key 0x63, before they're compared.
  bool encryptedMacs = le32(file + 4) > 1;
  auto storedMac = [&](u32 at) -> Block {
    Block mac;
    memcpy(mac.data(), file + at, 16);
    if(encryptedMacs) AES128{Keys::kirk7[0x63]}.decrypt(mac.data());
    return mac;
  };

  //The header's two MACs: the file with the DNAS key over its first 0x80 bytes, and with the version key over its
  //first 0x70. Either failing says the header isn't what its keys say, or the key is wrong.
  if(bbmac(file, 0x80, kDnasKey) != storedMac(0x80)) return "a PGD file whose header doesn't match its DNAS MAC";
  if(bbmac(file, 0x70, versionKey) != storedMac(0x70)) return "a PGD file whose header doesn't match its version MAC";

  //The descriptor (0x30 bytes at 0x30), BBCiphered with the 0x10 key XORed with the version key.
  u8 descriptor[0x30];
  memcpy(descriptor, file + 0x30, sizeof(descriptor));
  decryptData(descriptor, 0, sizeof(descriptor), file + 0x10, versionKey);

  memcpy(out.dataKey, descriptor, 16);
  out.dataSize = le32(descriptor + 0x14);
  out.blockSize = le32(descriptor + 0x18);
  out.dataOffset = le32(descriptor + 0x1c);
  if(out.blockSize == 0 || out.dataOffset > size || u64(out.dataSize) > size - out.dataOffset) {
    return "a PGD file whose descriptor says its data is somewhere it isn't";
  }
  return {};
}
