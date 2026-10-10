//PGD (ares/psp/kernel/pgd.cpp): the data in a file made here the way its disc has it (pgd.hpp, the part that follows
//docs/psp-core.md, run backwards) comes back exactly, read whole or in pieces, any place and any size; MAC type 1 and
//3 both round-trip; and a wrong key, a header damaged in any byte it hashes, a file cut short, and one that isn't a
//PGD are refused, each saying why, without reading or writing outside what they hold (the sanitizers watch). The
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

//The data, decrypted whole: its bytes, or why it can't.
inline auto decryptWhole(const psp_pgd::Bytes& file, const psp_pgd::Bytes& vKey, psp_pgd::Bytes& out) -> std::string {
  Pgd::Descriptor descriptor;
  auto why = Pgd::descriptor(file.data(), file.size(), vKey.data(), descriptor);
  if(!why.empty()) { out.clear(); return why; }
  out = region(file, descriptor);
  Pgd::decryptData(out.data(), 0, u32(out.size()), descriptor.dataKey, vKey.data());
  return {};
}

//The data read at region-relative `start` for `length` bytes: the ciphertext there decrypted at its offset, the
//plaintext there. BBCipher is one keystream, so any piece decrypts on its own.
inline auto decryptPiece(const psp_pgd::Bytes& file, const Pgd::Descriptor& d, const psp_pgd::Bytes& vKey,
  u32 start, u32 length, psp_pgd::Bytes& out) -> void {
  out = psp_pgd::Bytes(file.begin() + d.dataOffset + start, file.begin() + d.dataOffset + start + length);
  Pgd::decryptData(out.data(), start, length, d.dataKey, vKey.data());
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
        decryptPiece(file, descriptor, vKey, start, length, piece);
        CHECK(piece == psp_pgd::Bytes(plain.begin() + start, plain.begin() + start + length), true);
      }
    }
  }
}

//A wrong version key fails the header's version MAC; a header damaged in any byte the two MACs hash (0x00-0x6f, the
//0x30-0x5f descriptor they cover, and the 0x60-0x6f before the 0x70 MAC), and the data key, are refused.
static auto refusals() -> void {
  auto vKey = versionKey(3);
  auto plain = plaintext(0x200, 3);
  psp_pgd::Options options;
  options.versionKey = vKey.data();
  auto file = psp_pgd::build(plain, options);

  psp_pgd::Bytes wrongKeyData;
  auto wrong = decryptWhole(file, psp_pgd::scrambled(16, 0xdead'beef), wrongKeyData);
  CHECK(!wrong.empty(), true);

  Pgd::Descriptor descriptor;
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

auto pgdTests() -> Tests {
  return {
    {"pgd whole and piece reads", roundTrip},
    {"pgd refusals", refusals},
    {"pgd BBMac core", bbmacCore},
  };
}

}
