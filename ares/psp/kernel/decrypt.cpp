//The ~PSP format, in which Sony's retail programs come: a disc's EBOOT.BIN, and most of the modules (PRXs) a game
//loads. docs/psp-core.md, part 18, sets out every step; this follows it.
//
//A ~PSP file is a 0x150-byte header, then the program, encrypted as the data of KIRK's command 1 (kirk.cpp). That
//command needs a 0x90-byte header of its own, which holds the program's key: the ~PSP header carries its pieces,
//hidden by one of a few schemes (the type) under a key the file's tag names (keys.cpp), along with a SHA-1 digest
//to check them by. Decrypting a program is: find the tag's key, undo the type's hiding, check the digest, have KIRK
//decrypt the program, and unpack it if it was packed.

//A ~PSP header being unlocked: a copy of the file's first 0x150 bytes, whose hidden pieces are decrypted where they
//are, and KIRK command 1's header as it's rebuilt from them.
struct LockedHeader {
  struct Piece { u32 offset, size; };  //a run of the header's bytes

  u8 bytes[0x150];
  u8 kirk[0x90] = {};

  explicit LockedHeader(const u8* file) {
    memcpy(bytes, file, sizeof(bytes));
  }

  static auto word(const u8* at) -> u32 {
    return at[0] | at[1] << 8 | at[2] << 16 | u32(at[3]) << 24;
  }

  auto zeros(u32 from, u32 to) const -> bool {
    return std::all_of(bytes + from, bytes + to, [](u8 byte) { return byte == 0; });
  }

  //The pieces' bytes, one after another.
  auto gather(std::initializer_list<Piece> pieces) const -> std::vector<u8> {
    std::vector<u8> run;
    for(auto piece : pieces) run.insert(run.end(), bytes + piece.offset, bytes + piece.offset + piece.size);
    return run;
  }

  //Pieces decrypted together with KIRK command 7, as one run in the order given (first XORed with xorKey over and
  //over, when there is one), and put back where they were.
  auto decrypt(std::initializer_list<Piece> pieces, u8 keyseed, const u8* xorKey = nullptr) -> bool {
    auto run = gather(pieces);
    if(xorKey) for(u32 n = 0; n < run.size(); n++) run[n] ^= xorKey[n % 16];
    if(!Kirk::decryptInPlace(run.data(), run.size(), keyseed)) return false;
    u32 at = 0;
    for(auto piece : pieces) memcpy(bytes + piece.offset, run.data() + at, piece.size), at += piece.size;
    return true;
  }

  //Whether the SHA-1 digest of these bytes is the 20 at offset.
  auto digestIs(const std::vector<u8>& hashed, u32 offset) const -> bool {
    auto digest = sha1(hashed.data(), hashed.size());
    return !memcmp(digest.data(), bytes + offset, digest.size());
  }

  //KIRK's header's first size bytes, as both kinds of key hide them: XORed with some of the key's bytes, decrypted
  //with KIRK command 7, XORed with others.
  auto unscramble(u32 size, const u8* before, const u8* after, u8 keyseed) -> bool {
    for(u32 n = 0; n < size; n++) kirk[n] ^= before[n];
    if(!Kirk::decryptInPlace(kirk, size, keyseed)) return false;
    for(u32 n = 0; n < size; n++) kirk[n] ^= after[n];
    return true;
  }

  //Types 0 and 1, with a 144-byte key. KIRK's header is stored in two pieces, its first 0x40 bytes at 0x110 and its
  //last 0x50 at 0x80, its first 0x70 bytes hidden. The digest at 0xd4 checks the key's first 0x14 bytes, 0xe8-0x110,
  //the header's pieces and the file's first 0x80 bytes. Type 1 first decrypts the 0xa0 bytes at 0xe0-0x150 and
  //0x80-0xb0 (the digest's end among them).
  auto unlock(const PadTag& tag) -> bool {
    u8 pad[144];
    for(u32 n = 0; n < 144; n++) pad[n] = u8(tag.pad[n / 4] >> n % 4 * 8);
    if(tag.type == 1 && !decrypt({{0xe0, 0x70}, {0x80, 0x30}}, tag.keyseed)) return false;
    auto hashed = gather({{0xe8, 0x28}, {0x110, 0x40}, {0x80, 0x50}, {0x00, 0x80}});
    hashed.insert(hashed.begin(), pad, pad + 0x14);
    if(!digestIs(hashed, 0xd4)) return false;
    memcpy(kirk, bytes + 0x110, 0x40);
    memcpy(kirk + 0x40, bytes + 0x80, 0x50);
    return unscramble(0x70, pad + 0x14, pad + 0x20, tag.keyseed);
  }

  //Types 2, 5 and 6, with a 16-byte key, spread first into a 0x90-byte pad: nine copies of it, copy n's first byte
  //n, decrypted as one. KIRK's header's first 0x40 bytes are stored, hidden, at 0x80-0xb0 and 0xc0-0xd0, its sizes
  //at 0xb0; the rest of it is zeros but for 1, the command, at 0x60. The 0x60 bytes at 0x140-0x150 (an ID),
  //0x12c-0x140 (the digest), 0x80-0xb0 and 0xc0-0xcc are decrypted as one; then the digest checks the tag, the pad's
  //first 16 bytes, 0xd4-0x12c, the ID, the header's stored pieces, its sizes and the file's first 0x80 bytes. Type 6
  //keeps the end of an ECDSA signature at 0x10c-0x12c, where type 2 has zeros, and is hashed the same way. Type 5
  //XORs a key of its own into what it decrypts, decrypts 0x50 bytes first, and counts 0xd4-0x12c as zeros; given a
  //second 16-byte key (scePauth's: pauth.cpp), it XORs that into the pad once the pad is decrypted, and into its own
  //key for the first 0x50 bytes.
  auto unlock(const SeedTag& tag, const u8* second = nullptr) -> bool {
    u8 pad[0x90];
    for(u32 n = 0; n < 9; n++) memcpy(pad + n * 16, tag.seed, 16), pad[n * 16] = n;
    if(!Kirk::decryptInPlace(pad, sizeof(pad), tag.keyseed)) return false;
    if(second) for(u32 n = 0; n < sizeof(pad); n++) pad[n] ^= second[n % 16];
    const u8* xorKey = tag.type == 5 ? tag.xorKey : nullptr;
    if(tag.type == 5) {
      if(!xorKey || !zeros(0xd5, 0x12c)) return false;
      u8 firstKey[16];
      for(u32 n = 0; n < 16; n++) firstKey[n] = xorKey[n] ^ (second ? second[n] : 0);
      if(!decrypt({{0x80, 0x30}, {0xc0, 0x10}, {0x12c, 0x10}}, tag.keyseed, firstKey)) return false;
    } else if(!zeros(0xd4, 0x10c)) {
      return false;
    }
    if(!decrypt({{0x140, 0x10}, {0x12c, 0x14}, {0x80, 0x30}, {0xc0, 0x0c}}, tag.keyseed, xorKey)) return false;
    auto hashed = gather({{0xd0, 0x04}});
    hashed.insert(hashed.end(), pad, pad + 0x10);
    auto between = gather({{0xd4, 0x58}});
    if(tag.type == 5) std::fill(between.begin(), between.end(), 0);
    hashed.insert(hashed.end(), between.begin(), between.end());
    auto rest = gather({{0x140, 0x10}, {0x80, 0x30}, {0xc0, 0x10}, {0xb0, 0x10}, {0x00, 0x80}});
    hashed.insert(hashed.end(), rest.begin(), rest.end());
    if(!digestIs(hashed, 0x12c)) return false;
    memcpy(kirk, bytes + 0x80, 0x30);
    memcpy(kirk + 0x30, bytes + 0xc0, 0x10);
    kirk[0x60] = 1;
    memcpy(kirk + 0x70, bytes + 0xb0, 0x10);
    return unscramble(0x40, pad + 0x10, pad + 0x50, tag.keyseed);
  }
};

//gzip's wrapping (RFC 1952) around a deflate stream: a 10-byte header (0x1f, 0x8b, 8 for deflate, flags, a time,
//more flags, the system), then what the flags add (4: extra bytes after their 2-byte size; 8: a name and 16: a
//comment, each ending in a zero byte; 2: a 2-byte CRC of the header), the stream, and the unpacked bytes' CRC-32 and
//size, 4 bytes each, little-endian. The program must unpack to exactly size bytes, and match both. (No program is
//bigger than the PSP's memory.)
static auto unpackGzip(const std::vector<u8>& packed, u32 size, std::vector<u8>& program) -> bool {
  u64 at = 10, end = packed.size();
  if(end < 18 || packed[0] != 0x1f || packed[1] != 0x8b || packed[2] != 8 || size > 64_MiB) return false;
  u8 flags = packed[3];
  if(flags & 4) {
    if(at + 2 > end) return false;
    at += 2 + (packed[at] | packed[at + 1] << 8);
  }
  for(u8 text : {8, 16}) {
    if(!(flags & text)) continue;
    while(at < end && packed[at]) at++;
    at++;
  }
  if(flags & 2) at += 2;
  if(at >= end) return false;
  program.assign(size, 0);
  u32 made = size, used = end - at;
  auto stream = const_cast<u8*>(packed.data() + at);  //puff only reads it
  if(nall::Decode::puff::puff(program.data(), &made, stream, &used) != 0 || made != size) return false;
  at += used;
  if(at + 8 > end) return false;
  u32 crc = LockedHeader::word(&packed[at]), length = LockedHeader::word(&packed[at + 4]);
  return length == size && nall::Hash::CRC32({program.data(), program.size()}).value() == crc;
}

auto encryptedProgram(const u8* data, u64 size) -> bool {
  return size >= 4 && !memcmp(data, "~PSP", 4);
}

//Some programs come with another header of Sony's before their ~PSP one: "~SCE", its own length at 4 (64 bytes in
//GTA Liberty City Stories' modules). Nothing in it is needed: it's passed over.
auto unwrapProgram(const u8*& data, u64& size) -> void {
  if(size < 8 || memcmp(data, "~SCE", 4)) return;
  u32 length = LockedHeader::word(data + 4);
  if(length >= 8 && length <= size) data += length, size -= length;
}

//The program in a ~PSP file: its header unlocked with the key its tag names (each, for a tag listed twice, until one
//checks out), then KIRK command 1 on the rebuilt header, the file's first 0x80 bytes (the padding the header asks
//for) and the file from 0x150 (the program), then unpacked if the header (0x06) says it was packed.
auto decryptProgram(const u8* data, u64 size, std::vector<u8>& program) -> std::string {
  program.clear();
  if(!encryptedProgram(data, size)) return "not an encrypted program";
  if(size < 0x150) return "an encrypted program cut short in its header";
  if(size > 0xffff'0000) return "an encrypted program too big to be one";
  std::string name;
  for(u32 at = 0x0a; at < 0x0a + 28 && data[at]; at++) name.push_back(char(data[at]));
  u32 tag = LockedHeader::word(data + 0xd0);
  char text[96];
  std::snprintf(text, sizeof(text), "an encrypted program (module \"%s\", tag 0x%08x) ", name.c_str(), tag);
  std::string about = text;

  u8 kirk[0x90];
  bool known = false, unlocked = false;
  auto tryKeys = [&](auto& table) {
    for(auto& entry : table) {
      if(unlocked || entry.tag != tag) continue;
      known = true;
      LockedHeader header{data};
      if(header.unlock(entry)) unlocked = true, memcpy(kirk, header.kirk, sizeof(kirk));
    }
  };
  tryKeys(Keys::padTags);
  tryKeys(Keys::seedTags);
  if(!known) return about + "whose tag names a key Phobos doesn't have";
  if(!unlocked) {
    return about + "whose header doesn't check out with its tag's key (it's damaged, or of a type Phobos doesn't "
                   "read)";
  }

  u32 dataSize = LockedHeader::word(kirk + 0x70), padding = LockedHeader::word(kirk + 0x74);
  if(padding != 0x80) return about + "whose program isn't where a ~PSP file keeps it";
  if(!dataSize || 0x150 + ((u64(dataSize) + 15) & ~15ull) > size) {
    return about + "whose program runs past the end of the file";
  }
  std::vector<u8> input(0x90 + 0x80 + (size - 0x150));
  memcpy(input.data(), kirk, 0x90);
  memcpy(input.data() + 0x90, data, 0x80);
  memcpy(input.data() + 0x110, data + 0x150, size - 0x150);
  std::vector<u8> decrypted(dataSize);
  if(u32 error = Kirk::decryptPrivate(decrypted.data(), dataSize, input.data(), input.size())) {
    return about + "that KIRK refuses (error " + std::to_string(error) + ")";
  }

  u32 packing = data[6] | data[7] << 8;
  if(!(packing & 1)) {
    program = std::move(decrypted);
    return {};
  }
  if(u32 method = packing >> 8 & 0xf) {
    if(method == 1) return about + "packed with 2RLZ, which Phobos doesn't unpack";
    if(method == 2) return about + "packed with KL4E, which Phobos doesn't unpack";
    return about + "packed in a way Phobos doesn't know (" + std::to_string(method) + ")";
  }
  if(!unpackGzip(decrypted, LockedHeader::word(data + 0x28), program)) {
    program.clear();
    return about + "whose packing (gzip) is damaged";
  }
  return {};
}
