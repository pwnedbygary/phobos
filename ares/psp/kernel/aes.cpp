//AES-128, as FIPS-197 (the "Advanced Encryption Standard", NIST, 2001) defines it: the cipher of the PSP's KIRK
//engine (kirk.cpp), and so of everything that decrypts a game.
//
//AES scrambles 16 bytes at a time (a block) under a 16-byte key. It sees the block as a 4x4 grid of bytes, the state,
//filled a column at a time: bytes 0-3 are the first column, top to bottom. Encrypting is ten rounds of four steps,
//each easy to undo on its own, which together mix every bit of the block with every other and with the key:
//  - SubBytes: each byte is swapped for another through a fixed table of 256, the S-box;
//  - ShiftRows: row r moves r places to the left, wrapping around, so each column gets a byte from every column;
//  - MixColumns: each column's four bytes become four sums of all four times small constants (not in round ten);
//  - AddRoundKey: the state is XORed with that round's 16 bytes of key.
//One AddRoundKey also comes before the first round: 11 round keys in all, made from the key (the key expansion).
//Decrypting undoes the steps in reverse order: the S-box's inverse, rows moved right, and MixColumns' inverse.
//
//The arithmetic is in the field GF(2^8), whose 256 elements are the bytes. Adding two is XOR. Multiplying treats
//each byte's bits as a polynomial's coefficients (0x13 is x^4 + x + 1) and multiplies those, except that x^8 is
//replaced by x^4 + x^3 + x + 1 (0x1b) wherever it turns up, so a product always fits in a byte. Doubling (times x)
//is then a shift left, XORed with 0x1b when a bit falls off the top, and any product is a sum of doublings, as in
//long multiplication.

//Doubling in GF(2^8).
auto AES128::twice(u8 value) -> u8 {
  return value << 1 ^ (value & 0x80 ? 0x1b : 0);
}

//The S-box and its inverse, made from their definition (FIPS-197, 5.1.1) rather than typed out: a byte's
//reciprocal in GF(2^8) (the byte it multiplies to 1; 0 has none and stays 0), put through an "affine
//transformation": XORed with itself rotated left by 1, 2, 3 and 4 bits, and with 0x63.
struct AES128::Tables {
  u8 sbox[256], inverse[256];

  //a * b: a doubled once for each bit of b, and the doublings for b's set bits added up
  static auto times(u8 a, u8 b) -> u8 {
    u8 product = 0;
    for(; b; b >>= 1, a = twice(a)) {
      if(b & 1) product ^= a;
    }
    return product;
  }

  static auto rotate(u8 value, u32 bits) -> u8 {
    return value << bits | value >> (8 - bits);
  }

  Tables() {
    for(u32 value = 0; value < 256; value++) {
      u8 reciprocal = 0;
      for(u32 candidate = 1; value && candidate < 256 && !reciprocal; candidate++) {
        if(times(value, candidate) == 1) reciprocal = candidate;
      }
      u8 substitute = reciprocal ^ rotate(reciprocal, 1) ^ rotate(reciprocal, 2) ^ rotate(reciprocal, 3) ^
                      rotate(reciprocal, 4) ^ 0x63;
      sbox[value] = substitute;
      inverse[substitute] = value;
    }
  }
};

auto AES128::tables() -> const Tables& {
  static const Tables made;  //once, the first time they're needed
  return made;
}

//The key expansion (FIPS-197, 5.2): the 176 bytes of round keys start with the key; each 4 bytes after it are the 4
//before them XORed with the 4 sixteen bytes back. At the start of each round key (every 16 bytes), the 4 before are
//first rotated a byte to the left and put through the S-box, and their first byte XORed with the round's constant:
//1, then doubled for each round key (1, 2, 4, ... 0x80, 0x1b, 0x36).
AES128::AES128(const u8 key[16]) {
  auto& t = tables();
  memcpy(roundKeys, key, 16);
  u8 constant = 1;
  for(u32 at = 16; at < sizeof(roundKeys); at += 4) {
    u8 word[4] = {roundKeys[at - 4], roundKeys[at - 3], roundKeys[at - 2], roundKeys[at - 1]};
    if(at % 16 == 0) {
      u8 first = word[0];
      word[0] = t.sbox[word[1]] ^ constant;
      word[1] = t.sbox[word[2]];
      word[2] = t.sbox[word[3]];
      word[3] = t.sbox[first];
      constant = twice(constant);
    }
    for(u32 n = 0; n < 4; n++) roundKeys[at + n] = roundKeys[at - 16 + n] ^ word[n];
  }
}

auto AES128::addRoundKey(u8 state[16], u32 round) const -> void {
  for(u32 n = 0; n < 16; n++) state[n] ^= roundKeys[round * 16 + n];
}

//The state's byte at row r and column c is state[r + 4 * c]. Row r takes the bytes r columns to its right.
auto AES128::shiftRows(u8 state[16]) -> void {
  u8 before[16];
  memcpy(before, state, 16);
  for(u32 row = 1; row < 4; row++) {
    for(u32 column = 0; column < 4; column++) state[row + 4 * column] = before[row + 4 * ((column + row) % 4)];
  }
}

auto AES128::unshiftRows(u8 state[16]) -> void {
  u8 before[16];
  memcpy(before, state, 16);
  for(u32 row = 1; row < 4; row++) {
    for(u32 column = 0; column < 4; column++) state[row + 4 * ((column + row) % 4)] = before[row + 4 * column];
  }
}

//Each column's bytes a0-a3 become 2a0+3a1+a2+a3, a0+2a1+3a2+a3, a0+a1+2a2+3a3 and 3a0+a1+a2+2a3: each byte twice
//itself, three times the one below it, and once each of the other two (below the bottom byte comes the top one).
//Three times is twice plus once.
auto AES128::mixColumns(u8 state[16]) -> void {
  for(u32 column = 0; column < 4; column++) {
    u8* a = state + 4 * column;
    u8 before[4] = {a[0], a[1], a[2], a[3]};
    for(u32 n = 0; n < 4; n++) {
      u8 below = before[(n + 1) % 4];
      a[n] = twice(before[n]) ^ twice(below) ^ below ^ before[(n + 2) % 4] ^ before[(n + 3) % 4];
    }
  }
}

//The inverse: each byte 14 times itself, 11 times the one below, 13 times the next and 9 times the last. Those
//multiples are sums of the byte doubled once, twice and three times (2a, 4a, 8a): 9a = 8a+a, 11a = 8a+2a+a,
//13a = 8a+4a+a, 14a = 8a+4a+2a.
auto AES128::unmixColumns(u8 state[16]) -> void {
  for(u32 column = 0; column < 4; column++) {
    u8* a = state + 4 * column;
    u8 nine[4], eleven[4], thirteen[4], fourteen[4];
    for(u32 n = 0; n < 4; n++) {
      u8 two = twice(a[n]), four = twice(two), eight = twice(four);
      nine[n] = eight ^ a[n];
      eleven[n] = eight ^ two ^ a[n];
      thirteen[n] = eight ^ four ^ a[n];
      fourteen[n] = eight ^ four ^ two;
    }
    for(u32 n = 0; n < 4; n++) {
      a[n] = fourteen[n] ^ eleven[(n + 1) % 4] ^ thirteen[(n + 2) % 4] ^ nine[(n + 3) % 4];
    }
  }
}

auto AES128::encrypt(u8 block[16]) const -> void {
  auto& t = tables();
  addRoundKey(block, 0);
  for(u32 round = 1; round <= 10; round++) {
    for(u32 n = 0; n < 16; n++) block[n] = t.sbox[block[n]];
    shiftRows(block);
    if(round < 10) mixColumns(block);
    addRoundKey(block, round);
  }
}

//The rounds backwards, each step undone.
auto AES128::decrypt(u8 block[16]) const -> void {
  auto& t = tables();
  for(u32 round = 10; round >= 1; round--) {
    addRoundKey(block, round);
    if(round < 10) unmixColumns(block);
    unshiftRows(block);
    for(u32 n = 0; n < 16; n++) block[n] = t.inverse[block[n]];
  }
  addRoundKey(block, 0);
}

auto AES128::encryptECB(u8* data, u64 size) const -> void {
  for(u64 at = 0; at + 16 <= size; at += 16) encrypt(data + at);
}

auto AES128::decryptECB(u8* data, u64 size) const -> void {
  for(u64 at = 0; at + 16 <= size; at += 16) decrypt(data + at);
}

//In CBC each block is XORed with the encrypted block before it (the first with the IV) and then encrypted, so equal
//blocks encrypt differently. Decrypting a block undoes that with the encrypted block before it, which decrypting in
//place has just overwritten: it's kept aside first.
auto AES128::encryptCBC(u8* data, u64 size, const u8 iv[16]) const -> void {
  const u8* previous = iv;
  for(u64 at = 0; at + 16 <= size; at += 16) {
    for(u32 n = 0; n < 16; n++) data[at + n] ^= previous[n];
    encrypt(data + at);
    previous = data + at;
  }
}

auto AES128::decryptCBC(u8* data, u64 size, const u8 iv[16]) const -> void {
  u8 previous[16], encrypted[16];
  memcpy(previous, iv, 16);
  for(u64 at = 0; at + 16 <= size; at += 16) {
    memcpy(encrypted, data + at, 16);
    decrypt(data + at);
    for(u32 n = 0; n < 16; n++) data[at + n] ^= previous[n];
    memcpy(previous, encrypted, 16);
  }
}
