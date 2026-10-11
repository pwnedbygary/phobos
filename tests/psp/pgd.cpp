//PGD (ares/psp/kernel/pgd.cpp): the data in a file made here the way its disc has it (pgd.hpp, the part that follows
//docs/psp-core.md, run backwards) comes back exactly, read whole or in pieces, any place and any size; MAC type 1 and
//3 both round-trip; a wrong key is recovered from the header's own MAC and reads the data all the same; a header
//damaged in any byte it hashes, a file cut short, and one that isn't a PGD are refused, each saying why, without
//reading or writing outside what they hold (the sanitizers watch). The
//BBMac core is checked against AES-CMAC's own example (FIPS-198.1), so the check isn't only that encrypting and
//decrypting here undo each other.
#include "kernel-machine.hpp"
#include "pgd.hpp"
#include "../ares/psp/kernel/pgd.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Pgd;
using ares::PlayStationPortable::AES128;
namespace Keys = ares::PlayStationPortable::Keys;

//The plaintext the tests make: bytes that aren't a pattern a block could match, so a wrong decrypt is seen.
inline auto plaintext(u32 size, u32 seed) -> psp_pgd::Bytes {
  psp_pgd::Bytes bytes(size);
  u32 x = seed * 2654435761u + 7;
  for(auto& byte : bytes) x = x * 1103515245 + 12345, byte = u8(x >> 16);
  return bytes;
}

//A version key, each test's own.
inline auto versionKey(u32 seed) -> psp_pgd::Bytes {
  return psp_pgd::scrambled(16, 0x1000'0000 | seed);
}

//A file's data region's bytes (its 0x90 header's past).
inline auto region(const psp_pgd::Bytes& file, const Pgd::Descriptor& d) -> psp_pgd::Bytes {
  return psp_pgd::Bytes(file.begin() + d.dataOffset, file.begin() + d.dataOffset + d.dataSize);
}

//The data, decrypted whole: its bytes, or why it can't. Decrypted with the version key the descriptor reports, which
//is what the kernel reads it back with (io.cpp), not necessarily the key handed in.
inline auto decryptWhole(const psp_pgd::Bytes& file, const psp_pgd::Bytes& vKey, psp_pgd::Bytes& out) -> std::string {
  Pgd::Descriptor descriptor;
  auto why = Pgd::descriptor(file.data(), file.size(), vKey.data(), descriptor);
  if(!why.empty()) { out.clear(); return why; }
  out = region(file, descriptor);
  Pgd::decryptData(out.data(), 0, u32(out.size()), descriptor.dataKey, descriptor.versionKey);
  return {};
}

//The data read at region-relative `start` for `length` bytes: the ciphertext there decrypted at its offset, the
//plaintext there. BBCipher is one keystream, so any piece decrypts on its own. The descriptor's own version key is used,
//which is what the kernel decrypts the data with.
inline auto decryptPiece(const psp_pgd::Bytes& file, const Pgd::Descriptor& d,
  u32 start, u32 length, psp_pgd::Bytes& out) -> void {
  out = psp_pgd::Bytes(file.begin() + d.dataOffset + start, file.begin() + d.dataOffset + start + length);
  Pgd::decryptData(out.data(), start, length, d.dataKey, d.versionKey);
}

//The data, made with each MAC type, comes back exactly read whole and in every piece tested: whole, and each 16-byte
//aligned and off-by-one boundary, any start and size that begin and end inside the data.
static auto roundTrip() -> void {
  for(u32 keyIndex : {0u, 2u}) {
    auto vKey = versionKey(keyIndex);
    psp_pgd::Options options;
    options.keyIndex = keyIndex;
    options.versionKey = vKey.data();
    auto plain = plaintext(0x1234, keyIndex);
    auto file = psp_pgd::build(plain, options);
    CHECK(file.size(), 0x90 + plain.size());

    psp_pgd::Bytes data;
    CHECK(decryptWhole(file, vKey, data).empty(), true);
    CHECK(data == plain, true);

    Pgd::Descriptor descriptor;
    CHECK(Pgd::descriptor(file.data(), file.size(), vKey.data(), descriptor).empty(), true);
    CHECK(descriptor.dataSize, plain.size());
    CHECK(descriptor.dataOffset, 0x90);

    for(u32 start : {0u, 1u, 0xfu, 0x10u, 0x11u, 0xffu, 0x100u, 0x1200u}) {
      for(u32 length : {0u, 1u, 0xfu, 0x10u, 0x11u, 0x100u, 0x200u}) {
        if(start + length > plain.size()) continue;
        psp_pgd::Bytes piece;
        decryptPiece(file, descriptor, start, length, piece);
        CHECK(piece == psp_pgd::Bytes(plain.begin() + start, plain.begin() + start + length), true);
      }
    }
  }
}

//A version key that doesn't match the header's MAC is not refused: the key is recovered out of that MAC (amctrl's
//bbmac_getkey), so the file still unlocks and its data still comes back exactly, and the descriptor reports the key it
//was read under so the data is decrypted with the same one. What IS refused: a header damaged in any byte the DNAS
//MAC hashes (0x00-0x6f, the 0x30-0x5f descriptor they cover, and the 0x60-0x6f before the 0x70 MAC), one cut short,
//and one that isn't a PGD at all.
static auto refusals() -> void {
  auto vKey = versionKey(3);
  auto plain = plaintext(0x200, 3);
  psp_pgd::Options options;
  options.versionKey = vKey.data();
  auto file = psp_pgd::build(plain, options);

  //a wrong key: recovered from the MAC, so the descriptor comes out and the data decrypts exactly.
  auto wrongKey = psp_pgd::scrambled(16, 0xdead'beef);
  psp_pgd::Bytes wrongKeyData;
  auto wrong = decryptWhole(file, wrongKey, wrongKeyData);
  CHECK(wrong.empty(), true);
  CHECK(wrongKeyData == plain, true);

  //the key it reports is the one the data goes with, and it is the file's own, not the wrong one handed in.
  Pgd::Descriptor descriptor;
  CHECK(Pgd::descriptor(file.data(), file.size(), wrongKey.data(), descriptor).empty(), true);
  CHECK(memcmp(descriptor.versionKey, vKey.data(), 16), 0);
  //with the right key handed in, the same key is reported (nothing was recovered in that case).
  CHECK(Pgd::descriptor(file.data(), file.size(), vKey.data(), descriptor).empty(), true);
  CHECK(memcmp(descriptor.versionKey, vKey.data(), 16), 0);

  for(u32 at : {0u, 4u, 0x10u, 0x20u, 0x30u, 0x40u, 0x60u, 0x6fu}) {
    auto damaged = file;
    damaged[at] ^= 0x01;
    CHECK(!Pgd::descriptor(damaged.data(), damaged.size(), vKey.data(), descriptor).empty(), true);
  }

  //a file cut short in its header, and one that isn't a PGD at all: each refused.
  CHECK(!Pgd::descriptor(file.data(), 0x8f, vKey.data(), descriptor).empty(), true);
  auto notPgd = file;
  notPgd[0] = 'X';
  CHECK(!Pgd::descriptor(notPgd.data(), notPgd.size(), vKey.data(), descriptor).empty(), true);
  CHECK(Pgd::is(notPgd.data()), false);
  CHECK(Pgd::is(file.data()), true);
}

//AES-128-CMAC (FIPS-198.1) built on the core's FIPS-tested AES-128, the way BBMac is, to check its core against a
//standard's own example.
inline auto aesCmac(const u8 key[16], const u8* message, u64 len) -> psp_pgd::Block {
  AES128 aes{key};
  psp_pgd::Block L{};
  aes.encrypt(L.data());
  psp_pgd::Block k1 = psp_pgd::cmacDoubled(L);
  psp_pgd::Block k2 = psp_pgd::cmacDoubled(k1);
  u64 nblocks = len ? (len + 15) / 16 : 1;
  u64 last = nblocks - 1;
  //The last block, finalized (padded and XORed with K2, or the complete block XORed with K1), kept in its own
  //variable: the loop below copies each message block into a separate one, so the finalized block must not share
  //that slot or it would be overwritten before the final block is processed.
  psp_pgd::Block lastBlock{};
  if(len == 0 || len % 16) {
    u64 tail = len % 16;
    memcpy(lastBlock.data(), message + last * 16, tail);
    lastBlock[tail] = 0x80;
    for(u32 n = 0; n < 16; n++) lastBlock[n] ^= k2[n];
  } else {
    memcpy(lastBlock.data(), message + last * 16, 16);
    for(u32 n = 0; n < 16; n++) lastBlock[n] ^= k1[n];
  }
  //CBC-MAC over every block, the last already finalized in lastBlock.
  psp_pgd::Block state{};
  for(u64 i = 0; i < nblocks; i++) {
    psp_pgd::Block block = (i == last) ? lastBlock : psp_pgd::Block{};
    if(i != last) memcpy(block.data(), message + i * 16, 16);
    for(u32 n = 0; n < 16; n++) state[n] ^= block[n];
    aes.encrypt(state.data());
  }
  return state;
}

//BBMac's AES-CMAC core matches the standard's examples (its key, message and MAC), so the cipher there is right, not
//only self-consistent here.
static auto bbmacCore() -> void {
  auto nistKey = psp_pgd::bytes("2b7e151628aed2a6abf7158809cf4f3c");
  auto nistMsg = psp_pgd::bytes("6bc1bee22e409f96e93d7e117393172a");
  auto nistMac = psp_pgd::hexBlock("070a16b46b4d4144f79bdd9dd04a287c");
  CHECK(aesCmac(nistKey.data(), nistMsg.data(), nistMsg.size()) == nistMac, true);

  auto longMsg = psp_pgd::bytes(
    "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411"
    "e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
  auto longMac = psp_pgd::hexBlock("51f0bebf7e3b9d92fc49741779363cfe");
  CHECK(aesCmac(nistKey.data(), longMsg.data(), longMsg.size()) == longMac, true);

  //the fourty-byte example (two full blocks then a partial one, so its last block is padded and XORed with K2):
  CHECK(aesCmac(nistKey.data(),
    psp_pgd::bytes("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411").data(),
    psp_pgd::bytes("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411").size())
    == psp_pgd::hexBlock("dfa66747de9ae63030ca32611497c827"), true);

  //BBMac is that CMAC (with KIRK's key 0x38) XORed with the whitening constant (no extra key): its wrapper, checked
  //against the same core, so both the cipher and the whitening are right, not only self-consistent here.
  auto direct = psp_pgd::bbmac(nistMsg.data(), nistMsg.size(), nullptr);
  psp_pgd::Block expected{};
  for(u32 n = 0; n < 16; n++) expected[n] = u8(direct[n] ^ psp_pgd::kMacWhitening[n]);
  CHECK(aesCmac(Keys::kirk7[0x38], nistMsg.data(), nistMsg.size()) == expected, true);
}

//BBCipher's keystream chains on the counter block it is given, not on the keystream it makes: block s is
//AES_0x63(counter(base,s)) XOR counter(base,s-1), with counter(base,0) as the first block's IV. A round trip only
//shows the decrypter undoes whatever encryptor ran beside it, so this pins the convention to bytes worked out from
//amctrl.prx's own routine (tpunix's sub_428 feeding KIRK's cipher-block-chaining under key 0x63) and checked against
//a real game's decrypted PGD data region. Chaining on the keystream instead (what this once did) agrees on the first
//block, whose IV is 0, and differs from the second on: a two-block vector catches it, and three are used here.
static auto bbcipherVector() -> void {
  auto key   = psp_pgd::bytes("000102030405060708090a0b0c0d0e0f");
  auto plain = psp_pgd::bytes(
    "030a11181f262d343b424950575e656c"
    "737a81888f969da4abb2b9c0c7ced5dc"
    "e3eaf1f8ff060d141b222930373e454c");
  auto expect = psp_pgd::bytes(
    "ec8fdde83b0b63fa7f4aab91949c3591"
    "188a9ba085c39dcf2a3161f3b1043379"
    "5e50dad78df2d23f3a5b6d90453022ac");
  //the data key XORed with the version key is the one BBCipher key: the version key all zero leaves the data key.
  u8 zeroVersionKey[16] = {};
  auto data = plain;
  Pgd::decryptData(data.data(), 0, u32(data.size()), key.data(), zeroVersionKey);
  CHECK(data == expect, true);

  //the second block alone, and the same bytes read at an offset, so the chain's start is checked too.
  auto piece = psp_pgd::Bytes(plain.begin() + 16, plain.end());
  Pgd::decryptData(piece.data(), 16, u32(piece.size()), key.data(), zeroVersionKey);
  CHECK(piece == psp_pgd::Bytes(expect.begin() + 16, expect.end()), true);
}

//The descriptor says where the data starts and how long it is, and that is checked against the WHOLE file, not the
//0x90-byte header that was read to find it. Handed the header's length instead, every real file is refused for
//holding more data than its own header, which is how a UMD data file (186 MB of it) came to be turned away. The file
//here is much bigger than its header on purpose, and both sizes are checked: the whole file comes back its descriptor,
//and a size of only the header's refuses it.
static auto descriptorBoundsUseTheFile() -> void {
  auto vKey = versionKey(9);
  auto plain = plaintext(0x8000, 9);  //32 KB of data under a 0x90-byte header
  psp_pgd::Options options;
  options.versionKey = vKey.data();
  options.dataSize = u32(plain.size());
  options.blockSize = 0x400;
  auto file = psp_pgd::build(plain, options);
  CHECK(file.size(), 0x90 + plain.size());

  Pgd::Descriptor descriptor;
  CHECK(Pgd::descriptor(file.data(), file.size(), vKey.data(), descriptor).empty(), true);
  CHECK(descriptor.dataSize, plain.size());
  CHECK(descriptor.dataOffset, 0x90);
  CHECK(descriptor.blockSize, 0x400);

  //the size of only the header, which is what the caller used to hand over: not the file, so refused.
  CHECK(!Pgd::descriptor(file.data(), 0x90, vKey.data(), descriptor).empty(), true);

  //and a descriptor claiming more data than the file holds is still refused.
  psp_pgd::Options overOptions;
  overOptions.versionKey = vKey.data();
  overOptions.dataSize = u32(plain.size());
  overOptions.blockSize = 0x400;
  auto overFile = psp_pgd::build(plain, overOptions);
  overFile.resize(0x90 + plain.size() / 2);  //the file, cut short of what its descriptor says
  CHECK(!Pgd::descriptor(overFile.data(), overFile.size(), vKey.data(), descriptor).empty(), true);
}

auto pgdTests() -> Tests {
  return {
    {"pgd whole and piece reads", roundTrip},
    {"pgd refusals", refusals},
    {"pgd BBMac core", bbmacCore},
    {"pgd BBCipher keystream", bbcipherVector},
    {"pgd descriptor bounds use the file", descriptorBoundsUseTheFile},
  };
}

}
