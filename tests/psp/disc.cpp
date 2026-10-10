//The disc (ares/psp/kernel: disc.cpp, io.cpp's disc paths, umd.cpp): disc images made here (disc-image.hpp), read as
//an ISO and as CSOs; their files through the kernel, every way games read them; and the drive's state.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Disc;
constexpr u32 Buffer = 0x0893'0000, Stat = 0x0894'0000, In = 0x0894'1000, Out = 0x0894'2000;

//Bytes that differ from their neighbours', so a read from the wrong place shows.
static auto pattern(u32 size, u32 seed) -> std::vector<u8> {
  std::vector<u8> bytes(size);
  u32 x = seed * 2654435761u + 1;
  for(auto& byte : bytes) x = x * 1103515245 + 12345, byte = u8(x >> 16);
  return bytes;
}

//The test disc: a game's folders, a program, data three and a half sectors long, an empty file.
static auto testDisc() -> disc_image::Image {
  return disc_image::makeIso({
    {"PSP_GAME/PARAM.SFO", pattern(100, 1)},
    {"PSP_GAME/SYSDIR/EBOOT.BIN", pattern(5000, 2)},
    {"PSP_GAME/USRDIR/DATA.BIN", pattern(7 * 1024, 3)},
    {"PSP_GAME/USRDIR/EMPTY.BIN", {}},
    {"UMD_DATA.BIN", pattern(32, 4)},
  });
}

//A Disc reading an image from memory, as the system reads one from a file.
static auto openDisc(const std::vector<u8>& bytes, std::string& error) -> std::shared_ptr<Disc> {
  auto disc = std::make_shared<Disc>();
  auto data = std::make_shared<std::vector<u8>>(bytes);
  bool opened = disc->open([data](u64 offset, void* out, u64 size) -> u64 {
    if(offset >= data->size()) return 0;
    size = std::min<u64>(size, data->size() - offset);
    memcpy(out, data->data() + offset, size);
    return size;
  }, bytes.size(), error);
  return opened ? disc : nullptr;
}

static auto names(const std::vector<Disc::Entry>& entries) -> std::string {
  std::string list;
  for(auto& entry : entries) list += (list.empty() ? "" : " ") + entry.name + (entry.folder ? "/" : "");
  return list;
}

//The reader, on the ISO and on CSOs: a small block, big blocks with the index shifted, and version 2.
static auto discImages() -> void {
  auto image = testDisc();
  auto data = pattern(7 * 1024, 3);
  struct Kind { const char* name; std::vector<u8> bytes; };
  std::vector<Kind> kinds = {
    {"ISO", image.bytes},
    {"CSO", disc_image::makeCso(image.bytes)},
    {"CSO of 16 KiB blocks, index shifted 2", disc_image::makeCso(image.bytes, 16_KiB, 2)},
    {"CSO version 2", disc_image::makeCso(image.bytes, 2048, 0, 2)},
  };
  //an image that ends part way into a 16 KiB block, whose last block the packer pads, as maxcso does
  auto odd = disc_image::makeIso({
    {"PSP_GAME/USRDIR/DATA.BIN", data},
    {"PSP_GAME/USRDIR/MORE.BIN", pattern(3000, 5)},
  });
  for(u32 version : {1u, 2u}) {
    std::string error;
    auto disc = openDisc(disc_image::makeCso(odd.bytes, 16_KiB, 0, version), error);
    CHECK(disc != nullptr && disc->sectors() == odd.bytes.size() / 2048 && odd.bytes.size() % 16_KiB != 0, true);
    std::vector<u8> last(3000);
    if(disc) CHECK(disc->read(u64(odd.sectors["PSP_GAME/USRDIR/MORE.BIN"]) * 2048, 3000, last.data()), true);
    CHECK(last == pattern(3000, 5), true);
  }
  for(auto& kind : kinds) {
    std::string error;
    auto disc = openDisc(kind.bytes, error);
    CHECK(disc != nullptr, true);
    if(!disc) { std::printf("  %s: %s\n", kind.name, error.c_str()); continue; }
    CHECK(disc->sectors(), u32(image.bytes.size() / 2048));
    Disc::Entry entry;
    CHECK(disc->find({"psp_game", "UsrDir", "DATA.BIN"}, entry), true);  //whatever the case
    CHECK(entry.sector, image.sectors["PSP_GAME/USRDIR/DATA.BIN"]);
    CHECK(entry.size, 7 * 1024);
    CHECK(entry.folder, false);
    CHECK(memcmp(entry.date, disc_image::RecordDate, 7), 0);
    std::vector<u8> read(data.size());
    CHECK(disc->read(u64(entry.sector) * 2048, read.size(), read.data()), true);
    CHECK(read == data, true);
    //across blocks: from a sector's last byte into the next ones
    std::vector<u8> across(4100);
    CHECK(disc->read(u64(entry.sector) * 2048 + 2047, across.size(), across.data()), true);
    CHECK(memcmp(across.data(), data.data() + 2047, across.size()), 0);
    CHECK(names(disc->list(disc->root())) == "PSP_GAME/ UMD_DATA.BIN", true);
    Disc::Entry game;
    CHECK(disc->find({"PSP_GAME"}, game), true);
    CHECK(names(disc->list(game)) == "PARAM.SFO SYSDIR/ USRDIR/", true);
    CHECK(disc->find({"PSP_GAME", "USRDIR", "EMPTY.BIN"}, entry), true);
    CHECK(entry.size, 0);
    CHECK(disc->find({"PSP_GAME", "NOTHING"}, entry), false);
    CHECK(disc->find({"UMD_DATA.BIN", "X"}, entry), false);  //a file isn't a folder
    CHECK(disc->read(u64(disc->sectors()) * 2048 - 1, 2, read.data()), false);  //past the end
  }

  //damaged images: no volume descriptor; a CSO header with no block size; a CSO cut short in its index; a CSO whose
  //block won't inflate (opens, but its sectors can't be read)
  std::string error;
  std::vector<u8> blank(64 * 2048, 0);
  CHECK(openDisc(blank, error) == nullptr, true);
  auto cso = disc_image::makeCso(image.bytes);
  auto noBlocks = cso;
  memset(noBlocks.data() + 16, 0, 4);
  CHECK(openDisc(noBlocks, error) == nullptr, true);
  CHECK(openDisc(std::vector<u8>(cso.begin(), cso.begin() + 40), error) == nullptr, true);
  auto shifted = cso;
  shifted[21] = 31;  //an alignment that puts every block past the file's end
  CHECK(openDisc(shifted, error) == nullptr, true);
  auto backwards = cso;
  backwards[24 + 4 * 20] = backwards[24 + 4 * 20 + 1] = backwards[24 + 4 * 20 + 2] = 0xff;  //an entry past the next
  backwards[24 + 4 * 20 + 3] &= 0x80;
  CHECK(openDisc(backwards, error) == nullptr, true);
  //a block that won't inflate: its stream starts well (a stored piece of 100 bytes, which lands in the block being
  //unpacked) and then turns invalid (block type 3); after the failed read, the block read before it still reads
  //right, with nothing of the failure left in it
  auto garbled = cso;
  auto entryOf = [&](u32 block) {
    const u8* p = garbled.data() + 24 + 4 * block;
    return u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24);
  };
  u32 block = 0;
  for(u32 n = 17; n < image.bytes.size() / 2048; n++) {  //a deflated block with room for the stream, after sector 16
    if(entryOf(n) >> 31 || (entryOf(n + 1) & 0x7fff'ffff) - (entryOf(n) & 0x7fff'ffff) < 106) continue;
    if(n != image.sectors["UMD_DATA.BIN"]) { block = n; break; }
  }
  CHECK(block != 0, true);
  u8 stream[106] = {0x00, 100, 0, 0x9b, 0xff};  //not the last piece, stored: 100 bytes, and 100 inverted
  memset(stream + 5, 0xaa, 100);
  stream[105] = 0x07;  //the last piece, of block type 3, which deflate doesn't have
  memcpy(garbled.data() + (entryOf(block) & 0x7fff'ffff), stream, sizeof(stream));
  auto broken = openDisc(garbled, error);
  CHECK(broken != nullptr, true);
  if(broken) {
    std::vector<u8> before(2048), sector(2048), again(2048);
    u32 good = image.sectors["UMD_DATA.BIN"];
    CHECK(broken->readSectors(good, 1, before.data()), true);
    CHECK(broken->readSectors(block, 1, sector.data()), false);
    CHECK(broken->readSectors(good, 1, again.data()), true);
    CHECK(before == again && memcmp(before.data(), pattern(32, 4).data(), 32) == 0, true);
  }
  //a trimmed image, cut right after its last file's last byte: that file still reads whole
  auto trimmed = disc_image::makeIso({{"LAST.BIN", pattern(3000, 6)}});
  trimmed.bytes.resize(u64(trimmed.sectors["LAST.BIN"]) * 2048 + 3000);
  auto cut = openDisc(trimmed.bytes, error);
  CHECK(cut != nullptr, true);
  std::vector<u8> tail(3000);
  if(cut) CHECK(cut->read(u64(trimmed.sectors["LAST.BIN"]) * 2048, 3000, tail.data()), true);
  CHECK(tail == pattern(3000, 6), true);
  //through the kernel too: the file, and a run of sectors over its part of the last sector
  KernelMachine m;
  m.kernel.disc = cut;
  u32 file = m.call("sceIoOpen", {m.string("disc0:/LAST.BIN"), 0x0001, 0});
  CHECK(m.call("sceIoRead", {file, Buffer, 4000}), 3000);
  std::vector<u8> read(3000);
  m.system.memory.copyOut(read.data(), Buffer, 3000);
  CHECK(read == pattern(3000, 6), true);
  char run[64];
  snprintf(run, sizeof(run), "disc0:/sce_lbn0x%x_size0x1000", trimmed.sectors["LAST.BIN"] + 1);
  file = m.call("sceIoOpen", {m.string(run), 0x0001, 0});
  CHECK(m.call("sceIoRead", {file, Buffer, 0x1000}), 3000 - 2048);
  //a damaged record ends its folder, not just its sector: the root made two sectors long, a bad record right after
  //its last entry, and a good one at the start of its second sector, which isn't listed
  auto damaged = image.bytes;
  u8* root = damaged.data() + image.sectors[""] * 2048;
  disc_image::putBoth32(damaged.data() + 16 * 2048 + 156 + 10, 4096);  //the volume descriptor's root record
  disc_image::putBoth32(root + 10, 4096);                              //and the root's record for itself
  u32 end = 0;
  while(root[end]) end += root[end];
  u8 bad[48] = {48};
  bad[32] = 255;  //a name longer than its record
  memcpy(root + end, bad, sizeof(bad));
  u32 last = 0;
  for(u32 at = 0; at < end; at += root[at]) last = at;  //UMD_DATA.BIN's record, the root's last
  memcpy(root + 2048, root + last, root[last]);
  root[2048 + 33] = 'Z';  //renamed, so a listing that went on past the bad record would show it
  auto stopped = openDisc(damaged, error);
  CHECK(stopped != nullptr, true);
  if(stopped) CHECK(names(stopped->list(stopped->root())) == "PSP_GAME/ UMD_DATA.BIN", true);
  //a folder whose record says it's 4 GB, followed by 512 sectors of good records: it's read no further than 256
  std::vector<u8> records(512 * 2048, 0);
  for(u32 sector = 0; sector < 512; sector++) {
    for(u32 at = 0; at + 34 <= 2048; at += 34) {
      u8* r = records.data() + sector * 2048 + at;
      r[0] = 34;
      r[32] = 1;
      r[33] = 'A';
    }
  }
  auto big = disc_image::makeIso({{"PAD.BIN", records}});
  disc_image::putBoth32(big.bytes.data() + 16 * 2048 + 156 + 10, 0xffff'f000);
  auto hollow = openDisc(big.bytes, error);
  CHECK(hollow != nullptr, true);
  if(hollow) CHECK(hollow->list(hollow->root()).size() <= 256 * (2048 / 34), true);
}

//A disc in a machine's drive, its data's first sector, and its data.
struct DiscMachine : KernelMachine {
  disc_image::Image image = testDisc();
  std::vector<u8> data = pattern(7 * 1024, 3);
  u32 dataSector = image.sectors["PSP_GAME/USRDIR/DATA.BIN"];
  DiscMachine() {
    std::string error;
    kernel.disc = openDisc(image.bytes, error);
  }
  auto bytes(u32 address, u32 size) -> std::vector<u8> {
    std::vector<u8> out(size);
    system.memory.copyOut(out.data(), address, size);
    return out;
  }
  auto same(u32 address, const std::vector<u8>& expected, u32 offset, u32 size) -> bool {
    return bytes(address, size) == std::vector<u8>(expected.begin() + offset, expected.begin() + offset + size);
  }
};

//Files on the disc through the kernel: by path, read only; their status; folders; relative paths; runs of sectors
//by number; umd0: itself; and a host folder standing for the disc instead.
static auto discFiles() -> void {
  DiscMachine m;
  u32 file = m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/USRDIR/DATA.BIN"), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoRead", {file, Buffer, 3000}), 3000);
  CHECK(m.same(Buffer, m.data, 0, 3000), true);
  CHECK(m.call("sceIoRead", {file, Buffer, 8000}), 7 * 1024 - 3000);  //to the end
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 0);
  CHECK(m.call("sceIoLseek32", {file, 2040, 0}), 2040);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 16);  //across a sector
  CHECK(m.same(Buffer, m.data, 2040, 16), true);
  CHECK(m.call("sceIoLseek32", {file, u32(-24), 2}), 7 * 1024 - 24);
  CHECK(m.call("sceIoWrite", {file, Buffer, 4}), Kernel::ErrorBadFile);
  CHECK(m.call("sceIoClose", {file}), 0);
  //nothing on the disc can be written; opening to read with other flags (creating, emptying) is let be
  CHECK(m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/NEW.BIN"), 0x0602, 0}), Kernel::ErrorInvalidFlag);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/UMD_DATA.BIN"), 0x0003, 0}), Kernel::ErrorInvalidFlag);
  file = m.call("sceIoOpen", {m.string("disc0:/UMD_DATA.BIN"), 0x0601, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoRead", {file, Buffer, 64}), 32);
  m.call("sceIoClose", {file});
  CHECK(m.call("sceIoOpen", {m.string("disc0:/UMD_DATA.BIN"), 0, 0}), Kernel::ErrorInvalidArgument);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/NOTHING.BIN"), 0x0001, 0}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/PSP_GAME"), 0x0001, 0}), Kernel::ErrorIsDirectory);
  CHECK(m.call("sceIoRemove", {m.string("disc0:/UMD_DATA.BIN")}), Kernel::ErrorReadOnly);
  CHECK(m.call("sceIoMkdir", {m.string("disc0:/NEW"), 0777}), Kernel::ErrorReadOnly);
  CHECK(m.call("sceIoRmdir", {m.string("disc0:/PSP_GAME")}), Kernel::ErrorReadOnly);
  CHECK(m.call("sceIoRename", {m.string("disc0:/UMD_DATA.BIN"), m.string("disc0:/X.BIN")}), Kernel::ErrorReadOnly);

  //status: read-only, the size, the recording date, the first sector in st_private[0], the other five words left
  auto& memory = m.system.memory;
  memory.fill(Stat + 64, 0xcc, 24);
  CHECK(m.call("sceIoGetstat", {m.string("disc0:/PSP_GAME/USRDIR/DATA.BIN"), Stat}), 0);
  CHECK(memory.read(4, Stat + 68), 0xcccc'cccc);
  CHECK(memory.read(4, Stat + 84), 0xcccc'cccc);
  CHECK(memory.read(4, Stat + 0), 0x216d);
  CHECK(memory.read(4, Stat + 4), 0x0025);
  CHECK(memory.read(4, Stat + 8), 7 * 1024);
  CHECK(memory.read(2, Stat + 48), 2004);  //modified: 2004-12-12 12:34:56
  CHECK(memory.read(2, Stat + 50), 12);
  CHECK(memory.read(2, Stat + 58), 56);
  CHECK(memory.read(4, Stat + 64), m.dataSector);
  CHECK(m.call("sceIoGetstat", {m.string("disc0:/PSP_GAME"), Stat}), 0);
  CHECK(memory.read(4, Stat + 0), 0x116d);
  CHECK(m.call("sceIoGetstat", {m.string("disc0:/NOTHING"), Stat}), Kernel::ErrorFileNotFound);
  for(const char* top : {"disc0:", "disc0:/", "umd0:", "ms0:/"}) {  //a device's top has no status
    CHECK(m.call("sceIoGetstat", {m.string(top), Stat}), Kernel::ErrorInvalidArgument);
  }
  CHECK(m.call("sceIoGetstat", {m.string("umd0:/X"), Stat}), 0);  //the whole disc, its size in sectors
  CHECK(memory.read(4, Stat + 8), u32(m.image.bytes.size() / 2048));
  CHECK(memory.read(4, Stat + 64), 0);
  char runStat[64];
  snprintf(runStat, sizeof(runStat), "disc0:/sce_lbn0x%x_size0x1000", m.dataSector);
  CHECK(m.call("sceIoGetstat", {m.string(runStat), Stat}), 0);  //a run: a file that size, starting there
  CHECK(memory.read(4, Stat + 8), 0x1000);
  CHECK(memory.read(4, Stat + 64), m.dataSector);

  //folders: the disc's order, with no "." or ".."; their names' short forms (d_private) aren't written
  auto listing = [&](const char* path) {
    std::string names;
    u32 folder = m.call("sceIoDopen", {m.string(path)});
    if(folder >= 0x8000'0000) return std::string{"error"};
    memory.fill(Buffer, 0, 352);
    memory.write(4, Buffer + 344, Out);
    while(m.call("sceIoDread", {folder, Buffer}) == 1) {
      names += (names.empty() ? "" : " ") + memory.readString(Buffer + 88, 256);
      if(memory.read(4, Buffer) & 0x1000) names += "/";
    }
    m.call("sceIoDclose", {folder});
    return names;
  };
  memory.fill(Out, 0xcc, 1044);
  CHECK(listing("disc0:/PSP_GAME") == "PARAM.SFO SYSDIR/ USRDIR/", true);
  CHECK(listing("disc0:/") == "PSP_GAME/ UMD_DATA.BIN", true);
  CHECK(listing("umd0:") == "", true);  //the whole disc holds no entries
  CHECK(memory.read(4, Out + 4), 0xcccc'cccc);
  CHECK(m.call("sceIoDopen", {m.string("disc0:/UMD_DATA.BIN")}), Kernel::ErrorNotDirectory);

  //relative paths, from the working folder
  CHECK(m.call("sceIoChdir", {m.string("disc0:/PSP_GAME/USRDIR")}), 0);
  file = m.call("sceIoOpen", {m.string("../USRDIR/DATA.BIN"), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  m.call("sceIoClose", {file});
  CHECK(m.call("sceIoChdir", {m.string("disc0:/UMD_DATA.BIN")}), Kernel::ErrorNotDirectory);
  CHECK(m.call("sceIoChdir", {m.string("umd0:/PSP_GAME")}), Kernel::ErrorNotDirectory);

  //disc0: assigned to the drive, as it already is: nothing changes
  CHECK(m.call("sceIoAssign", {m.string("disc0:"), m.string("umd0:"), m.string("isofs0:"), 1, 0, 0}), 0);
  CHECK(listing("disc0:/") == "PSP_GAME/ UMD_DATA.BIN", true);

  //a run of sectors by number, its numbers hexadecimal with or without "0x", what follows them ignored
  char run[64];
  for(const char* form : {"disc0:/sce_lbn0x%x_size0x1000", "disc0:/SCE_LBN%X_SIZE1000",
                          "disc0:/sce_lbn%x_x_size1000_z", "disc0:/sce_lbn0x%x_size0x1000/x",
                          "disc0:/sce_lbn0x%x/_size0x1000"}) {
    snprintf(run, sizeof(run), form, m.dataSector);
    file = m.call("sceIoOpen", {m.string(run), 0x0001, 0});
    CHECK(file >= 3 && file < 0x8000'0000, true);
    memory.fill(Buffer, 0, 0x2000);
    CHECK(m.call("sceIoRead", {file, Buffer, 0x2000}), 0x1000);
    CHECK(m.same(Buffer, m.data, 0, 0x1000), true);
    m.call("sceIoClose", {file});
  }
  file = m.call("sceIoOpen", {m.string("disc0:/sce_lbn_size"), 0x0001, 0});  //no digits: sector 0, no bytes
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 0);
  m.call("sceIoClose", {file});
  //through umd0:, whatever the path, the whole disc in sectors
  snprintf(run, sizeof(run), "umd0:/sce_lbn0x%x_size0x1000", m.dataSector);
  file = m.call("sceIoOpen", {m.string(run), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  memory.fill(Buffer, 0, 3 * 2048);
  CHECK(m.call("sceIoRead", {file, Buffer, 1}), 1);
  CHECK(m.same(Buffer, m.image.bytes, 0, 2048), true);  //sector 0
  CHECK(m.call("sceIoLseek32", {file, m.dataSector, 0}), m.dataSector);
  CHECK(m.call("sceIoRead", {file, Buffer, 2}), 2);
  CHECK(m.same(Buffer, m.data, 0, 4096), true);
  m.call("sceIoClose", {file});
  //a run said to go on past the disc's end stops there
  u32 last = u32(m.image.bytes.size() / 2048) - 1;
  snprintf(run, sizeof(run), "disc0:/sce_lbn0x%x_size0x10000", last);
  file = m.call("sceIoOpen", {m.string(run), 0x0001, 0});
  CHECK(m.call("sceIoRead", {file, Buffer, 0x10000}), 2048);
  CHECK(m.same(Buffer, m.image.bytes, last * 2048, 2048), true);
  m.call("sceIoClose", {file});
  CHECK(m.call("sceIoOpen", {m.string("disc0:/sce_lbn0x10"), 0x0001, 0}), Kernel::ErrorFileNotFound);  //no size
  CHECK(m.call("sceIoOpen", {m.string("disc0:/sce_lbn0xffffff_size0x10"), 0x0001, 0}), Kernel::ErrorFileNotFound);

  //umd0: itself: the whole disc, a sector at a time
  file = m.call("sceIoOpen", {m.string("umd0:"), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoLseek32", {file, m.dataSector, 0}), m.dataSector);
  CHECK(m.call("sceIoRead", {file, Buffer, 1}), 1);
  CHECK(m.same(Buffer, m.data, 0, 2048), true);
  CHECK(m.call("sceIoLseek32", {file, 0, 2}), u32(m.image.bytes.size() / 2048));
  CHECK(m.call("sceIoRead", {file, Buffer, 1}), 0);
  m.call("sceIoClose", {file});
  CHECK(m.call("sceIoOpen", {m.string("disc0:"), 0x0001, 0}), Kernel::ErrorIsDirectory);  //disc0: is no device file

  //a host folder standing for the disc (a homebrew program's own folder) comes first
  HostFolder folder;
  folder.put("HOST.TXT", "host");
  m.kernel.mount("disc0", folder.path.string());
  file = m.call("sceIoOpen", {m.string("disc0:/HOST.TXT"), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/UMD_DATA.BIN"), 0x0001, 0}), Kernel::ErrorFileNotFound);
}

//Requests to a disc file (ioctl) and to the devices (devctl), as games make them.
static auto discRequests() -> void {
  DiscMachine m;
  auto& memory = m.system.memory;
  u32 file = m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/USRDIR/DATA.BIN"), 0x0001, 0});
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0006, 0, 0, Out, 4}), 0);  //its first sector
  CHECK(memory.read(4, Out), m.dataSector);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0007, 0, 0, Out, 8}), 0);  //its size
  CHECK(memory.read(4, Out), 7 * 1024);
  CHECK(memory.read(4, Out + 4), 0);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0003, 0, 0, Out, 4}), 0);  //the sector size
  CHECK(memory.read(4, Out), 2048);
  memory.write(4, In, 100); memory.write(4, In + 4, 0); memory.write(4, In + 8, 0); memory.write(4, In + 12, 0);
  CHECK(m.call("sceIoIoctl", {file, 0x0101'0005, In, 16, 0, 0}), 0);  //seek to 100
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0004, 0, 0, Out, 4}), 0);  //where it is
  CHECK(memory.read(4, Out), 100);
  memory.write(4, In, 16);
  CHECK(m.call("sceIoIoctl", {file, 0x0103'0008, In, 4, Buffer, 16}), 16);  //read 16 bytes
  CHECK(m.same(Buffer, m.data, 100, 16), true);
  memory.write(4, In, 8000);
  CHECK(m.call("sceIoIoctl", {file, 0x0101'0005, In, 16, 0, 0}), Kernel::ErrorIOError);  //not past the end
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0001, 0, 0, Buffer, 2048}), 0);  //the volume descriptor
  CHECK(memory.readString(Buffer + 1, 5) == "CD001", true);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0001, 0, 0, Buffer, 100}), Kernel::ErrorInvalidArgument);
  u32 tableSize = memory.read(4, Buffer + 132);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0002, 0, 0, Buffer, 2048}), 0);  //the path table
  CHECK(m.same(Buffer, m.image.bytes, 18 * 2048, tableSize), true);
  CHECK(m.call("sceIoIoctl", {file, 0x1234'5678, 0, 0, 0, 0}), Kernel::ErrorFunctionNotSupported);
  CHECK(m.call("sceIoIoctl", {99, 0x0102'0006, 0, 0, Out, 4}), Kernel::ErrorBadFile);
  m.call("sceIoClose", {file});

  file = m.call("sceIoOpen", {m.string("umd0:"), 0x0001, 0});
  memory.write(4, In, m.dataSector); memory.write(4, In + 4, 0); memory.write(4, In + 12, 0);
  CHECK(m.call("sceIoIoctl", {file, 0x01f1'00a6, In, 16, 0, 0}), 0);  //seek, in sectors
  memory.write(4, In, 0);
  CHECK(m.call("sceIoIoctl", {file, 0x01f3'0003, In, 4, Buffer, 4096}), Kernel::ErrorInvalidArgument);  //no sectors
  memory.write(4, In, 2);
  CHECK(m.call("sceIoIoctl", {file, 0x01f3'0003, In, 4, Buffer, 4096}), 2);  //read two sectors
  CHECK(m.same(Buffer, m.data, 0, 4096), true);
  CHECK(m.call("sceIoIoctl", {file, 0x01d2'0001, 0, 0, Out, 4}), 0);  //where it is, in sectors
  CHECK(memory.read(4, Out), m.dataSector + 2);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0001, 0, 0, Buffer, 2048}), Kernel::ErrorFunctionNotSupported);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0007, 0, 0, Out, 8}), 0);  //its size: in sectors
  CHECK(memory.read(4, Out), u32(m.image.bytes.size() / 2048));
  memory.write(4, In, 0xffff); memory.write(4, In + 4, 0); memory.write(4, In + 12, 0);
  CHECK(m.call("sceIoIoctl", {file, 0x01f1'00a6, In, 16, 0, 0}), Kernel::ErrorInvalidFileSize);  //past the disc
  m.call("sceIoClose", {file});

  HostFolder folder;
  folder.put("A.TXT", "a");
  m.kernel.mount("ms0", folder.path.string());
  file = m.call("sceIoOpen", {m.string("ms0:/A.TXT"), 0x0001, 0});
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0006, 0, 0, Out, 4}), Kernel::ErrorFunctionNotSupported);

  //the drive: a game disc, ready, finished whatever it was asked to read ahead
  CHECK(m.call("sceIoDevctl", {m.string("umd0:"), 0x01f2'0001, 0, 0, Out, 8}), 0);
  CHECK(memory.read(4, Out + 4), 0x10);
  CHECK(m.call("sceIoDevctl", {m.string("umd0:"), 0x01e1'8030, In, 16, 0, 0}), 1);  //the region matches
  CHECK(m.call("sceIoDevctl", {m.string("umd0:"), 0x01f2'0003, 0, 0, Out, 4}), 0);
  CHECK(memory.read(4, Out), u32(m.image.bytes.size() / 2048) - 1);
  CHECK(m.call("sceIoDevctl", {m.string("umd0:"), 0x01f3'00a5, In, 4, Out, 4}), 0);
  CHECK(memory.read(4, Out), 1);
  CHECK(m.call("sceIoDevctl", {m.string("umd0:"), 0x01f1'00a4, In, 0, 0, 0}), Kernel::ErrorDevctlBadParameters);
  //the memory stick: in, formatted, writable, with room to spare
  CHECK(m.call("sceIoDevctl", {m.string("ms0:"), 0x0202'5806, 0, 0, Out, 4}), 0);
  CHECK(memory.read(4, Out), 1);
  CHECK(m.call("sceIoDevctl", {m.string("fatms0:"), 0x0242'5823, 0, 0, Out, 4}), 0);
  CHECK(memory.read(4, Out), 1);
  CHECK(m.call("sceIoDevctl", {m.string("fatms0:"), 0x0242'5824, 0, 0, Out, 4}), 0);
  CHECK(memory.read(4, Out), 0);
  memory.write(4, In, Out);
  CHECK(m.call("sceIoDevctl", {m.string("ms0:"), 0x0242'5818, In, 4, 0, 0}), 0);
  CHECK(memory.read(4, Out), Kernel::StickClusters);  //a 2 GB stick, mostly empty
  CHECK(memory.read(4, Out + 4), Kernel::StickFreeClusters);
  CHECK(memory.read(4, Out + 12), 512);
  CHECK(memory.read(4, Out + 16), 64);
  u32 callback = m.call("sceKernelCreateCallback", {m.string("stick"), 0x0880'1000, 0});
  memory.write(4, In, callback);
  CHECK(m.call("sceIoDevctl", {m.string("fatms0:"), 0x0241'5821, In, 4, 0, 0}), 0);
  CHECK(m.call("sceIoDevctl", {m.string("fatms0:"), 0x0241'5822, In, 4, 0, 0}), 0);
  CHECK(m.call("sceIoDevctl", {m.string("fatms0:"), 0x0241'5822, In, 4, 0, 0}), Kernel::ErrorInvalidArgument);
  memory.write(4, In, 0x1234);
  CHECK(m.call("sceIoDevctl", {m.string("ms0:"), 0x0201'5804, In, 4, 0, 0}), Kernel::ErrorInvalidArgument);
  CHECK(m.call("sceIoDevctl", {m.string("ms0:"), 0x0999'9999, 0, 0, 0, 0}), Kernel::ErrorFunctionNotSupported);
  CHECK(m.call("sceIoDevctl", {m.string("flash0:"), 0x0202'5806, 0, 0, Out, 4}), Kernel::ErrorFunctionNotSupported);
}

//A file's PGD key (ioctl 0x04100001): given for a file without PGD's header, the file is read as it is (the fan
//translations of 7th Dragon 2020 and its sequel); one with the header is refused, nothing here decrypting, as are a
//key shorter than 16 bytes, umd0: and a file off the disc, as every request was before.
static auto discKeys() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  auto plain = pattern(3000, 5), encrypted = pattern(3000, 6);
  memcpy(encrypted.data(), "\0PGD", 4);
  auto image = disc_image::makeIso({
    {"PSP_GAME/INSDIR/GAME.DNS", plain}, {"PSP_GAME/INSDIR/PGD.DNS", encrypted}, {"UMD_DATA.BIN", pattern(32, 4)},
  });
  std::string error;
  m.kernel.disc = openDisc(image.bytes, error);
  for(u32 at = 0; at < 16; at++) memory.write(1, In + at, 0x40 + at);  //the key
  u32 file = m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/INSDIR/GAME.DNS"), 0x4000'4001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoIoctl", {file, 0x0410'0001, In, 16, 0, 0}), 0);
  CHECK(m.call("sceIoRead", {file, Buffer, 4000}), 3000);
  std::vector<u8> read(3000);
  memory.copyOut(read.data(), Buffer, 3000);
  CHECK(read == plain, true);
  CHECK(m.call("sceIoIoctl", {file, 0x0410'0001, In, 8, 0, 0}), Kernel::ErrorFunctionNotSupported);
  m.call("sceIoClose", {file});
  file = m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/INSDIR/PGD.DNS"), 0x4000'4001, 0});
  CHECK(m.call("sceIoIoctl", {file, 0x0410'0001, In, 16, 0, 0}), Kernel::ErrorFunctionNotSupported);
  m.call("sceIoClose", {file});
  file = m.call("sceIoOpen", {m.string("umd0:"), 0x0001, 0});
  CHECK(m.call("sceIoIoctl", {file, 0x0410'0001, In, 16, 0, 0}), Kernel::ErrorFunctionNotSupported);
  m.call("sceIoClose", {file});
  HostFolder folder;
  folder.put("A.TXT", "a");
  m.kernel.mount("ms0", folder.path.string());
  file = m.call("sceIoOpen", {m.string("ms0:/A.TXT"), 0x0001, 0});
  CHECK(m.call("sceIoIoctl", {file, 0x0410'0001, In, 16, 0, 0}), Kernel::ErrorFunctionNotSupported);
  CHECK(m.notes.size(), 0);
}

//The drive's state, and waiting for it.
static auto umdDrive() -> void {
  for(bool recompile : {false, true}) {
    DiscMachine m;
    auto& memory = m.system.memory;
    CHECK(m.call("sceUmdCheckMedium", {}), 1);
    CHECK(m.call("sceUmdGetDriveStat", {}), Kernel::UmdPresent | Kernel::UmdReady | Kernel::UmdReadable);
    CHECK(m.call("sceUmdActivate", {1, m.string("disc0:")}), 0);
    CHECK(m.call("sceUmdActivate", {3, m.string("disc0:")}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdActivate", {1, m.string("disc1:")}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdDeactivate", {1, 0}), 0);
    CHECK(m.call("sceUmdDeactivate", {2, 0}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdGetDriveStat", {}), Kernel::UmdPresent | Kernel::UmdReady);  //deactivated: not readable
    CHECK(m.call("sceUmdActivate", {1, m.string("disc0:")}), 0);
    CHECK(m.call("sceUmdGetDriveStat", {}), Kernel::UmdPresent | Kernel::UmdReady | Kernel::UmdReadable);
    memory.write(4, Out, 8);
    CHECK(m.call("sceUmdGetDiscInfo", {Out}), 0);
    CHECK(memory.read(4, Out + 4), 0x10);  //a game
    memory.write(4, Out, 7);
    CHECK(m.call("sceUmdGetDiscInfo", {Out}), Kernel::ErrorInvalidArgument);
    u32 callback = m.call("sceKernelCreateCallback", {m.string("umd"), 0x0880'1000, 0});
    //registered and unregistered as umd/register recorded: 0 is no callback, but unregistering it is taken while
    //none is registered; unregistering gives the callback's ID back, or 0 for SDKs after 3.00
    CHECK(m.call("sceUmdRegisterUMDCallBack", {0x1234}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdRegisterUMDCallBack", {0}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdRegisterUMDCallBack", {callback}), 0);
    CHECK(m.call("sceUmdRegisterUMDCallBack", {callback}), 0);  //twice
    CHECK(m.call("sceUmdUnRegisterUMDCallBack", {callback + 1}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdUnRegisterUMDCallBack", {0}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdUnRegisterUMDCallBack", {callback}), callback);
    CHECK(m.call("sceUmdUnRegisterUMDCallBack", {callback}), Kernel::ErrorInvalidArgument);
    CHECK(m.call("sceUmdUnRegisterUMDCallBack", {0}), 0);
    m.kernel.sdkVersion = 0x0300'0001;
    CHECK(m.call("sceUmdRegisterUMDCallBack", {callback}), 0);
    CHECK(m.call("sceUmdUnRegisterUMDCallBack", {callback}), 0);
    m.kernel.sdkVersion = 0;
    CHECK(m.call("sceUmdWaitDriveStat", {Kernel::UmdChanged}), Kernel::ErrorInvalidArgument);  //can't be waited for

    //a thread waits for the drive: ready, at once; no disc, until its timeout (of 100 microseconds, which the PSP
    //makes 240); and no disc with no timeout, until the wait is cancelled
    Assembler main{m, 0x0880'1000};
    main.li(a0, Kernel::UmdReady);
    main.call("sceUmdWaitDriveStat");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.li(a0, Kernel::UmdNotPresent); main.li(a1, 100);
    main.call("sceUmdWaitDriveStatWithTimer");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 4, t0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s0));
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 8, t0));
    for(auto [wait, at] : {std::pair{"sceUmdWaitDriveStatWithTimer", 16u}, {"sceUmdWaitDriveStatCB", 20u}}) {
      main.call("sceKernelGetSystemTimeLow");  //a timeout of 1 microsecond: 25 waiting alone, 240 with callbacks
      main.put(addu(s0, v0, zero));
      main.li(a0, Kernel::UmdNotPresent); main.li(a1, 1);
      main.call(wait);
      main.call("sceKernelGetSystemTimeLow");
      main.put(subu(v0, v0, s0));
      main.li(t0, KernelMachine::Results); main.put(sw(v0, at, t0));
    }
    main.li(a0, Kernel::UmdNotPresent);
    main.call("sceUmdWaitDriveStat");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 12, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, false);  //waiting for good
    CHECK(memory.read(4, KernelMachine::Results + 0), 0);
    CHECK(memory.read(4, KernelMachine::Results + 4), Kernel::ErrorWaitTimeout);
    CHECK(memory.read(4, KernelMachine::Results + 8) >= 240, true);
    u32 alone = memory.read(4, KernelMachine::Results + 16);
    u32 withCallbacks = memory.read(4, KernelMachine::Results + 20);
    CHECK(alone >= 25 && alone < 240, true);
    CHECK(withCallbacks >= 240, true);
    m.call("sceUmdCancelWaitDriveStat", {});  //(from outside any thread: what it returns lands in the woken one's)
    m.kernel.run(Kernel::CPUFrequency / 100);
    CHECK(m.kernel.exited, true);
    CHECK(memory.read(4, KernelMachine::Results + 12), Kernel::ErrorWaitCancelled);
  }

  KernelMachine empty;  //no disc in the drive
  CHECK(empty.call("sceUmdCheckMedium", {}), 0);
  CHECK(empty.call("sceUmdGetDriveStat", {}), Kernel::UmdNotPresent);
}

//The drive's callback, as pspautotests' umd/callbacks recorded, and the waits umd/wait did: activating the drive
//tells it the state (0x32, or 0x22 to a program that gave no SDK version), and CheckCallback runs it; deactivating
//leaves the drive ready but not readable (0x12), which a wait for "readable" times out on, and tells the callback,
//which a wait that runs callbacks runs as it returns at once. Activating again wakes a thread waiting for the drive
//to be readable, and lets a loop of vertical blanks that run callbacks, as MotorStorm: Arctic Edge's boot waits,
//see it. On both engines; a state saved at the end loads.
static auto umdCallback() -> void {
  constexpr u32 R = KernelMachine::Results, Handler = 0x0880'0000, Waiter = 0x0880'0800;
  for(u32 sdk : {0x0500'0010u, 0u}) {
    for(bool recompile : {false, true}) {
      DiscMachine m;
      auto& memory = m.system.memory;
      m.kernel.sdkVersion = sdk;
      //the callback (count, the drive's state, its argument): counts its calls at R + 0x40, keeps the state at 0x44
      Assembler handler{m, Handler};
      handler.li(t0, R);
      handler.put(lw(t1, 0x40, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0x40, t0));
      handler.put(sw(a1, 0x44, t0)); handler.put(sw(a2, 0x48, t0));
      handler.put(jr(ra)); handler.put(addiu(v0, zero, 0));
      //a thread that waits for the drive to be readable, then writes 0x77 at R + 0x28 and what the wait gave at 0x2c
      Assembler waiter{m, Waiter};
      waiter.li(a0, Kernel::UmdReadable);
      waiter.call("sceUmdWaitDriveStat");
      waiter.li(t0, R); waiter.put(sw(v0, 0x2c, t0)); waiter.li(t1, 0x77); waiter.put(sw(t1, 0x28, t0));
      waiter.call("sceKernelSleepThread");

      Assembler main{m, 0x0880'1000};
      main.li(s0, R);
      main.li(a0, m.string("umd")); main.li(a1, Handler); main.li(a2, 0x1234);
      main.call("sceKernelCreateCallback");
      main.put(addu(a0, v0, zero));
      main.call("sceUmdRegisterUMDCallBack");
      main.li(a0, 1); main.li(a1, m.string("disc0:"));
      main.call("sceUmdActivate");
      main.put(sw(v0, 0, s0));
      main.call("sceKernelCheckCallback");
      main.put(sw(v0, 4, s0)); main.put(lw(t0, 0x44, s0)); main.put(sw(t0, 8, s0));
      main.li(a0, 1); main.li(a1, 0);
      main.call("sceUmdDeactivate");
      main.put(sw(v0, 0xc, s0));
      main.call("sceUmdGetDriveStat");
      main.put(sw(v0, 0x10, s0));
      main.li(a0, Kernel::UmdReadable); main.li(a1, 1000);
      main.call("sceUmdWaitDriveStatWithTimer");
      main.put(sw(v0, 0x14, s0));
      main.li(a0, 0xff); main.li(a1, 0);
      main.call("sceUmdWaitDriveStatCB");
      main.put(sw(v0, 0x18, s0)); main.put(lw(t0, 0x44, s0)); main.put(sw(t0, 0x1c, s0));
      main.call("sceKernelCheckCallback");
      main.put(sw(v0, 0x20, s0));
      main.put(sw(zero, 0x44, s0));
      main.li(a0, m.string("waiter")); main.li(a1, Waiter); main.li(a2, 0x10); main.li(a3, 0x1000); main.li(t0, 0);
      main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");  //(it runs first, and waits)
      main.put(lw(t0, 0x28, s0)); main.put(sw(t0, 0x24, s0));  //nothing there yet
      main.li(a0, 1); main.li(a1, m.string("disc0:"));
      main.call("sceUmdActivate");
      u32 loop = main.here();  //MotorStorm's: a vertical blank at a time, till the callback says readable
      main.call("sceDisplayWaitVblankStartCB");
      main.put(lw(t0, 0x44, s0)); main.put(andi(t0, t0, Kernel::UmdReadable));
      main.put(beq(t0, zero, s32(loop - main.here() - 4) / 4)); main.put(nop);
      main.li(t0, 0x600d); main.put(sw(t0, 0x30, s0));
      main.call("sceKernelExitGame");
      m.runProgram(0x0880'1000, recompile);

      u32 activated = sdk ? 0x32 : 0x22;
      CHECK(m.kernel.exited, true);
      CHECK(memory.read(4, R), 0);
      CHECK(memory.read(4, R + 4), 1);  //the callback ran
      CHECK(memory.read(4, R + 8), activated);
      CHECK(memory.read(4, R + 0x48), 0x1234);
      CHECK(memory.read(4, R + 0xc), 0);
      CHECK(memory.read(4, R + 0x10), 0x12);
      CHECK(memory.read(4, R + 0x14), Kernel::ErrorWaitTimeout);
      CHECK(memory.read(4, R + 0x18), 0);
      CHECK(memory.read(4, R + 0x1c), 0x12);  //run as the wait returned
      CHECK(memory.read(4, R + 0x20), 0);     //nothing left for CheckCallback
      CHECK(memory.read(4, R + 0x24), 0);
      CHECK(memory.read(4, R + 0x28), 0x77);  //woken by the activation
      CHECK(memory.read(4, R + 0x2c), 0);
      CHECK(memory.read(4, R + 0x44), activated);
      CHECK(memory.read(4, R + 0x40), 3);
      CHECK(memory.read(4, R + 0x30), 0x600d);
      CHECK(m.kernel.umdDeactivated, false);
      CHECK(roundTrip(m), true);
    }
  }
}

//Activating the drive mounts the disc's file system, which reads the disc: the caller waits a sector's read (about
//1.6 ms) and a worse thread runs meanwhile, as Def Jam: Fight for NY needs (its file thread makes the semaphore the
//thread main makes after activating waits on). The drive is readable as it returns. With interrupts held off, or
//dispatching, or from an interrupt handler, it can't wait, and mounts at once. On both engines.
static auto umdMounting() -> void {
  constexpr u32 R = KernelMachine::Results, Worse = 0x0880'0800;
  for(bool recompile : {false, true}) {
    DiscMachine m;
    auto& memory = m.system.memory;
    //a worse thread: notes that it ran (0x600d at R + 0x20), then sleeps
    Assembler worse{m, Worse};
    worse.li(t0, R); worse.li(t1, 0x600d); worse.put(sw(t1, 0x20, t0));
    worse.call("sceKernelSleepThread");

    Assembler main{m, 0x0880'1000};
    main.li(s0, R);
    main.li(a0, m.string("worse")); main.li(a1, Worse); main.li(a2, 0x30); main.li(a3, 0x1000); main.li(t0, 0);
    main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //(it doesn't run: main is better)
    main.put(lw(t0, 0x20, s0)); main.put(sw(t0, 0x24, s0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s1, v0, zero));
    main.li(a0, 1); main.li(a1, m.string("disc0:"));
    main.call("sceUmdActivate");
    main.put(sw(v0, 0, s0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s1)); main.put(sw(v0, 4, s0));
    main.put(lw(t0, 0x20, s0)); main.put(sw(t0, 0x28, s0));  //it ran meanwhile
    main.call("sceUmdGetDriveStat");
    main.put(sw(v0, 8, s0));
    //interrupts held off, then dispatching: at once
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s2, v0, zero));
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s1, v0, zero));
    main.li(a0, 1); main.li(a1, m.string("disc0:"));
    main.call("sceUmdActivate");
    main.put(sw(v0, 0xc, s0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s1)); main.put(sw(v0, 0x10, s0));
    main.put(addu(a0, s2, zero));
    main.call("sceKernelCpuResumeIntr");
    main.call("sceKernelSuspendDispatchThread");
    main.put(addu(s2, v0, zero));
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s1, v0, zero));
    main.li(a0, 1); main.li(a1, m.string("disc0:"));
    main.call("sceUmdActivate");
    main.put(sw(v0, 0x14, s0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s1)); main.put(sw(v0, 0x18, s0));
    main.put(addu(a0, s2, zero));
    main.call("sceKernelResumeDispatchThread");
    //a vertical blank's handler activating it, timed at R + 0x2c and 0x30
    Assembler handler{m, 0x0880'3000};
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp)); handler.put(sw(s0, 8, sp));
    handler.put(sw(s1, 4, sp));
    handler.li(s0, R);
    handler.call("sceKernelGetSystemTimeLow");
    handler.put(addu(s1, v0, zero));
    handler.li(a0, 1); handler.li(a1, m.string("disc0:"));
    handler.call("sceUmdActivate");
    handler.put(sw(v0, 0x2c, s0));
    handler.call("sceKernelGetSystemTimeLow");
    handler.put(subu(v0, v0, s1)); handler.put(sw(v0, 0x30, s0));
    handler.put(lw(s1, 4, sp)); handler.put(lw(s0, 8, sp)); handler.put(lw(ra, 12, sp));
    handler.put(addiu(sp, sp, 16));
    handler.li(v0, 0); handler.put(jr(ra)); handler.put(nop);
    main.li(a0, 30); main.li(a1, 0); main.li(a2, 0x0880'3000); main.li(a3, 0);
    main.call("sceKernelRegisterSubIntrHandler");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelEnableSubIntr");
    main.call("sceDisplayWaitVblankStart");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelReleaseSubIntrHandler");
    main.call("sceKernelExitGame");
    memory.write(4, R + 0x2c, 0xcccc'cccc);
    m.runProgram(0x0880'1000, recompile);

    CHECK(m.kernel.exited, true);
    CHECK(memory.read(4, R + 0x24), 0);       //not before
    CHECK(memory.read(4, R + 0), 0);
    u32 mounted = memory.read(4, R + 4);
    CHECK(mounted >= 1580 && mounted < 1700, true);  //100 microseconds and a sector at 1,375,000 bytes a second
    CHECK(memory.read(4, R + 0x28), 0x600d);
    CHECK(memory.read(4, R + 8), Kernel::UmdPresent | Kernel::UmdReady | Kernel::UmdReadable);
    CHECK(memory.read(4, R + 0xc), 0);
    CHECK(memory.read(4, R + 0x10) < 100, true);
    CHECK(memory.read(4, R + 0x14), 0);
    CHECK(memory.read(4, R + 0x18) < 100, true);
    CHECK(memory.read(4, R + 0x2c), 0);
    CHECK(memory.read(4, R + 0x30) < 100, true);
  }
}

//The memory stick's insert and eject callback, as pspautotests' mstick recorded: registered through fatms0:, it's
//told at once that a stick is in, and the program's next sceKernelCheckCallback runs it (1: one ran) with a count of
//1, the event 1 (inserted) and its own argument; unregistering it and deleting it then succeed. mscmhc0's register
//tells it too. A better thread's callback, its thread asleep where callbacks run, runs at once, and the register
//still gives its caller 0. On both engines.
static auto stickCallback() -> void {
  constexpr u32 R = KernelMachine::Results, Handler = 0x0880'0000, Owner = 0x0880'0800;
  for(bool recompile : {false, true}) {
    DiscMachine m;
    auto& memory = m.system.memory;
    //the callback (count, event, its argument): counts its calls at R + 0x40 and keeps its three words after
    Assembler handler{m, Handler};
    handler.li(t0, R);
    handler.put(lw(t1, 0x40, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0x40, t0));
    handler.put(sw(a0, 0x44, t0)); handler.put(sw(a1, 0x48, t0)); handler.put(sw(a2, 0x4c, t0));
    handler.put(jr(ra)); handler.put(addiu(v0, zero, 0));
    //a thread better than main's: makes a callback of its own (its ID at R + 0x64), then sleeps where callbacks run
    Assembler owner{m, Owner};
    owner.li(a0, m.string("OWNER")); owner.li(a1, Handler); owner.li(a2, 0x888);
    owner.call("sceKernelCreateCallback");
    owner.li(t0, R); owner.put(sw(v0, 0x64, t0));
    u32 sleep = owner.here();
    owner.call("sceKernelSleepThreadCB");
    owner.put(beq(zero, zero, s32(sleep - owner.here() - 4) / 4)); owner.put(nop);

    Assembler main{m, 0x0880'1000};
    main.li(s0, R);
    main.li(a0, m.string("MSCB")); main.li(a1, Handler); main.li(a2, 0x777);
    main.call("sceKernelCreateCallback");
    main.put(addu(s1, v0, zero));
    main.put(sw(s1, 0x60, s0));  //the callback's ID, the devctl's in
    main.li(a0, m.string("fatms0:")); main.li(a1, 0x0241'5821); main.put(addiu(a2, s0, 0x60)); main.li(a3, 4);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceIoDevctl");
    main.put(sw(v0, 0, s0));
    main.call("sceKernelCheckCallback");
    main.put(sw(v0, 4, s0));
    main.call("sceKernelCheckCallback");  //nothing more to run
    main.put(sw(v0, 8, s0));
    main.li(a0, m.string("fatms0:")); main.li(a1, 0x0241'5822); main.put(addiu(a2, s0, 0x60)); main.li(a3, 4);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceIoDevctl");
    main.put(sw(v0, 0xc, s0));
    main.li(a0, m.string("mscmhc0:")); main.li(a1, 0x0201'5804); main.put(addiu(a2, s0, 0x60)); main.li(a3, 4);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceIoDevctl");
    main.put(sw(v0, 0x10, s0));
    main.call("sceKernelCheckCallback");
    main.put(sw(v0, 0x14, s0));
    main.li(a0, m.string("mscmhc0:")); main.li(a1, 0x0201'5805); main.put(addiu(a2, s0, 0x60)); main.li(a3, 4);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceIoDevctl");
    main.put(sw(v0, 0x18, s0));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelDeleteCallback");
    main.put(sw(v0, 0x1c, s0));
    main.li(a0, m.string("owner")); main.li(a1, Owner); main.li(a2, 0x10); main.li(a3, 0x1000); main.li(t0, 0);
    main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //(it runs first, and sleeps)
    main.li(a0, m.string("fatms0:")); main.li(a1, 0x0241'5821); main.put(addiu(a2, s0, 0x64)); main.li(a3, 4);
    main.li(t0, 0); main.li(t1, 0);
    main.li(v0, 0x5555);  //(what a result written to the wrong thread would leave)
    main.call("sceIoDevctl");
    main.put(sw(v0, 0x20, s0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);

    CHECK(m.kernel.exited, true);
    CHECK(memory.read(4, R), 0);         //registered
    CHECK(memory.read(4, R + 4), 1);     //and run by the next CheckCallback
    CHECK(memory.read(4, R + 8), 0);
    CHECK(memory.read(4, R + 0xc), 0);   //unregistered
    CHECK(memory.read(4, R + 0x10), 0);  //registered with mscmhc0
    CHECK(memory.read(4, R + 0x14), 1);
    CHECK(memory.read(4, R + 0x18), 0);
    CHECK(memory.read(4, R + 0x1c), 0);  //deleted
    CHECK(memory.read(4, R + 0x20), 0);  //the better thread's registered, and main told so
    CHECK(memory.read(4, R + 0x40), 3);  //run once for each register
    CHECK(memory.read(4, R + 0x44), 1);  //a count of 1
    CHECK(memory.read(4, R + 0x48), 1);  //inserted
    CHECK(memory.read(4, R + 0x4c), 0x888);  //(the last: the better thread's)
    CHECK(m.kernel.memoryStickCallbacks.size(), 1);
  }
}

auto discTests() -> Tests {
  return {
    {"disc images", discImages}, {"disc files", discFiles}, {"disc requests", discRequests},
    {"disc PGD keys", discKeys}, {"disc drive", umdDrive}, {"disc drive's callback", umdCallback},
    {"disc drive mounting as it's activated", umdMounting}, {"memory stick's callback", stickCallback},
  };
}

}
