//SHA-256, as FIPS 180-4 describes it: the message is taken 64 bytes at a time, each block stirred into eight 32-bit
//words of state over 64 rounds; the end is padded with a 1 bit, zeros, and the message's length in bits.
#include "sha256.h"
#include <string.h>

//The first 32 bits of the fractional parts of the cube roots of the first 64 primes, one per round.
static const unsigned int Rounds[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static unsigned int rotate(unsigned int x, int n) { return x >> n | x << (32 - n); }

//One 64-byte block into the state.
static void stir(Sha256* hash, const unsigned char* block) {
  unsigned int w[64];
  for(int i = 0; i < 16; i++) {  //the block as sixteen big-endian words, then 48 more mixed from them
    w[i] = (unsigned int)block[i * 4] << 24 | block[i * 4 + 1] << 16 | block[i * 4 + 2] << 8 | block[i * 4 + 3];
  }
  for(int i = 16; i < 64; i++) {
    unsigned int s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ w[i - 15] >> 3;
    unsigned int s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ w[i - 2] >> 10;
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  unsigned int a = hash->state[0], b = hash->state[1], c = hash->state[2], d = hash->state[3];
  unsigned int e = hash->state[4], f = hash->state[5], g = hash->state[6], h = hash->state[7];
  for(int i = 0; i < 64; i++) {
    unsigned int t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + Rounds[i] + w[i];
    unsigned int t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
  }
  hash->state[0] += a, hash->state[1] += b, hash->state[2] += c, hash->state[3] += d;
  hash->state[4] += e, hash->state[5] += f, hash->state[6] += g, hash->state[7] += h;
}

//The state starts as the first 32 bits of the fractional parts of the square roots of the first eight primes.
void sha256Start(Sha256* hash) {
  static const unsigned int Start[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  };
  memcpy(hash->state, Start, sizeof(Start));
  hash->filled = 0;
  hash->length = 0;
}

void sha256Add(Sha256* hash, const void* data, unsigned int size) {
  const unsigned char* bytes = data;
  hash->length += size;
  while(size) {
    unsigned int take = 64 - hash->filled < size ? 64 - hash->filled : size;
    memcpy(hash->block + hash->filled, bytes, take);
    hash->filled += take, bytes += take, size -= take;
    if(hash->filled == 64) {
      stir(hash, hash->block);
      hash->filled = 0;
    }
  }
}

void sha256Finish(Sha256* hash, char text[65]) {
  unsigned long long bits = hash->length * 8;
  unsigned char end[72] = {0x80};  //the 1 bit, then zeros up to 56 bytes into a block, then the length
  unsigned int pad = hash->filled < 56 ? 56 - hash->filled : 120 - hash->filled;
  for(int i = 0; i < 8; i++) end[pad + i] = bits >> (56 - i * 8);
  sha256Add(hash, end, pad + 8);
  static const char Digits[] = "0123456789abcdef";
  for(int i = 0; i < 32; i++) {
    unsigned char byte = hash->state[i / 4] >> (24 - i % 4 * 8);
    text[i * 2] = Digits[byte >> 4];
    text[i * 2 + 1] = Digits[byte & 15];
  }
  text[64] = 0;
}
