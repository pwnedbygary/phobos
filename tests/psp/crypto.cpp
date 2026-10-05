//The PSP's cryptography (ares/psp/kernel/crypto.hpp): AES-128 and SHA-1 against the standards' own examples, and
//the KIRK engine's commands 1, 7 and 0xb on data encrypted here the way the PSP Developer Wiki says KIRK's other
//commands (and Sony's tools) encrypt it.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::AES128;
namespace Kirk = ares::PlayStationPortable::Kirk;
namespace Keys = ares::PlayStationPortable::Keys;

//Bytes written as hexadecimal digits, two to a byte.
static auto bytes(const char* text) -> std::vector<u8> {
  std::vector<u8> result;
  auto digit = [](char c) -> u32 { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for(; text[0] && text[1]; text += 2) result.push_back(digit(text[0]) << 4 | digit(text[1]));
  return result;
}

//Bytes that look random, the same for the same seed.
static auto scrambled(u32 size, u32 seed) -> std::vector<u8> {
  std::vector<u8> result(size);
  u32 x = seed * 2654435761u + 7;
  for(auto& byte : result) x = x * 1103515245 + 12345, byte = u8(x >> 16);
  return result;
}

static auto put32(std::vector<u8>& data, u32 at, u32 value) -> void {
  for(u32 n = 0; n < 4; n++) data[at + n] = u8(value >> n * 8);
}

//What KIRK's command 4 makes (the wiki's "Commands 4/7"): data encrypted with AES-128-CBC, a zero IV and the key of
//the keyseed, behind command 7's header (mode 5, the keyseed, the size).
static auto staticInput(const std::vector<u8>& data, u32 keyseed) -> std::vector<u8> {
  std::vector<u8> input(0x14 + data.size(), 0);
  put32(input, 0, 5);
  input[0x0c] = keyseed;
  put32(input, 0x10, data.size());
  std::copy(data.begin(), data.end(), input.begin() + 0x14);
  u8 iv[16] = {};
  AES128{Keys::kirk7[keyseed & 0x7f]}.encryptCBC(input.data() + 0x14, data.size(), iv);
  return input;
}

//A command 1 input, as Sony's tools make one (the wiki's "Commands 1 & 3"): data encrypted with AES-128-CBC and a
//zero IV under a key of its own, padded out to a whole block, after padding bytes and a header holding that key
//encrypted with command 1's key, the command (1), the signature's kind, the data's size and the padding's.
static auto privateInput(const std::vector<u8>& data, u32 padding, bool ecdsa) -> std::vector<u8> {
  auto key = scrambled(16, data.size() + padding);
  u32 blocks = (data.size() + 15) & ~15u;
  std::vector<u8> input(0x90 + padding + blocks, 0x5a);
  std::fill(input.begin(), input.begin() + 0x90, 0);
  std::copy(key.begin(), key.end(), input.begin());
  AES128{Keys::kirk1}.encrypt(input.data());
  put32(input, 0x60, 1);
  put32(input, 0x64, ecdsa);
  put32(input, 0x70, data.size());
  put32(input, 0x74, padding);
  u8* body = input.data() + 0x90 + padding;
  std::copy(data.begin(), data.end(), body);
  std::fill(body + data.size(), body + blocks, 0);
  u8 iv[16] = {};
  AES128{key.data()}.encryptCBC(body, blocks, iv);
  return input;
}

//AES-128 on the examples of FIPS-197 (appendix B, and C.1) and of NIST SP 800-38A (F.1.1 and F.2.1: four blocks with
//ECB and with CBC), each encrypted and decrypted back; a thousand blocks chained, whose last encrypted block OpenSSL
//gave; and the bytes after the last whole block left alone.
static auto aes() -> void {
  struct Example { const char* key; const char* plain; const char* encrypted; };
  for(auto [key, plain, encrypted] : {
    Example{"2b7e151628aed2a6abf7158809cf4f3c", "3243f6a8885a308d313198a2e0370734",
            "3925841d02dc09fbdc118597196a0b32"},
    Example{"000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff",
            "69c4e0d86a7b0430d8cdb78070b4c55a"},
  }) {
    AES128 aes{bytes(key).data()};
    auto block = bytes(plain);
    aes.encrypt(block.data());
    CHECK(block == bytes(encrypted), true);
    aes.decrypt(block.data());
    CHECK(block == bytes(plain), true);
  }

  AES128 aes{bytes("2b7e151628aed2a6abf7158809cf4f3c").data()};
  auto plain = bytes("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
                     "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
  auto data = plain;
  aes.encryptECB(data.data(), data.size());
  CHECK(data == bytes("3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf"
                      "43b1cd7f598ece23881b00e3ed0306887b0c785e27e8ad3f8223207104725dd4"), true);
  aes.decryptECB(data.data(), data.size());
  CHECK(data == plain, true);
  auto iv = bytes("000102030405060708090a0b0c0d0e0f");
  aes.encryptCBC(data.data(), data.size(), iv.data());
  CHECK(data == bytes("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
                      "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7"), true);
  aes.decryptCBC(data.data(), data.size(), iv.data());
  CHECK(data == plain, true);

  //chained with a zero IV, each block of zeros encrypts the block before it again: the last is the first block
  //encrypted a thousand times over
  AES128 counting{bytes("000102030405060708090a0b0c0d0e0f").data()};
  std::vector<u8> zeros(16'000), chain = zeros;
  u8 zeroIV[16] = {};
  counting.encryptCBC(chain.data(), chain.size(), zeroIV);
  CHECK(std::vector<u8>(chain.end() - 16, chain.end()) == bytes("1fd09ae87c7258990cc56156460ff206"), true);
  counting.decryptCBC(chain.data(), chain.size(), zeroIV);
  CHECK(chain == zeros, true);

  //a partial block at the end: untouched, either way
  data = plain;
  data.resize(plain.size() + 5, 0xa5);
  aes.encryptCBC(data.data(), data.size(), iv.data());
  CHECK(std::all_of(data.end() - 5, data.end(), [](u8 b) { return b == 0xa5; }), true);
  aes.decryptCBC(data.data(), data.size(), iv.data());
  CHECK(std::equal(plain.begin(), plain.end(), data.begin()), true);
  aes.encryptECB(data.data(), data.size());
  CHECK(std::all_of(data.end() - 5, data.end(), [](u8 b) { return b == 0xa5; }), true);
}

//SHA-1 on FIPS 180-1's three examples ("abc", the 56-letter message, a million times "a") and the empty message;
//through KIRK's command 0xb too, which refuses an empty or cut-short input and an output under 20 bytes.
static auto sha1Digests() -> void {
  auto digest = [](const std::string& text) {
    auto result = ares::PlayStationPortable::sha1((const u8*)text.data(), text.size());
    return std::vector<u8>(result.begin(), result.end());
  };
  CHECK(digest("abc") == bytes("a9993e364706816aba3e25717850c26c9cd0d89d"), true);
  CHECK(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        bytes("84983e441c3bd26ebaae4aa1f95129e5e54670f1"), true);
  CHECK(digest(std::string(1'000'000, 'a')) == bytes("34aa973cd4c4daa4f61eeb2bdbad27316534016f"), true);
  CHECK(digest("") == bytes("da39a3ee5e6b4b0d3255bfef95601890afd80709"), true);
  CHECK(Kernel::nid("sceKernelExitGame"), 0x0557'2a5f);

  std::vector<u8> input = {3, 0, 0, 0, 'a', 'b', 'c'}, out(20);
  CHECK(Kirk::hash(out.data(), out.size(), input.data(), input.size()), Kirk::Success);
  CHECK(out == bytes("a9993e364706816aba3e25717850c26c9cd0d89d"), true);
  CHECK(Kirk::hash(out.data(), 19, input.data(), input.size()), Kirk::InvalidSize);
  CHECK(Kirk::hash(out.data(), out.size(), input.data(), input.size() - 1), Kirk::InvalidSize);
  CHECK(Kirk::hash(out.data(), out.size(), input.data(), 3), Kirk::InvalidSize);
  input[0] = 0;
  CHECK(Kirk::hash(out.data(), out.size(), input.data(), input.size()), Kirk::InvalidSize);
}

//Command 7: data encrypted as command 4 encrypts it comes back, under every keyseed that isn't made per console, in
//its own buffer, in place over the input, and through decryptInPlace(). Refused: the keyseeds made per console and
//those past KIRK's 128, a header for another mode or submode, no data, data that isn't whole blocks, more than the
//input holds or the output takes.
static auto kirkStatic() -> void {
  auto perConsole = [](u32 keyseed) {
    return (keyseed >= 0x20 && keyseed <= 0x2f) || (keyseed >= 0x6c && keyseed <= 0x7b);
  };
  for(u32 keyseed = 0; keyseed < 0x80; keyseed++) {
    auto data = scrambled(64, keyseed);
    auto input = staticInput(data, keyseed);
    std::vector<u8> out(64);
    u32 result = Kirk::decryptStatic(out.data(), out.size(), input.data(), input.size());
    if(perConsole(keyseed)) {
      CHECK(result, Kirk::InvalidKeyseed);
      continue;
    }
    CHECK(result, Kirk::Success);
    CHECK(out == data, true);
    CHECK(Kirk::decryptStatic(input.data(), input.size(), input.data(), input.size()), Kirk::Success);
    CHECK(std::equal(data.begin(), data.end(), input.begin()), true);
    auto inPlace = staticInput(data, keyseed);
    CHECK(Kirk::decryptInPlace(inPlace.data() + 0x14, 64, keyseed), true);
    CHECK(std::equal(data.begin(), data.end(), inPlace.begin() + 0x14), true);
  }
  CHECK(Kirk::decryptInPlace(scrambled(32, 1).data(), 32, 0x20), false);

  auto data = scrambled(48, 99);
  std::vector<u8> out(48);
  auto refused = [&](std::vector<u8> input, u32 outSize = 48) {
    return Kirk::decryptStatic(out.data(), outSize, input.data(), input.size());
  };
  auto input = staticInput(data, 0x4b);
  for(u32 keyseed : {0x80u, 0xffu}) {
    auto other = input;
    other[0x0c] = keyseed;
    CHECK(refused(other), Kirk::InvalidKeyseed);
  }
  auto other = input;
  put32(other, 0, 4);
  CHECK(refused(other), Kirk::InvalidMode);
  other = input;
  other[0x0d] = 1;
  CHECK(refused(other), Kirk::InvalidMode);
  for(u32 size : {0u, 17u, 64u}) {
    other = input;
    put32(other, 0x10, size);
    CHECK(refused(other, 64), Kirk::InvalidSize);
  }
  CHECK(refused(input, 47), Kirk::InvalidSize);
  CHECK(refused(std::vector<u8>(input.begin(), input.end() - 1)), Kirk::InvalidSize);
  CHECK(refused(std::vector<u8>(input.begin(), input.begin() + 0x13)), Kirk::InvalidSize);
  CHECK(refused(input), Kirk::Success);
}

//Command 1: data encrypted as Sony's tools encrypt it comes back, of sizes in whole blocks and not, after no padding
//and after 0x80 bytes (as a ~PSP file has), its header signed with CMAC or ECDSA (the signature isn't checked), and
//in place over the input. Refused: a header for another command, no data, an output too small for the data, an
//input too small for the header, or for the padding and the data, whatever huge sizes the header claims.
static auto kirkPrivate() -> void {
  for(u32 size : {16u, 100u, 4096u + 7}) {
    for(u32 padding : {0u, 0x80u}) {
      for(bool ecdsa : {false, true}) {
        auto data = scrambled(size, size + padding);
        auto input = privateInput(data, padding, ecdsa);
        std::vector<u8> out(size);
        CHECK(Kirk::decryptPrivate(out.data(), out.size(), input.data(), input.size()), Kirk::Success);
        CHECK(out == data, true);
        CHECK(Kirk::decryptPrivate(input.data(), input.size(), input.data(), input.size()), Kirk::Success);
        CHECK(std::equal(data.begin(), data.end(), input.begin()), true);
      }
    }
  }

  auto data = scrambled(100, 5);
  auto input = privateInput(data, 0x80, false);
  std::vector<u8> out(100);
  auto refused = [&](std::vector<u8> bytes, u32 outSize = 100) {
    return Kirk::decryptPrivate(out.data(), outSize, bytes.data(), bytes.size());
  };
  CHECK(refused(input), Kirk::Success);
  CHECK(refused(input, 99), Kirk::InvalidSize);
  CHECK(refused(std::vector<u8>(input.begin(), input.end() - 1)), Kirk::InvalidSize);
  CHECK(refused(std::vector<u8>(input.begin(), input.begin() + 0x8f)), Kirk::InvalidSize);
  auto other = input;
  put32(other, 0x60, 2);
  CHECK(refused(other), Kirk::InvalidMode);
  for(u32 size : {0u, 0xffff'fff0u, 0xffff'ffffu}) {
    other = input;
    put32(other, 0x70, size);
    CHECK(refused(other, 0xffff'ffff), Kirk::InvalidSize);
  }
  for(u32 padding : {0x81u, 0xffff'ffffu}) {
    other = input;
    put32(other, 0x74, padding);
    CHECK(refused(other), Kirk::InvalidSize);
  }
}

auto cryptoTests() -> Tests {
  return {
    {"crypto aes", aes}, {"crypto sha-1", sha1Digests}, {"crypto kirk static", kirkStatic},
    {"crypto kirk private", kirkPrivate},
  };
}

}
