#pragma once
//SHA-256 (FIPS 180-4): a 256-bit fingerprint of a file, the same on the PSP as on a computer, so a copied file can be
//checked against the original without the original at hand (`shasum -a 256 -c SHA256SUMS`).

typedef struct {
  unsigned int state[8];       //the fingerprint so far
  unsigned char block[64];     //bytes waiting for a whole 64-byte block
  unsigned int filled;         //how many of them
  unsigned long long length;   //bytes taken in, all told
} Sha256;

void sha256Start(Sha256* hash);
void sha256Add(Sha256* hash, const void* data, unsigned int size);
//The fingerprint as 64 lowercase hexadecimal digits (and a terminating 0) in text.
void sha256Finish(Sha256* hash, char text[65]);
