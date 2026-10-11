#pragma once

#include <array>
#include <string>

namespace ares::PlayStationPortable {

//PGD (Protected Game Data): the format a game's UMD data files, and its NPDRM (download) EDATA files, come in to
//keep their data encrypted. The PSP decrypts them as they're read, in amctrl.prx's two routines: BBMac, the MAC that
//ties a file to its game, and BBCipher, the counter cipher that turns each 16-byte block into a keystream. Both are
//built on AES-128 with three keys from KIRK's vault (0x38, 0x39, 0x63); the header's other constants are amctrl's
//own, reverse engineered by "tpu" and documented on the PSP Developer Wiki (part 18, and the part that follows).
//
//A PGD file is a 0x90-byte header, then its data. In the header: 0x10 a key in the clear; 0x30 an encrypted
//descriptor; 0x70 and 0x80 the file's two MACs. The descriptor, decrypted, gives the data's key, its size and where
//it starts; the data, from there on, is BBCiphered. The whole thing is unlocked with the version key the game gives
//(sceIoIoctl's 0x04100001 for a UMD file, an NPDRM file's licensee key).

struct Pgd {

  //Whether `file` starts with the magic: 0, 'P', 'G', 'D'.
  static auto is(const u8* file) -> bool {
    return memcmp(file, "\0PGD", 4) == 0;
  }

  //A header's decrypted descriptor: the data's key, its size, the chunk it's decrypted in, and where it starts.
  struct Descriptor {
    u8 dataKey[16];
    u32 dataSize, blockSize, dataOffset;
  };

  //A file's header descriptor (0x30 bytes at 0x30), decrypted with the version key the game gave. The file is checked
  //against its two MACs first (0x80 with a fixed DNAS key, 0x70 with the version key); why it can't be, or nothing.
  static auto descriptor(const u8* file, u64 size, const u8 versionKey[16], Descriptor& out) -> std::string;

  //`size` bytes of a PGD file's data, at region-relative `offset` (bytes after the data starts), decrypted in place,
  //the data key and version key given. BBCipher is one keystream over the whole region, so any bytes of it decrypt on
  //their own, aligned to its 16-byte blocks or not.
  static auto decryptData(u8* data, u32 offset, u32 size, const u8 dataKey[16], const u8 versionKey[16]) -> void;
};

}
