//Unpacking the blocks of the PSP homebrew scene's compressed disc images (disc.cpp): LZ4, which CSO version 2 and ZSO
//images use, and LZO1X, which JSO images may use. (Deflate, the third, is nall's inflate.) Each takes a block's
//packed bytes and fills an output of a known size, and says how many bytes it unpacked, or 0 if the packed bytes are
//damaged: a length past either end, or a match reaching back before the output's start. Nothing is read or written
//outside the two buffers, whatever the packed bytes say.
//
//Both are "LZ77" schemes: the packed bytes are a run of instructions, each either copying literal bytes from the
//packed stream, or copying a match: bytes the output already holds, from some distance back (a distance shorter than
//the length repeats them, as "abcabcabc" is "abc" then a match 3 back, 6 long).

//LZ4's block format (lz4's own description of it, lz4_Block_format.md): a run of sequences, each a token byte, its
//high 4 bits the count of literals and its low 4 bits the match's length less 4. A count of 15 goes on in the bytes
//that follow, each adding up to 255 (a byte of 255 says another follows). Then the literals, then the match's
//distance back (2 bytes, little-endian, never 0), then the rest of its length if it needed more bytes. The last
//sequence has only literals: the block ends after them, or once the output is whole, as a CSO's or a ZSO's block may
//be padded out to the image's alignment (maxcso's description of both says so) and the padding isn't LZ4.
auto unpackLZ4(const u8* in, u32 inSize, u8* out, u32 outSize) -> u32 {
  u32 at = 0, made = 0;
  //a count of 15 going on in the bytes after it
  auto more = [&](u32& count) -> bool {
    u32 byte = 255;
    while(byte == 255) {
      if(at >= inSize) return false;
      byte = in[at++];
      count += byte;
      if(count > outSize) return false;  //no count can be more than the whole output
    }
    return true;
  };
  while(at < inSize && made < outSize) {
    u32 token = in[at++];
    u32 literals = token >> 4;
    if(literals == 15 && !more(literals)) return 0;
    if(literals > inSize - at || literals > outSize - made) return 0;
    memcpy(out + made, in + at, literals);
    at += literals, made += literals;
    if(at == inSize || made == outSize) break;  //the last sequence
    if(inSize - at < 2) return 0;
    u32 distance = in[at] | in[at + 1] << 8;
    at += 2;
    u32 length = token & 15;
    if(length == 15 && !more(length)) return 0;
    length += 4;
    if(!distance || distance > made || length > outSize - made) return 0;
    for(u32 n = 0; n < length; n++, made++) out[made] = out[made - distance];
  }
  return made;
}

//LZO1X, as minilzo (the library JSO's tools use) writes it. (Its stream is described in Linux's
//Documentation/staging/lzo.rst; LZO's own source is the reference, there being no specification.) Each instruction
//byte copies a match from up to 48 KiB back, and then 0 to 3 literals, its last two bits saying how many ("the
//state"); the next instruction's meaning depends on that state when its top four bits are 0:
//  0000LLLL, after no literals: a run of 4 or more literals (3 + L, or 18 and up when L is 0).
//  0000DDSS, after 1 to 3 literals: a 2-byte match up to 1 KiB back; then a byte H: distance (H << 2) + D + 1.
//  0000DDSS, after a run of 4 or more literals: a 3-byte match 2 to 3 KiB back: distance (H << 2) + D + 2049.
//  0001HLLL: a match 16 to 48 KiB back, 2 + L long (9 and up when L is 0), then 2 bytes (little-endian) whose top
//            14 bits D give the distance 16384 + (H << 14) + D and low 2 bits the literals after. A distance of
//            exactly 16384 ends the stream (minilzo ends every stream with 0x11 0x00 0x00).
//  001LLLLL: a match up to 16 KiB back, 2 + L long (33 and up when L is 0), then 2 bytes as above: distance D + 1.
//  01LDDDSS: a 3 or 4 byte match (3 + L) up to 2 KiB back; then a byte H: distance (H << 3) + D + 1.
//  1LLDDDSS: a 5 to 8 byte match (5 + L), the same distance.
//A length of 0 in its bits goes on in the bytes after it: each 0 byte adds 255, and the first that isn't adds itself.
//The first byte is different, as there's nothing to match yet: 18 and up means that many less 17 literals.
auto unpackLZO(const u8* in, u32 inSize, u8* out, u32 outSize) -> u32 {
  u32 at = 0, made = 0, state = 0;
  auto byte = [&](u32& value) -> bool {
    if(at >= inSize) return false;
    value = in[at++];
    return true;
  };
  //a length whose bits were 0: `start` plus 255 for each 0 byte, plus the first byte that isn't 0
  auto more = [&](u32 start, u32& length) -> bool {
    length = start;
    while(true) {
      u32 next = 0;
      if(!byte(next)) return false;
      if(next) return length += next, length <= outSize;
      length += 255;
      if(length > outSize) return false;
    }
  };
  auto literals = [&](u32 count) -> bool {
    if(count > inSize - at || count > outSize - made) return false;
    memcpy(out + made, in + at, count);
    at += count, made += count;
    return true;
  };
  auto match = [&](u32 distance, u32 length) -> bool {
    if(!distance || distance > made || length > outSize - made) return false;
    for(u32 n = 0; n < length; n++, made++) out[made] = out[made - distance];
    return true;
  };

  if(inSize && in[0] > 17) {
    u32 count = in[at++] - 17;
    if(!literals(count)) return 0;
    state = count < 4 ? count : 4;
  }
  while(true) {
    u32 code = 0, high = 0, length = 0, distance = 0;
    if(!byte(code)) return 0;
    if(code < 16 && state == 0) {
      if(!(length = code) && !more(15, length)) return 0;
      if(!literals(length + 3)) return 0;
      state = 4;
      continue;
    }
    if(code < 16) {
      if(!byte(high)) return 0;
      length = state < 4 ? 2 : 3;
      distance = (high << 2) + (code >> 2 & 3) + (state < 4 ? 1 : 2049);
    } else if(code < 64) {
      bool far = code < 32;  //0001HLLL rather than 001LLLLL
      u32 bits = far ? 7 : 31;
      if(!(length = code & bits) && !more(bits, length)) return 0;
      length += 2;
      u32 low = 0;
      if(!byte(low) || !byte(high)) return 0;
      u32 d = (high << 8 | low) >> 2;
      if(far) {
        d += (code & 8) << 11;
        if(!d) return made;  //the end
        distance = d + 16384;
      } else {
        distance = d + 1;
      }
      code = low;  //its low two bits are the literals after
    } else {
      if(!byte(high)) return 0;
      length = code < 128 ? 3 + (code >> 5 & 1) : 5 + (code >> 5 & 3);
      distance = (high << 3) + (code >> 2 & 7) + 1;
    }
    state = code & 3;
    if(!match(distance, length) || !literals(state)) return 0;
  }
}
