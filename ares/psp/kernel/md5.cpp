//MD5, as RFC 1321 defines it, its state in the program's memory: the kernel's (UtilsForUser's sceKernelUtilsMd5Digest,
//BlockInit, BlockUpdate and BlockResult, a SceKernelUtilsMd5Context of psputils.h) and Sony's library of its own
//(sceMd5, libmd5.prx, which God Eater 2 carries and the kernel stands in for: its update names the files of its
//downloaded content by MD5 digests, and asserts if it can't open the one it worked out).
//
//The context, 96 bytes, as pspautotests' hash/md5ctx recorded the kernel's: the four chaining words; a word left as
//it was; the bytes waiting for a whole 64-byte block to be stirred in (16 bits), and 16 bits more, 0; the bytes taken
//in all (64 bits); then the waiting bytes, where each block goes before it's stirred in (a whole 64 bytes taken
//leaves them there, none waiting). Starting writes only the first 32 bytes. Finishing leaves the context as it was
//(chosen: no recording shows it after BlockResult). The library's context is taken to be the same (chosen: no
//recording shows it; the digests are MD5's either way). A context or data the program's memory doesn't hold is
//refused, ILLEGAL_ADDR (chosen).

namespace {

constexpr u32 Md5Start[4] = {0x6745'2301, 0xefcd'ab89, 0x98ba'dcfe, 0x1032'5476};

//RFC 1321's sine table (each the integer part of 2^32 * |sin(i + 1)|) and each round's shifts.
constexpr u32 Md5Sines[64] = {
  0xd76a'a478, 0xe8c7'b756, 0x2420'70db, 0xc1bd'ceee, 0xf57c'0faf, 0x4787'c62a, 0xa830'4613, 0xfd46'9501,
  0x6980'98d8, 0x8b44'f7af, 0xffff'5bb1, 0x895c'd7be, 0x6b90'1122, 0xfd98'7193, 0xa679'438e, 0x49b4'0821,
  0xf61e'2562, 0xc040'b340, 0x265e'5a51, 0xe9b6'c7aa, 0xd62f'105d, 0x0244'1453, 0xd8a1'e681, 0xe7d3'fbc8,
  0x21e1'cde6, 0xc337'07d6, 0xf4d5'0d87, 0x455a'14ed, 0xa9e3'e905, 0xfcef'a3f8, 0x676f'02d9, 0x8d2a'4c8a,
  0xfffa'3942, 0x8771'f681, 0x6d9d'6122, 0xfde5'380c, 0xa4be'ea44, 0x4bde'cfa9, 0xf6bb'4b60, 0xbebf'bc70,
  0x289b'7ec6, 0xeaa1'27fa, 0xd4ef'3085, 0x0488'1d05, 0xd9d4'd039, 0xe6db'99e5, 0x1fa2'7cf8, 0xc4ac'5665,
  0xf429'2244, 0x432a'ff97, 0xab94'23a7, 0xfc93'a039, 0x655b'59c3, 0x8f0c'cc92, 0xffef'f47d, 0x8584'5dd1,
  0x6fa8'7e4f, 0xfe2c'e6e0, 0xa301'4314, 0x4e08'11a1, 0xf753'7e82, 0xbd3a'f235, 0x2ad7'd2bb, 0xeb86'd391,
};
constexpr u8 Md5Shifts[4][4] = {{7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}};

//One 64-byte block stirred into the four chaining words: four rounds of 16 steps, each round with its own mix of the
//other three words and its own order through the block's 16 words (little-endian).
auto md5Block(u32 h[4], const u8* block) -> void {
  u32 x[16];
  for(u32 n = 0; n < 16; n++) {
    x[n] = block[n * 4] | block[n * 4 + 1] << 8 | block[n * 4 + 2] << 16 | u32(block[n * 4 + 3]) << 24;
  }
  u32 a = h[0], b = h[1], c = h[2], d = h[3];
  for(u32 step = 0; step < 64; step++) {
    u32 round = step / 16, mixed, word;
    if(round == 0)      mixed = (b & c) | (~b & d), word = step;
    else if(round == 1) mixed = (b & d) | (c & ~d), word = (5 * step + 1) % 16;
    else if(round == 2) mixed = b ^ c ^ d,          word = (3 * step + 5) % 16;
    else                mixed = c ^ (b | ~d),       word = 7 * step % 16;
    u32 turned = b + std::rotl(a + mixed + x[word] + Md5Sines[step], Md5Shifts[round][step % 4]);
    a = d, d = c, c = b, b = turned;
  }
  h[0] += a, h[1] += b, h[2] += c, h[3] += d;
}

//The last bytes (fewer than 64) padded as RFC 1321 asks (a 1 bit, zeros, then the bits taken in all as a 64-bit
//number, low byte first) and stirred in; the digest is the chaining words then, low byte first.
auto md5Finish(u32 h[4], const u8* last, u32 count, u64 taken, u8 digest[16]) -> void {
  u8 tail[128] = {};
  if(count) memcpy(tail, last, count);  //(memcpy wants a real pointer even for no bytes; an empty message's may be 0)
  tail[count] = 0x80;
  u32 length = count < 56 ? 64 : 128;
  for(u32 n = 0; n < 8; n++) tail[length - 8 + n] = u8(taken * 8 >> n * 8);
  for(u32 at = 0; at < length; at += 64) md5Block(h, tail + at);
  for(u32 n = 0; n < 16; n++) digest[n] = u8(h[n / 4] >> n % 4 * 8);
}

}

//(context): started: the chaining words, nothing waiting, nothing taken.
auto Kernel::md5Init(u32 context) -> u32 {
  if(!memory.reaches(context, 96)) return ErrorIllegalAddress;
  for(u32 n = 0; n < 4; n++) memory.write(4, context + n * 4, Md5Start[n]);
  for(u32 at = 20; at < 32; at += 4) memory.write(4, context + at, 0);
  return 0;
}

//(context, data, size): the bytes taken, a block stirred in each time the waiting ones make 64.
auto Kernel::md5Update(u32 context, u32 data, u32 size) -> u32 {
  if(!memory.reaches(context, 96) || (size && !memory.reaches(data, size))) return ErrorIllegalAddress;
  u32 h[4];
  for(u32 n = 0; n < 4; n++) h[n] = memory.read(4, context + n * 4);
  u32 waiting = memory.read(2, context + 20) % 64;
  u64 taken = memory.read(4, context + 24) | u64(memory.read(4, context + 28)) << 32;
  u8 block[64];
  memory.copyOut(block, context + 32, 64);
  for(u32 at = 0; at < size;) {
    u32 count = std::min(size - at, 64 - waiting);
    memory.copyOut(block + waiting, data + at, count);
    memory.copyIn(context + 32 + waiting, block + waiting, count);
    waiting += count, at += count;
    if(waiting == 64) md5Block(h, block), waiting = 0;
  }
  taken += size;
  for(u32 n = 0; n < 4; n++) memory.write(4, context + n * 4, h[n]);
  memory.write(2, context + 20, waiting);
  memory.write(4, context + 24, u32(taken));
  memory.write(4, context + 28, u32(taken >> 32));
  return 0;
}

//(context, where to put the digest): the digest of all the bytes taken.
auto Kernel::md5Result(u32 context, u32 digest) -> u32 {
  if(!memory.reaches(context, 96) || !memory.reaches(digest, 16)) return ErrorIllegalAddress;
  u32 h[4];
  for(u32 n = 0; n < 4; n++) h[n] = memory.read(4, context + n * 4);
  u32 waiting = memory.read(2, context + 20) % 64;
  u64 taken = memory.read(4, context + 24) | u64(memory.read(4, context + 28)) << 32;
  u8 last[64], made[16];
  memory.copyOut(last, context + 32, waiting);
  md5Finish(h, last, waiting, taken, made);
  memory.copyIn(digest, made, 16);
  return 0;
}

//(data, size, where to put the digest): the digest of the data, at once.
auto Kernel::md5Digest(u32 data, u32 size, u32 digest) -> u32 {
  if((size && !memory.reaches(data, size)) || !memory.reaches(digest, 16)) return ErrorIllegalAddress;
  std::vector<u8> bytes(size);
  memory.copyOut(bytes.data(), data, size);
  u32 h[4] = {Md5Start[0], Md5Start[1], Md5Start[2], Md5Start[3]};
  u32 whole = size / 64 * 64;
  for(u32 at = 0; at < whole; at += 64) md5Block(h, bytes.data() + at);
  u8 made[16];
  md5Finish(h, bytes.data() + whole, size - whole, size, made);
  memory.copyIn(digest, made, 16);
  return 0;
}

auto Kernel::sceKernelUtilsMd5Digest() -> void { result(md5Digest(arg(0), arg(1), arg(2))); }
auto Kernel::sceKernelUtilsMd5BlockInit() -> void { result(md5Init(arg(0))); }
auto Kernel::sceKernelUtilsMd5BlockUpdate() -> void { result(md5Update(arg(0), arg(1), arg(2))); }
auto Kernel::sceKernelUtilsMd5BlockResult() -> void { result(md5Result(arg(0), arg(1))); }

//The library's, as the kernel's: (data, size, digest), (context), (context, data, size), (context, digest).
auto Kernel::sceMd5Digest() -> void { result(md5Digest(arg(0), arg(1), arg(2))); }
auto Kernel::sceMd5BlockInit() -> void { result(md5Init(arg(0))); }
auto Kernel::sceMd5BlockUpdate() -> void { result(md5Update(arg(0), arg(1), arg(2))); }
auto Kernel::sceMd5BlockResult() -> void { result(md5Result(arg(0), arg(1))); }
