//The PSP scene's other compressed disc images (ares/psp/kernel/disc.cpp): CSO version 2 with LZ4 blocks, ZSO, DAX and
//JSO, made here from an ISO (disc-formats.hpp) and read back sector for sector; damaged ones refused, or failing the
//reads of their damaged blocks; and the LZ4 and LZO unpackers (unpack.cpp) on blocks the reference packers made
//(unpack-vectors.hpp), cut short and corrupted. (CHD images are read in the system, through libchdr: the ares tests.)
#include "kernel-machine.hpp"
#include "disc-formats.hpp"
#include "unpack-vectors.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Disc;
using ares::PlayStationPortable::unpackLZ4;
using ares::PlayStationPortable::unpackLZO;

static auto get16(const u8* p) -> u32 { return p[0] | p[1] << 8; }
static auto get32(const u8* p) -> u32 { return p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24; }

//Bytes no packer can shrink.
static auto noise(u32 size, u32 seed) -> std::vector<u8> {
  std::vector<u8> bytes(size);
  u32 x = seed * 2654435761u + 1;
  for(auto& byte : bytes) x = x * 1103515245 + 12345, byte = u8(x >> 16);
  return bytes;
}

//A Disc reading an image from memory.
static auto openImage(const std::vector<u8>& bytes, std::string& error) -> std::shared_ptr<Disc> {
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

//A disc whose blocks pack every way: noise (stored as it is, as nothing shrinks it), text (literals and short
//matches), zeros (long matches), and noise repeated 20 KiB on (LZO's matches 16 to 48 KiB back, in 32 KiB blocks).
static auto formatsDisc() -> disc_image::Image {
  std::string text;
  while(text.size() < 40'000) text += "PSP_GAME/USRDIR/DATA" + std::to_string(text.size()) + ".BIN, PARAM.SFO; ";
  auto twice = noise(20 * 1024, 2);
  twice.insert(twice.end(), twice.begin(), twice.end());
  return disc_image::makeIso({
    {"PSP_GAME/SYSDIR/EBOOT.BIN", noise(9000, 1)},
    {"PSP_GAME/USRDIR/TEXT.BIN", std::vector<u8>(text.begin(), text.end())},
    {"PSP_GAME/USRDIR/ZERO.BIN", std::vector<u8>(24 * 1024, 0)},
    {"PSP_GAME/USRDIR/TWICE.BIN", twice},
  });
}

//Every sector of the disc as the image unpacks it, against the ISO's.
static auto sameAsIso(Disc& disc, const std::vector<u8>& iso) -> bool {
  if(disc.size() != iso.size()) return false;
  std::vector<u8> all(iso.size());
  return disc.read(0, all.size(), all.data()) && all == iso;
}

//Each compressed form of the disc, read back the same as the ISO, with every packing each form has: a CSO version 2's
//blocks deflated, LZ4-packed and stored; a ZSO's LZ4-packed and stored; both padded out to an alignment; a DAX's
//zlib frames and its frames left uncompressed; a JSO's LZO, zlib or raw deflate blocks, its stored ones, and a short
//last block stored at its length.
static auto discFormats() -> void {
  auto image = formatsDisc();
  auto& iso = image.bytes;
  //a disc ending part way into an 8 KiB block, 3 of its 4 sectors there
  auto longer = iso;
  while(longer.size() % 8192 != 6144) longer.insert(longer.end(), 2048, 0x5a);
  struct Form { std::string name; std::vector<u8> bytes; const std::vector<u8>* disc; };
  std::vector<Form> forms = {
    {"CSO version 2", disc_image::makeCso2(iso), &iso},
    {"CSO version 2 of 8 KiB blocks", disc_image::makeCso2(iso, 8192), &iso},
    {"ZSO", disc_image::makeZso(iso), &iso},
    {"ZSO of 8 KiB blocks", disc_image::makeZso(iso, 8192), &iso},
    {"CSO version 2 aligned to 4 bytes, NUL padding", disc_image::makeCso2(iso, 2048, 2, 0), &iso},
    {"CSO version 2 aligned to 64 bytes, 'X' padding", disc_image::makeCso2(iso, 8192, 6, 'X'), &iso},
    {"ZSO aligned to 4 bytes, NUL padding", disc_image::makeZso(iso, 2048, 2, 0), &iso},
    {"ZSO aligned to 64 bytes, 'X' padding", disc_image::makeZso(iso, 2048, 6, 'X'), &iso},
    {"DAX", disc_image::makeDax(iso), &iso},
    {"DAX with uncompressed areas", disc_image::makeDax(iso, {{0, 2}, {5, 1}}), &iso},
    {"JSO (LZO)", disc_image::makeJso(iso, 2048, true), &iso},
    {"JSO (LZO) of 32 KiB blocks", disc_image::makeJso(iso, 32768, true), &iso},
    {"JSO (zlib)", disc_image::makeJso(iso, 2048, false), &iso},
    {"JSO (raw deflate) of 8 KiB blocks", disc_image::makeJso(iso, 8192, false, true), &iso},
    {"JSO (LZO), its short last block stored", disc_image::makeJso(longer, 8192, true, false, true), &longer},
    {"JSO (zlib), its short last block stored", disc_image::makeJso(longer, 8192, false, false, true), &longer},
  };
  for(auto& form : forms) {
    std::string error;
    auto disc = openImage(form.bytes, error);
    CHECK(disc != nullptr, true);
    if(!disc) { std::printf("  %s: %s\n", form.name.c_str(), error.c_str()); continue; }
    bool same = sameAsIso(*disc, *form.disc);
    CHECK(same, true);
    if(!same) std::printf("  %s: not the ISO's bytes\n", form.name.c_str());
    Disc::Entry entry;
    CHECK(disc->find({"PSP_GAME", "USRDIR", "TWICE.BIN"}, entry), true);
    CHECK(entry.size, 40 * 1024);
  }

  //the forms do hold what they're meant to test: blocks of each packing
  auto form = [&](const std::string& name) -> const std::vector<u8>& {
    for(auto& f : forms) if(f.name == name) return f.bytes;
    std::printf("  no form named %s\n", name.c_str());
    return iso;
  };
  auto entries = [](const std::vector<u8>& bytes, u32 at, u32 count) {
    std::vector<u32> list(count);
    for(u32 n = 0; n < count; n++) list[n] = get32(bytes.data() + at + 4 * n);
    return list;
  };
  u32 blocks = iso.size() / 2048;
  auto& cso = form("CSO version 2");
  auto index = entries(cso, 24, blocks + 1);
  u32 lz4 = 0, deflated = 0, stored = 0;
  for(u32 n = 0; n < blocks; n++) {
    u32 size = (index[n + 1] & 0x7fff'ffff) - (index[n] & 0x7fff'ffff);
    if(size >= 2048) stored++;
    else if(index[n] >> 31) lz4++;
    else deflated++;
  }
  CHECK(lz4 > 0 && deflated > 0 && stored > 0, true);
  auto zso = entries(form("ZSO"), 24, blocks + 1);
  CHECK(std::count_if(zso.begin(), zso.end() - 1, [](u32 e) { return e >> 31; }) > 0, true);
  CHECK(std::count_if(zso.begin(), zso.end() - 1, [](u32 e) { return !(e >> 31); }) > 0, true);
  auto jso = entries(form("JSO (LZO)"), 48, blocks + 1);
  u32 storedJso = 0;
  for(u32 n = 0; n + 1 < jso.size(); n++) storedJso += jso[n + 1] - jso[n] == 2048;
  CHECK(storedJso > 0 && storedJso + 1 < jso.size(), true);
  auto& dax = form("DAX with uncompressed areas");
  u32 frames = (iso.size() + 8191) / 8192;
  for(u32 n : {0u, 1u, 5u}) CHECK(get16(dax.data() + 32 + 4 * frames + 2 * n), 8192);
  CHECK(get16(dax.data() + 32 + 4 * frames + 2 * 2) < 8192, true);
}

//Damaged images: refused when their headers or tables are wrong; when a block is, that block's reads fail, and the
//others' still work.
static auto discFormatsDamaged() -> void {
  auto image = formatsDisc();
  auto& iso = image.bytes;
  u32 blocks = iso.size() / 2048;
  std::string error;
  auto refused = [&](const std::vector<u8>& bytes, const std::string& why) {
    error.clear();
    return openImage(bytes, error) == nullptr && error == why;
  };
  //reads of the disc's sectors, the damaged block's failing
  auto readsBut = [&](const std::vector<u8>& bytes, u32 damaged, u32 blockSize) {
    auto disc = openImage(bytes, error);
    if(!disc) return false;
    std::vector<u8> data(blockSize);
    bool bad = disc->read(u64(damaged) * blockSize, blockSize, data.data());
    u32 other = damaged ? 0 : 1;
    bool good = disc->read(u64(other) * blockSize, blockSize, data.data());
    return !bad && good && !memcmp(data.data(), iso.data() + u64(other) * blockSize, blockSize);
  };

  //CSO version 2: an LZ4 block made of bytes saying its literals run past its end
  auto cso = disc_image::makeCso2(iso);
  for(u32 n = 0; n < blocks; n++) {
    u32 entry = get32(cso.data() + 24 + 4 * n);
    u32 next = get32(cso.data() + 24 + 4 * (n + 1)) & 0x7fff'ffff;
    if(!(entry >> 31) || next - (entry & 0x7fff'ffff) >= 2048) continue;
    memset(cso.data() + (entry & 0x7fff'ffff), 0xff, next - (entry & 0x7fff'ffff));
    CHECK(readsBut(cso, n, 2048), true);
    break;
  }
  //ZSO: an index going back
  auto zso = disc_image::makeZso(iso);
  disc_image::put32(zso.data() + 24 + 4 * 3, get32(zso.data() + 24 + 4 * 2) - 1);
  CHECK(refused(zso, "the ZSO's index is damaged"), true);
  //DAX: cut short in its tables; a frame past the file's end; a frame whose zlib header is wrong
  auto dax = disc_image::makeDax(iso);
  CHECK(refused(std::vector<u8>(dax.begin(), dax.begin() + 40), "the DAX's tables are cut short"), true);
  auto pastEnd = dax;
  disc_image::put32(pastEnd.data() + 32 + 4 * 1, dax.size() - 4);
  CHECK(refused(pastEnd, "the DAX's index is damaged"), true);
  auto badFrame = dax;
  badFrame[get32(dax.data() + 32 + 4 * 3)] ^= 0x01;  //its header no longer a multiple of 31
  CHECK(readsBut(badFrame, 3, 8192), true);
  //DAX's uncompressed areas: more of them than frames (a damaged count, refused before its table is read), one
  //running past the last frame, and two whose frames add up to more than the disc has
  u32 frames = (iso.size() + 8191) / 8192;
  auto manyAreas = disc_image::makeDax(iso, {{0, 1}});
  disc_image::put32(manyAreas.data() + 12, 0x0100'0001);
  CHECK(refused(manyAreas, "the DAX's uncompressed areas are damaged"), true);
  CHECK(refused(disc_image::makeDax(iso, {{frames - 1, 2}}), "the DAX's uncompressed areas are damaged"), true);
  CHECK(refused(disc_image::makeDax(iso, {{0, frames}, {0, 1}}), "the DAX's uncompressed areas are damaged"), true);
  //JSO: block headers, which aren't read; a packing that isn't LZO or zlib; a block bigger than a block; a damaged
  //LZO block
  auto jso = disc_image::makeJso(iso, 2048, true);
  auto headers = jso;
  headers[8] = 1;
  CHECK(refused(headers, "the JSO has block headers, which aren't read yet"), true);
  auto method = jso;
  method[10] = 2;
  CHECK(refused(method, "the JSO's header is damaged"), true);
  auto gap = jso;
  disc_image::put32(gap.data() + 48 + 4 * 1, get32(jso.data() + 48) + 2049);
  CHECK(refused(gap, "the JSO's index is damaged"), true);
  for(u32 n = 0; n < blocks; n++) {
    u32 start = get32(jso.data() + 48 + 4 * n), end = get32(jso.data() + 52 + 4 * n);
    if(end - start >= 2048) continue;
    auto damaged = jso;
    damaged[end - 1] = 0x01;  //its end marker made a match 16 KiB back, before the block's start
    CHECK(readsBut(damaged, n, 2048), true);
    break;
  }
}

//The unpackers: the reference packers' blocks and the hand-built LZO streams, unpacked exactly; cut short at every
//length, or into too little room, never all of the block; and matches reaching back before the start refused.
static auto unpackers() -> void {
  struct Vector { const char* name; const std::vector<u8>& packed; std::vector<u8> expected; bool lzo; };
  std::vector<u8> farExpected(2052);
  for(u32 n = 0; n < 2052; n++) farExpected[n] = u8(n * 7 + n / 251);
  farExpected.insert(farExpected.end(), farExpected.begin() + 3, farExpected.begin() + 6);
  auto far = unpack_vectors::lzoFarMatch();
  std::vector<Vector> vectors = {
    {"LZO 2 KiB", unpack_vectors::Lzo2048, unpack_vectors::input(0), true},
    {"LZO 40000", unpack_vectors::Lzo40000, unpack_vectors::input(1), true},
    {"LZO 50000", unpack_vectors::Lzo50000, unpack_vectors::input(2), true},
    {"LZ4 2 KiB", unpack_vectors::Lz42048, unpack_vectors::input(0), false},
    {"LZ4 40000", unpack_vectors::Lz440000, unpack_vectors::input(1), false},
    {"LZ4 50000", unpack_vectors::Lz450000, unpack_vectors::input(2), false},
    {"LZO 2-byte match", unpack_vectors::LzoShortMatch, {'a', 'b', 'c', 'a', 'b'}, true},
    {"LZO 3-byte match 2049 back", far, farExpected, true},
  };
  for(auto& v : vectors) {
    auto unpack = v.lzo ? unpackLZO : unpackLZ4;
    u32 size = v.expected.size(), packed = v.packed.size();
    std::vector<u8> out(size);
    bool exact = unpack(v.packed.data(), packed, out.data(), size) == size && out == v.expected;
    CHECK(exact, true);
    if(!exact) std::printf("  %s: not unpacked exactly\n", v.name);
    bool shortOk = true;
    for(u32 cut = 0; cut < packed; cut++) {
      std::vector<u8> part(v.packed.begin(), v.packed.begin() + cut);  //its own buffer, so a read past it shows
      if(unpack(part.data(), cut, out.data(), size) == size) shortOk = false;
    }
    CHECK(shortOk, true);
    std::vector<u8> small(size - 1);
    CHECK(unpack(v.packed.data(), packed, small.data(), small.size()), 0);
  }
  //a match with nothing before it: LZ4's first sequence, no literals, 1 back; LZO's first instruction a match 16 KiB
  //back; and LZO's 2-byte match reaching 4 back after 3 literals
  u8 out[64];
  u8 lz4[] = {0x04, 0x01, 0x00, 0x10, 'x'};
  CHECK(unpackLZ4(lz4, sizeof(lz4), out, sizeof(out)), 0);
  u8 lzo[] = {0x11, 0x04, 0x00, 0x11, 0x00, 0x00};
  CHECK(unpackLZO(lzo, sizeof(lzo), out, sizeof(out)), 0);
  u8 lzoBack[] = {20, 'a', 'b', 'c', 0x0c, 0x00, 0x11, 0x00, 0x00};
  CHECK(unpackLZO(lzoBack, sizeof(lzoBack), out, sizeof(out)), 0);
  //an LZ4 block padded out to an alignment (a CSO's or a ZSO's): it ends once the output is whole, as NULs or 'X's
  //after it would be read as a sequence with a bad distance
  for(u8 pad : {u8(0), u8('X')}) {
    auto padded = unpack_vectors::Lz42048;
    padded.insert(padded.end(), 3, pad);
    std::vector<u8> whole(2048);
    CHECK(unpackLZ4(padded.data(), padded.size(), whole.data(), whole.size()) == 2048
          && whole == unpack_vectors::input(0), true);
  }
  //LZ4's distance of 0, which no packer writes
  u8 zero[] = {0x10, 'a', 0x00, 0x00, 0x10, 'b'};
  CHECK(unpackLZ4(zero, sizeof(zero), out, sizeof(out)), 0);
  //a count going on past the whole output in its 255s
  std::vector<u8> endless(300, 0xff);
  endless[0] = 0xf0;
  CHECK(unpackLZ4(endless.data(), endless.size(), out, sizeof(out)), 0);
  std::vector<u8> zeros(300, 0x00);
  CHECK(unpackLZO(zeros.data(), zeros.size(), out, sizeof(out)), 0);
}

auto discFormatTests() -> Tests {
  return {{"disc formats", discFormats}, {"disc formats damaged", discFormatsDamaged}, {"unpackers", unpackers}};
}

}
