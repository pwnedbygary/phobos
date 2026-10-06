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

//The keys themselves. The round trips (above, and tests/psp/decrypt.cpp's through encrypt.hpp) encrypt with the very
//tables they test, so a key typed wrong would still come back: each key's SHA-1 digest is pinned here, worked out
//from keys.cpp once a reviewer had checked its bytes against the PSP Developer Wiki's "Keys" page. KIRK command 1's
//key; commands 4 and 7's 128 keys as one run, and those the tags use one at a time; and each tag's key (a 144-byte
//one as its words' bytes, low byte first), with its type and keyseed, and type 5's XOR key.
static auto keys() -> void {
  auto digest = [](const u8* data, u64 size) {
    auto result = ares::PlayStationPortable::sha1(data, size);
    return std::vector<u8>(result.begin(), result.end());
  };
  CHECK(digest(Keys::kirk1, 16) == bytes("5baf7fbe574e52175f23ca0f773246353fc16e40"), true);
  CHECK(digest(&Keys::kirk7[0][0], sizeof(Keys::kirk7)) == bytes("30521b60c36858845ea37faaebf30c701abebd0c"), true);
  struct Seed { u32 keyseed; const char* digest; };
  for(auto [keyseed, sum] : {
    Seed{0x42, "75f32bb7f760477d5110df60931421bd051b0afb"}, Seed{0x43, "1bb1597cad7c47537eee8d99a59a1d43899b8fa9"},
    Seed{0x46, "ca172c1f218fa511488f833aff81e32c43cb7877"}, Seed{0x47, "91171049c9d94ebc6ced4dcc79730e26382a2f3a"},
    Seed{0x4b, "d8ecc39be43c7f4245ba55e434b088ff9b78ddfc"}, Seed{0x4c, "3e7d08e512638b28f151a8632f5d1114f6d006b4"},
    Seed{0x4f, "1694f090ae23b4a2c7bfd13c3b0b1ef35964fc80"}, Seed{0x59, "5ec9616d34e4aca7f17dad24a79062438883cc13"},
    Seed{0x5b, "6014004031d0c99a57e02ca1bff498f8f7b69d53"}, Seed{0x5d, "f603c34ccfdb6c54855e89fc7c989ed7d4bf3bb8"},
    Seed{0x5e, "a6aaaa193afb63ca7222115c8ffcde1129072aa2"},
  }) {
    CHECK(digest(Keys::kirk7[keyseed], 16) == bytes(sum), true);
  }

  struct Tag { u32 tag, type, keyseed; const char* key; const char* xorKey = nullptr; };
  std::vector<Tag> pads = {
    {0x0000'0000, 0, 0x42, "6f5cda910fb34b5837cf1abd08052499d1c320a6"},
    {0x0300'0000, 0, 0x46, "f85837d895189b545054e4f3a4c47abf68d57d7e"},
    {0x0800'0000, 0, 0x4b, "50135c6b25ba916649b43e366a88f5347cf86a4d"},
    {0x0900'0000, 0, 0x4c, "3e1b13a79e7724acf674e0410ec21fc7cf077e15"},
    {0x0c00'0000, 0, 0x4f, "84712f1c9c95ebe662c317de3f1bfb8561b09f89"},
    {0x4467'415d, 1, 0x59, "138264819bc06fa6d4536bff3b468fdc6bda2ec6"},
    {0x3ace'4dce, 1, 0x5b, "86a501503ed025b66c8ecc88bf24470c30411a7e"},
    {0xc0cb'167c, 1, 0x5d, "f4417095dca1cac5b6bcaf7d461f0837ffdf87c8"},
    {0xbb67'c59f, 1, 0x5e, "c3eebdddf298798ab7cb45d54f556279eda9b966"},
  };
  const char* pauth = "48a104276dd13a89205de16bdecf178239f9753c";  //scePauth's XOR key
  std::vector<Tag> seeds = {
    {0x0000'0000, 2, 0x42, "e997fbcbe21f9e3f242ce5800be352cc4664ab0b"},
    {0x4467'415d, 2, 0x59, "9736012811def66085bdb3124d5fe2e87f686f22"},
    {0x4c94'0ff0, 2, 0x43, "bf31f70dab7add43530d99f8a35bc9a14dc31872"},
    {0x4c94'0af0, 2, 0x43, "8fa81cd3d59e6c118db1a100a630491c232b26b5"},
    {0x4c94'0bf0, 2, 0x43, "2303225748a291798aeb9b4f6babf744ac3026ba"},
    {0x4c94'0cf0, 2, 0x43, "3e944bc0ff7f274f8167ada14719dad5da89f35d"},
    {0x4c94'0df0, 2, 0x43, "ef0c4242ec83afa126919b896d2210c03e7fb0ca"},
    {0x4c94'10f0, 2, 0x43, "c688ce15a274746affea9ec21ca31f0a6754eac2"},
    {0x4c94'12f0, 2, 0x43, "6b90d552445f3e62e534276b4e8827b1c78b3af5"},
    {0x4c94'13f0, 2, 0x43, "37afc08d0196d20bfa8654fc3dd675fac4270832"},
    {0x4c94'14f0, 2, 0x43, "7159e511c8fb58270c659e443277135909c1b32d"},
    {0x4c94'15f0, 2, 0x43, "3279e9fb4c24a9369a2c2115b1b93b9d7123b501"},
    {0x4c94'16f0, 2, 0x43, "1ebf73aa2daa3ae116111a40bc5c66601b195a68"},
    {0x4c94'17f0, 2, 0x43, "c0b7ccd07bbb44d312748e6aa00ade6127fde3fe"},
    {0x4c94'1ff0, 2, 0x43, "4f561e69d5408ab4ce08648332dffe7d1284dee8"},
    {0x4c94'18f0, 2, 0x43, "0bfe34786938f7feeb5a237bf008f8b03eabb1d9"},
    {0x4c94'19f0, 2, 0x43, "5943dfca01360877b4d87bb7bb73a323183258b3"},
    {0x4c94'29f0, 2, 0x43, "cd28abad67a9760a49e7d9c42befe0f60ec5f13f"},
    {0x4c94'1ef0, 2, 0x43, "5bfa0ee2b07529632211e7343c503335428a3a47"},
    {0x4c94'22f0, 2, 0x43, "6663d2c5b0b75bc1a2e325f2507101e7a911f930"},
    {0x4c94'1cf0, 2, 0x43, "d9994ff218910af5bbcd550123d4c9355d08ca49"},
    {0x4c94'1df0, 2, 0x43, "2edcaea27e662362e4a6781d18f0ae018112b246"},
    {0x4c94'28f0, 2, 0x43, "e25bc44a61d449f7daada2895a904741d0a55cc6"},
    {0x4c94'2af0, 2, 0x43, "aab39ea8ca230c21f2a3a8cd96e3e27abfed8aa5"},
    {0x4c94'84f0, 2, 0x43, "10424d6ad8992b562bd8b4046a848794581d4195"},
    {0x4c94'85f0, 2, 0x43, "3326593d785c07893c1bf024238076d4630be8dc"},
    {0x4c94'86f0, 2, 0x43, "ab95852cf509494bb080ed834eda4d46d9627be4"},
    {0x4c94'87f0, 2, 0x43, "423d0978d0151c1222e22e9d4f1b8b63a157db53"},
    {0x4c94'8af0, 2, 0x43, "d2261f6821d482d02a2f715a344f78b92dff799c"},
    {0x4c94'8bf0, 2, 0x43, "fd5f2a181c19325cda8560c2dfdb1e15bdb37053"},
    {0x4c94'8cf0, 2, 0x43, "a46656219e5778d3bfdcc97ec2839847a1f63fff"},
    {0x4c94'8df0, 2, 0x43, "8ed481915536c28a66717323b9f260926450beba"},
    {0x4c94'90f0, 2, 0x43, "dcb564734191058db2b05465c73fa618a62183aa"},
    {0x4c94'91f0, 2, 0x43, "85617687b51ebd29816bea8681be3e21b43cedd0"},
    {0x4c94'92f0, 2, 0x43, "d625dc225330de688891b224378c85155c0c8a6a"},
    {0x4c94'93f0, 2, 0x43, "8666f3f878f087b2707aec82cb3866a586c3f4d3"},
    {0x4c94'94f0, 2, 0x43, "68f50d056ec1739d7806569be128640a95ed7876"},
    {0x4c94'95f0, 2, 0x43, "2d87ec0b2a2627043a97d1877dfe14a8e3307cd9"},
    {0x4c94'96f0, 2, 0x43, "eb85f0355e3fddf952356f7ed23b2d60f2985997"},
    {0x4c94'97f0, 2, 0x43, "7b36598a8f895a67e002e892aba26a1f8f30242a"},
    {0x7620'2403, 2, 0x5b, "86b8ab50ca816b7c91253df2e9d59e230f323632"},
    {0x457b'05f0, 2, 0x5b, "e0c6d74a47d906ccac6f3f7ac8f0ed231d9b7bfc"},
    {0x457b'06f0, 2, 0x5b, "a67b59e51c2e6b66b9abec4bf919241e0d0c59f8"},
    {0x457b'08f0, 2, 0x5b, "481e47a752b52584d7c7f55a47158efe7994d05c"},
    {0x457b'0af0, 2, 0x5b, "da0c7188957708edf5f0f312f4a06c3487412a51"},
    {0x457b'0bf0, 2, 0x5b, "8cd435ea59187e9d0f7e8adb65112529a324ebd9"},
    {0x457b'0cf0, 2, 0x5b, "3f31b055940c3dcbd2d8931154770cdc850419c7"},
    {0x457b'10f0, 2, 0x5b, "6449dc2c24f30a2d1cc2ca39db0a180c764a691a"},
    {0x457b'1ef0, 2, 0x5b, "9546b36080c07d1d6ca339616dca082bcfb3c551"},
    {0x457b'28f0, 2, 0x5b, "cf3f7a5786782a11966c1e895f9142d4c9e27414"},
    {0x457b'80f0, 6, 0x5b, "776e6d9baab779a0081b554d065b6d3a9744c9d1"},
    {0x457b'81f0, 6, 0x5b, "57aa1d14155ee12267b232af8d38f57b898139b6"},
    {0x457b'82f0, 6, 0x5b, "aa0deb93569512a97a8f8602c6cc2d759e281a0b"},
    {0x457b'83f0, 6, 0x5b, "fe8af68bf03a22dc4c6676a1174944d5f93c5c7f"},
    {0x8004'fd03, 2, 0x5d, "292768cecd4637b42e26d9d437bae719619b345f"},
    {0xd916'05f0, 2, 0x5d, "822b89e598ccbc887f8d82cd30676607e026267b"},
    {0xd916'06f0, 2, 0x5d, "33a904ec5cd582c7593a4f3de25c6dc7b10acb90"},
    {0xd916'08f0, 2, 0x5d, "9168ad2b5ec0ed3694a2c278334fecf7cadaf367"},
    {0xd916'09f0, 2, 0x5d, "44b63861bfb2c2bf5dab815aded1c77c37a7f440"},
    {0xd916'0af0, 2, 0x5d, "6764b2eb8f2b49491b17bf3d986693e58cffac89"},
    {0xd916'0bf0, 2, 0x5d, "ae18641f02ad5afdf45b0fdf51b85cbac82f3e9b"},
    {0xd916'11f0, 2, 0x5d, "2f7536809fcdc8bec6b8ce693cbb04c32cd5d03d"},
    {0xd916'12f0, 2, 0x5d, "1edabf966df1b91bd4c55d21cbc62aed33daefe3"},
    {0xd916'13f0, 2, 0x5d, "ec7742fed5555d14c12e6ce9078bec6919bfc40e"},
    {0xd916'14f0, 2, 0x5d, "3da9908f0c7edfc8584eec259cba16036ba24af2"},
    {0xd916'15f0, 2, 0x5d, "e838a0a9f8886b8c9f4e42051ba7a9fe389fc3cb"},
    {0xd916'16f0, 2, 0x5d, "ea2cdffc9f261dada9a2b3d592fe388ffd936eb3"},
    {0xd916'17f0, 2, 0x5d, "fef5e0df37ab19e92779e68246ad4f9074f1bafb"},
    {0xd916'18f0, 2, 0x5d, "d859898310a53e8bf14320b9c8b8bd23ecfc05c0"},
    {0xd916'19f0, 2, 0x5d, "4876e22bfefa1c2a9d6cc24fdf9377f37d7a26f5"},
    {0xd916'1af0, 2, 0x5d, "9d2463879ba9e1bc567fc35c21579ccf514404b3"},
    {0xd916'20f0, 2, 0x5d, "e4e8b8e9eec8a6b72e3b9a3c0c58c7dbf01c8caa"},
    {0xd916'21f0, 2, 0x5d, "f637874903c552b0160d7a0e7a50f80e2a8e27b0"},
    {0xd916'22f0, 2, 0x5d, "8c9e3566e9bbdeaa78137082896e4bdb62de6ffa"},
    {0xd916'23f0, 2, 0x5d, "c4e86ad692e5bd4773931019e09b44ee29e874f7"},
    {0xd916'24f0, 2, 0x5d, "d88f039f20f3ef014507b4c57030e135c8113853"},
    {0xd916'28f0, 2, 0x5d, "e4ab98b53f57265b31c05bfa8ef4e6574726ea9a"},
    {0xd916'80f0, 6, 0x5d, "4e2fe573529027fb8fed551f564ef5685977df85"},
    {0xd916'81f0, 6, 0x5d, "5ee284bd8e1e6948492c3a21f8e00e76a2f7734b"},
    {0x0a35'ea03, 2, 0x5e, "fcc441d2b52f0614b078870e1d5ce5774aba959c"},
    {0x7b05'05f0, 2, 0x5e, "6dfaa1b1f188e6e3687fb91335c54d58734fd2e2"},
    {0x7b05'06f0, 2, 0x5e, "a3b2dbe1f29cda5667f893c8246403f8f91821ce"},
    {0x7b05'08f0, 2, 0x5e, "46b12fa0f68355e00a23ad97a13ac8fcf325354e"},
    {0x2fd3'11f0, 5, 0x47, "7af119123c84d4272fe7c0e194d686f1a9266966", pauth},
    {0x2fd3'12f0, 5, 0x47, "35b7584e02ff51721ebf6eb33df754a1356c2909", pauth},
    {0x2fd3'13f0, 5, 0x47, "ab74997f6ddec2f0d48ebd2bf019b4afe7e5adb9", pauth},
  };
  CHECK(Keys::padTags.size(), pads.size());
  CHECK(Keys::seedTags.size(), seeds.size());
  for(u32 n = 0; n < pads.size() && n < Keys::padTags.size(); n++) {
    auto& entry = Keys::padTags[n];
    u8 key[144];
    for(u32 at = 0; at < 144; at++) key[at] = u8(entry.pad[at / 4] >> at % 4 * 8);
    bool same = entry.tag == pads[n].tag && entry.type == pads[n].type && entry.keyseed == pads[n].keyseed &&
                digest(key, 144) == bytes(pads[n].key);
    CHECK(same, true);
    if(!same) std::printf("  tag %08x's 144-byte key\n", entry.tag);
  }
  for(u32 n = 0; n < seeds.size() && n < Keys::seedTags.size(); n++) {
    auto& entry = Keys::seedTags[n];
    auto& pinned = seeds[n];
    bool same = entry.tag == pinned.tag && entry.type == pinned.type && entry.keyseed == pinned.keyseed &&
                digest(entry.seed, 16) == bytes(pinned.key) && !entry.xorKey == !pinned.xorKey &&
                (!entry.xorKey || digest(entry.xorKey, 16) == bytes(pinned.xorKey));
    CHECK(same, true);
    if(!same) std::printf("  tag %08x's 16-byte key\n", entry.tag);
  }
}

auto cryptoTests() -> Tests {
  return {
    {"crypto aes", aes}, {"crypto sha-1", sha1Digests}, {"crypto kirk static", kirkStatic},
    {"crypto kirk private", kirkPrivate}, {"crypto keys", keys},
  };
}

}
