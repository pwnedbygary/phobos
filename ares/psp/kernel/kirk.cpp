//The KIRK engine, the crypto chip in the PSP's main chip, as the PSP Developer Wiki's "Kirk" page describes it: it
//encrypts, decrypts, hashes and signs whatever the kernel hands it, with keys of its own that no program can read.
//The kernel asks for a command by number (through sceUtilsBufferCopyWithRange), with an input buffer that starts
//with the command's header, and an output buffer. Of its 19 commands, decrypting a program takes three: 1, which
//decrypts with a key carried, encrypted, in its header; 7, which decrypts with one of KIRK's own keys; and 0xb,
//SHA-1.
//The keys are in keys.cpp.

namespace Kirk {

static auto word(const u8* bytes) -> u32 {
  return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | u32(bytes[3]) << 24;
}

}

//Command 1, "decrypt private": the data after a 0x90-byte header, and after a padding whose size the header gives,
//is AES-128-CBC with a zero IV, under a key in the header that is itself encrypted with KIRK's own command 1 key.
//The header:
//  0x00  the data's key, encrypted (AES-128, one block) with command 1's key
//  0x10  the key of the header's CMAC signature, encrypted the same way
//  0x20  the signature (0x20-0x60): a CMAC of the header and one of the data, or (0x10-0x60) two ECDSA signatures
//  0x60  1: the command the header is for
//  0x64  bit 0 set: the signature is ECDSA's, not CMAC's
//  0x70  the data's size, then (0x74) the padding's
//The PSP checks the signature before it decrypts. Phobos doesn't: an ECDSA signature would need Sony's private keys
//to make (tests couldn't), and the ~PSP header's own check (decrypt.cpp) has already said whether it's whole. out
//gets the data decrypted, its size bytes (the data is encrypted a whole 16-byte block at a time: the last block's
//bytes past its size are dropped).
auto Kirk::decryptPrivate(u8* out, u32 outSize, const u8* in, u32 inSize) -> u32 {
  if(inSize < 0x90) return InvalidSize;
  if(word(in + 0x60) != 1) return InvalidMode;
  u32 size = word(in + 0x70), padding = word(in + 0x74);
  u64 blocks = (u64(size) + 15) & ~15ull;
  if(!size || outSize < size || 0x90 + u64(padding) + blocks > inSize) return InvalidSize;
  u8 key[16];
  memcpy(key, in, 16);
  AES128{Keys::kirk1}.decrypt(key);
  AES128 aes{key};
  const u8* data = in + 0x90 + padding;
  u32 whole = size & ~15u;
  //a last block only partly kept is decrypted on its own, chained on from the encrypted block before it (or the
  //zero IV), both taken before out is written, as out may be where the input was
  u8 last[16] = {}, before[16] = {};
  if(whole < size) memcpy(last, data + whole, 16);
  if(whole < size && whole) memcpy(before, data + whole - 16, 16);
  u8 iv[16] = {};
  memmove(out, data, whole);
  aes.decryptCBC(out, whole, iv);
  if(whole < size) {
    aes.decryptCBC(last, 16, before);
    memcpy(out + whole, last, size - whole);
  }
  return Success;
}

//Command 7, "decrypt static": the data after a 0x14-byte header is AES-128-CBC with a zero IV, under the one of the
//128 keys KIRK holds for commands 4 (encrypt) and 7 that the header's keyseed picks (KIRK's key slot 4 + keyseed).
//The header: 0x00 the mode, 5 (decrypting); 0x0c the keyseed; 0x0d the submode, whose low 3 bits are 0 for this
//command; 0x10 the data's size. On a PSP the keys of keyseeds 0x20-0x2f and 0x6c-0x7b are changed for each console,
//from its fuse ID: Phobos, which has no console's fuse ID, refuses them. (No ~PSP tag uses them: keys.cpp's tags
//name keyseeds 0x42 to 0x5e.) The data is decrypted a whole block at a time, so its size must be a multiple of 16.
auto Kirk::decryptStatic(u8* out, u32 outSize, const u8* in, u32 inSize) -> u32 {
  if(inSize < 0x14) return InvalidSize;
  if(word(in) != 5 || (in[0x0d] & 7) != 0) return InvalidMode;
  u32 keyseed = in[0x0c], size = word(in + 0x10);
  bool perConsole = (keyseed >= 0x20 && keyseed <= 0x2f) || (keyseed >= 0x6c && keyseed <= 0x7b);
  if(keyseed >= 0x80 || perConsole) return InvalidKeyseed;
  if(!size || size % 16 || outSize < size || inSize - 0x14 < size) return InvalidSize;
  memmove(out, in + 0x14, size);
  u8 iv[16] = {};
  AES128{Keys::kirk7[keyseed]}.decryptCBC(out, size, iv);
  return Success;
}

//Command 7 on data in place, its header made here: what the ~PSP format's steps use (decrypt.cpp).
auto Kirk::decryptInPlace(u8* data, u32 size, u8 keyseed) -> bool {
  std::vector<u8> input(0x14 + size, 0);
  input[0x00] = 5;
  input[0x0c] = keyseed;
  for(u32 n = 0; n < 4; n++) input[0x10 + n] = u8(size >> n * 8);
  memcpy(input.data() + 0x14, data, size);
  return decryptStatic(data, size, input.data(), input.size()) == Success;
}

//Command 0xb: the SHA-1 digest of the data after a 4-byte header giving its size; 20 bytes out.
auto Kirk::hash(u8* out, u32 outSize, const u8* in, u32 inSize) -> u32 {
  if(inSize < 4) return InvalidSize;
  u32 size = word(in);
  if(!size || inSize - 4 < size || outSize < 20) return InvalidSize;
  auto digest = sha1(in + 4, size);
  memcpy(out, digest.data(), digest.size());
  return Success;
}

//SHA-1, as FIPS 180-1 defines it: the message is padded to a multiple of 64 bytes (a 1 bit, zeros, then its length
//in bits as a 64-bit number, most significant byte first), and each 64-byte chunk is stirred into five 32-bit words
//through 80 rounds. The digest is the five words, most significant byte first.
auto sha1(const u8* data, u64 size) -> std::array<u8, 20> {
  std::vector<u8> message(data, data + size);
  u64 bits = size * 8;
  message.push_back(0x80);
  while(message.size() % 64 != 56) message.push_back(0);
  for(s32 shift = 56; shift >= 0; shift -= 8) message.push_back(u8(bits >> shift));
  u32 hash[5] = {0x6745'2301, 0xefcd'ab89, 0x98ba'dcfe, 0x1032'5476, 0xc3d2'e1f0};
  for(u64 chunk = 0; chunk < message.size(); chunk += 64) {
    u32 w[80];
    for(u32 i = 0; i < 16; i++) {
      const u8* b = &message[chunk + i * 4];
      w[i] = u32(b[0]) << 24 | u32(b[1]) << 16 | u32(b[2]) << 8 | u32(b[3]);
    }
    for(u32 i = 16; i < 80; i++) w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    u32 a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];
    for(u32 i = 0; i < 80; i++) {
      u32 f, k;
      if(i < 20)      f = (b & c) | (~b & d),          k = 0x5a82'7999;
      else if(i < 40) f = b ^ c ^ d,                   k = 0x6ed9'eba1;
      else if(i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1b'bcdc;
      else            f = b ^ c ^ d,                   k = 0xca62'c1d6;
      u32 t = std::rotl(a, 5) + f + e + k + w[i];
      e = d; d = c; c = std::rotl(b, 30); b = a; a = t;
    }
    hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d; hash[4] += e;
  }
  std::array<u8, 20> digest;
  for(u32 n = 0; n < 20; n++) digest[n] = u8(hash[n / 4] >> (24 - n % 4 * 8));
  return digest;
}
