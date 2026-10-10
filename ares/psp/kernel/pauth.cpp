//scePauth: data a game keeps encrypted, decrypted with a key the game gives. The data is the body of a ~PSP file
//(decrypt.cpp) without its "~PSP" header in the clear: 0x150 bytes whose first 0x80 are whatever the file holds, its
//hidden pieces and tag where a ~PSP header has them, then the data as KIRK command 1's. Its tags are type 5's, the
//ones keyed with the PSP Developer Wiki's "scePauth XOR Key" (keys.cpp), and the game's 16-byte key is mixed into
//type 5's steps as a second key: XORed into the pad once it's decrypted, and into the XOR key for the first 0x50
//bytes. Worked out from Monster Hunter Portable 3rd's call (tag 0x2fd313f0): of the ways a second key could enter
//type 5's steps, that is the one whose SHA-1 digest checks out, and its data then decrypts to a Capcom texture
//(".TMH0.14"). Nothing else in the steps changes, so a digest that checks is the proof a key was right.

//The decryption, as decryptProgram() does a program's: the header unlocked with the tag's key and the given one,
//then KIRK command 1 on the rebuilt header, the first 0x80 bytes and the data from 0x150.
static auto decryptPauth(const u8* data, u32 size, const u8 key[16], std::vector<u8>& plain) -> bool {
  u32 tag = LockedHeader::word(data + 0xd0);
  u8 kirk[0x90];
  bool unlocked = false;
  for(auto& entry : Keys::seedTags) {
    if(unlocked || entry.tag != tag || entry.type != 5) continue;
    LockedHeader header{data};
    if(header.unlock(entry, key)) unlocked = true, memcpy(kirk, header.kirk, sizeof(kirk));
  }
  if(!unlocked) return false;
  u32 dataSize = LockedHeader::word(kirk + 0x70), padding = LockedHeader::word(kirk + 0x74);
  if(padding != 0x80 || !dataSize || 0x150 + ((u64(dataSize) + 15) & ~15ull) > size) return false;
  std::vector<u8> input(0x90 + 0x80 + (size - 0x150));
  memcpy(input.data(), kirk, 0x90);
  memcpy(input.data() + 0x90, data, 0x80);
  memcpy(input.data() + 0x110, data + 0x150, size - 0x150);
  plain.assign(dataSize, 0);
  return Kirk::decryptPrivate(plain.data(), dataSize, input.data(), input.size()) == Kirk::Success;
}

//(data, size, where to put the decrypted size, key): the data decrypted where it is; 0, the size written. Monster
//Hunter Portable 3rd copies that many bytes back from its buffer whatever the result, and marks the data bad when
//the result isn't 0. Data that won't decrypt (cut short, a tag that isn't scePauth's, a digest that doesn't check)
//is left as it was, the size not written: SCE_ERROR_INVALID_VALUE then (chosen: no recording tells the error). The
//PSP's KIRK checks the data's own CMAC too; Phobos doesn't, as for programs (kirk.cpp).
auto Kernel::scePauth_98B83B5D() -> void {
  u32 data = arg(0), size = arg(1), decryptedSize = arg(2), keyAddress = arg(3);
  if(size < 0x150 || size > 64_MiB || !memory.reaches(data, size) || !memory.reaches(keyAddress, 16)) {
    return result(ErrorInvalidValue);
  }
  std::vector<u8> bytes(size);
  memory.copyOut(bytes.data(), data, size);
  u8 key[16];
  memory.copyOut(key, keyAddress, 16);
  std::vector<u8> plain;
  if(!decryptPauth(bytes.data(), size, key, plain)) return result(ErrorInvalidValue);
  memory.copyIn(data, plain.data(), plain.size());
  if(memory.reaches(decryptedSize, 4)) memory.write(4, decryptedSize, plain.size());
  result(0);
}
