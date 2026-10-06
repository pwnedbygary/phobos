#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nall/decode/inflate.hpp>

//A UMD, as an image of it: an ISO (the disc's 2048-byte sectors one after another), one of the PSP homebrew scene's
//compressed forms of it (CSO, ZSO, DAX, JSO: the same bytes, packed a block at a time), and the ISO 9660 file system
//on it. (A CHD, MAME's form, is read by the system: system/chd.cpp, which hands its bytes on as an ISO's.)
//
//The image's bytes come through a Reader the system gives (a file on the host, or on Android the descriptor of a
//file the app was given), so a disc of a gigabyte or two is read a little at a time as the game asks, never copied.
//
//ISO 9660 (the CD-ROM file system, ECMA-119), as a PSP's discs use it: sector 16 is the primary volume descriptor
//("CD001", and on a PSP's disc "PSP GAME"), which holds the root folder's directory record. A folder is a run of
//sectors full of directory records, none crossing from one sector into the next (a record of length 0 means "the
//rest of this sector is empty"). Each record says where its file or folder starts (its first sector, the logical
//block number or LBA), how many bytes it has, whether it's a folder, when it was recorded, and its name: a file's
//ends in ";1", a version number every file carries, and the records for the folder itself and its parent are named
//with the bytes 0 and 1. A PSP's discs name everything in capitals, but the PSP finds names whatever their case.
//
//The compressed forms (each read here, unpacked a block at a time as reads come):
//  - CSO (its layout is the one ciso and maxcso write): a 24-byte header ("CISO", the header's size, the disc's size
//    in bytes as a 64-bit number, the block size (usually 2048), a version and an alignment shift), then one 32-bit
//    index entry per block and one more for the end. An entry's low 31 bits, shifted left by the alignment, are where
//    the block's data starts in the file; it runs to the next entry's start. Version 1 packs blocks with deflate
//    (raw, as zlib's inflate with no header reads it), or stores them as they are when the entry's top bit is set.
//    Version 2 stores a block that takes a whole block's room, and packs the others with deflate, or with LZ4 when
//    the top bit is set. Encoders pad the last block out to a whole one.
//  - ZSO: a CSO version 1's layout, "ZISO", packed with LZ4 rather than deflate.
//  - DAX: a 32-byte header ("DAX\0", the disc's size, the version, how many uncompressed areas), each 8 KiB frame's
//    start (32 bits) and packed size (16 bits), then, from version 1, the uncompressed areas (each its first frame
//    and how many frames). The other frames are packed with deflate in zlib's wrapping.
//  - JSO: a 48-byte header ("JISO", the block size at 6, whether blocks have headers at 8, the packing at 10 (0 LZO,
//    1 zlib), the disc's size at 12), then each block's start and one more for the end. A block that takes a whole
//    block's room is stored as it is.
//LZ4 and LZO are unpacked by unpack.cpp, deflate by nall's inflate.

namespace ares::PlayStationPortable {

struct Disc {
  static constexpr u32 SectorSize = 2048;

  //A file or folder on the disc.
  struct Entry {
    std::string name;     //without its ";1"
    u32 sector = 0;       //where it starts
    u32 size = 0;         //how many bytes (a folder's: its records' sectors)
    bool folder = false;
    u8 date[7] = {};      //when it was recorded: years since 1900, month, day, hour, minute, second, time zone
  };

  //Reads size bytes from offset in the image into data; returns how many it got.
  using Reader = std::function<auto (u64 offset, void* data, u64 size) -> u64>;

  auto open(Reader reader, u64 imageSize, std::string& error) -> bool;
  auto size() const -> u64 { return discSize; }  //in bytes: a trimmed image may end part way into its last sector
  auto sectors() const -> u32 { return sectorCount; }  //the whole ones
  auto readSectors(u32 first, u32 count, u8* data) -> bool;
  auto read(u64 offset, u64 size, u8* data) -> bool;  //bytes anywhere on the disc
  auto root() const -> const Entry& { return rootEntry; }
  auto find(const std::vector<std::string>& names, Entry& entry) -> bool;  //a path's names, from the root
  auto list(const Entry& folder) -> std::vector<Entry>;                    //its entries, as the disc orders them

private:
  Reader reader;
  u64 discSize = 0;      //in bytes, unpacked
  u32 sectorCount = 0;
  Entry rootEntry;

  //a compressed image's
  enum class Format : u32 { ISO, CSO, ZSO, DAX, JSO } format = Format::ISO;
  u32 blockSize = 0;
  u32 alignment = 0;          //a CSO's or a ZSO's index shift
  u32 version = 0;            //a CSO's: 0 or 1, or 2
  bool lzo = false;           //a JSO's blocks are packed with LZO, else with zlib
  std::vector<u32> index;     //where each block starts (a CSO's or a ZSO's with its flag bit)
  std::vector<u16> sizes;     //a DAX's frames' packed sizes
  std::vector<bool> plainFrames;  //a DAX's frames stored as they are (its uncompressed areas)
  s64 cachedBlock = -1;       //the last block unpacked, as reads come in runs
  std::vector<u8> block, packed;

  auto openCSO(const u8* header, u64 imageSize, std::string& error) -> bool;
  auto openDAX(const u8* header, u64 imageSize, std::string& error) -> bool;
  auto openJSO(const u8* header, u64 imageSize, std::string& error) -> bool;
  auto plainFrame(u32 number) const -> bool;
  auto readBlock(u32 number) -> bool;
  auto readImage(u64 offset, u64 size, u8* data) -> bool;
  static auto record(const u8* bytes, Entry& entry) -> bool;
};

//Unpack a block packed with LZ4 (its block form) or LZO (LZO1X), into out's outSize bytes; each returns how many
//bytes came out, or 0 if the packed bytes are damaged (unpack.cpp).
auto unpackLZ4(const u8* in, u32 inSize, u8* out, u32 outSize) -> u32;
auto unpackLZO(const u8* in, u32 inSize, u8* out, u32 outSize) -> u32;

}
