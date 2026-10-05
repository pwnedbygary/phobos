//The PSP's cryptography (ares/psp/kernel/crypto.hpp): AES-128 against the standards' own examples.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::AES128;

//Bytes written as hexadecimal digits, two to a byte.
static auto bytes(const char* text) -> std::vector<u8> {
  std::vector<u8> result;
  auto digit = [](char c) -> u32 { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for(; text[0] && text[1]; text += 2) result.push_back(digit(text[0]) << 4 | digit(text[1]));
  return result;
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

auto cryptoTests() -> Tests {
  return {
    {"crypto aes", aes},
  };
}

}
