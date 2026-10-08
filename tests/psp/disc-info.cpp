//The disc's title, disc ID, region and icon (ares/psp/kernel/disc-info.cpp): read off a synthetic ISO, a CSO, and
//a CHD; and off damaged images (no PSP_GAME, PARAM.SFO past the disc's end, an ICON0 over 1 MiB).
#include "system.hpp"
#include "disc-image.hpp"
#include "disc-formats.hpp"

#include <ares/psp/kernel/disc-info.hpp>

namespace allegrex_test::psp {

using ares::PlayStationPortable::Disc;
using ares::PlayStationPortable::DiscInfo;
using ares::PlayStationPortable::readDiscInfo;
using ares::PlayStationPortable::sfoValue;
using ares::PlayStationPortable::regionFromDiscId;

//A minimal PARAM.SFO with TITLE and DISC_ID: a "\0PSF" head, a key table of two entries (bytes 20-51),
//the key names (bytes 52+), and a value table.
static auto makeParamSFO(const std::string& title, const std::string& discId) -> std::vector<u8> {
  auto titleLen = u32(title.size()) + 1;
  auto idLen = u32(discId.size()) + 1;
  auto keys = u32(52);  //after the entry table (2 entries * 16 bytes)
  auto keyNamesSize = u32(6 + 8);  //"TITLE\0" + "DISC_ID\0"
  auto values = u32(52 + keyNamesSize);  //after the key names
  auto total = values + titleLen + idLen;
  std::vector<u8> sfo(total, 0);
  sfo[0] = '\0'; sfo[1] = 'P'; sfo[2] = 'S'; sfo[3] = 'F';
  sfo[4] = 1;  //version
  sfo[8] = u8(keys);  //keys offset
  sfo[12] = u8(values);  //values offset
  sfo[16] = 2;  //count
  //entry 0: TITLE (keyOffset at 20, format at 22, valueLength at 24, valueRoom at 28, valueOffset at 32)
  sfo[22] = 2;  //format: string
  sfo[24] = u8(titleLen);  //value length (bytes 24-27)
  sfo[28] = u8(titleLen);  //value room (bytes 28-31)
  //value offset is 0 (bytes 32-35, already 0)
  //entry 1: DISC_ID (keyOffset at 36, format at 38, valueLength at 40, valueRoom at 44, valueOffset at 48)
  sfo[36] = u8(6);  //key offset (past "TITLE\0")
  sfo[38] = 2;  //format: string
  sfo[40] = u8(idLen);  //value length (bytes 40-43)
  sfo[44] = u8(idLen);  //value room (bytes 44-47)
  sfo[48] = u8(titleLen);  //value offset (bytes 48-51)
  //key names: "TITLE\0DISC_ID\0"
  auto at = keys;
  for(auto c : std::string("TITLE")) sfo[at++] = u8(c);
  sfo[at++] = '\0';
  for(auto c : std::string("DISC_ID")) sfo[at++] = u8(c);
  sfo[at++] = '\0';
  //values: title\0discId\0
  at = values;
  for(auto c : title) sfo[at++] = u8(c);
  sfo[at++] = '\0';
  for(auto c : discId) sfo[at++] = u8(c);
  sfo[at++] = '\0';
  return sfo;
}

//A small PNG signature (the reader takes the icon's bytes as they are; it doesn't check the PNG's structure).
static auto iconBytes() -> std::vector<u8> {
  return {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0, 1, 2, 3, 4};
}

//A disc's image with a PARAM.SFO (title and disc ID) and an ICON0.PNG, laid out as a PSP's discs lay them.
static auto gameDisc(const std::string& title, const std::string& discId) -> disc_image::Image {
  return disc_image::makeIso({
    {"PSP_GAME/PARAM.SFO", makeParamSFO(title, discId)},
    {"PSP_GAME/SYSDIR/EBOOT.BIN", {1, 2, 3, 4}},
    {"PSP_GAME/ICON0.PNG", iconBytes()},
  });
}

//A Reader over an in-memory image, as the system reads one from a file.
static auto reader(const std::vector<u8>& bytes) -> Disc::Reader {
  return [bytes](u64 offset, void* out, u64 size) -> u64 {
    if(offset >= bytes.size()) return 0;
    size = std::min<u64>(size, bytes.size() - offset);
    memcpy(out, bytes.data() + offset, size);
    return size;
  };
}

//The reader, on an ISO and a CSO: the title, disc ID, region and icon all read.
static auto discInfoFormats() -> void {
  auto image = gameDisc("Test Game", "ULUS10025");
  std::string error;
  for(auto& [name, bytes] : {std::pair{"ISO", image.bytes},
                              std::pair{"CSO", disc_image::makeCso(image.bytes)}}) {
    auto info = readDiscInfo(reader(bytes), bytes.size(), error);
    CHECK(!error.empty(), false);
    CHECK(info.title == "Test Game", true);
    CHECK(info.discId == "ULUS10025", true);
    CHECK(info.region == "US", true);
    CHECK(info.icon == iconBytes(), true);
  }
}

//Region from the disc ID's third letter: J is Japan, U is the US, E is Europe, the rest unknown.
static auto regions() -> void {
  CHECK(regionFromDiscId("NPJH50634") == "Japan", true);
  CHECK(regionFromDiscId("ULUS10025") == "US", true);
  CHECK(regionFromDiscId("NLES00552") == "Europe", true);
  CHECK(regionFromDiscId("NPJH") == "Japan", true);  //short ID, third letter still J
  CHECK(regionFromDiscId("NP") == "Unknown", true);  //too short
  CHECK(regionFromDiscId("") == "Unknown", true);
  CHECK(regionFromDiscId("NPXX12345") == "Unknown", true);  //X isn't a shipped region
}

//A CHD: read through libchdr's hunks, or, without it, taken as none.
static auto chdDisc() -> void {
  auto image = gameDisc("CHD Game", "NPJH50634");
  auto chd = disc_image::makeChd(image.bytes);
  std::string error;
  auto info = readDiscInfo(reader(chd), chd.size(), error);
#if defined(ARES_ENABLE_CHD)
  CHECK(!error.empty(), false);
  CHECK(info.title == "CHD Game", true);
  CHECK(info.discId == "NPJH50634", true);
  CHECK(info.region == "Japan", true);
  CHECK(info.icon == iconBytes(), true);
#else
  CHECK(info.title.empty(), true);  //CHD support not built in: taken as none
  CHECK(info.discId.empty(), true);
#endif
}

//Damaged images: no PSP_GAME folder; a PARAM.SFO whose value's offset runs past the SFO's end; an ICON0 over 1 MiB.
static auto damagedDiscs() -> void {
  std::string error;
  //no PSP_GAME: a disc with files at the root only
  auto noGame = disc_image::makeIso({{"DATA.BIN", {1, 2, 3}}});
  auto info = readDiscInfo(reader(noGame.bytes), noGame.bytes.size(), error);
  CHECK(info.title.empty(), true);
  CHECK(info.discId.empty(), true);
  CHECK(info.icon.empty(), true);

  //a PARAM.SFO whose TITLE value's offset runs past the SFO's end: the value isn't read
  auto badOffset = makeParamSFO("Test Game", "ULUS10025");
  badOffset[32] = u8(badOffset.size());  //TITLE's value offset past the end
  auto disc = disc_image::makeIso({{"PSP_GAME/PARAM.SFO", badOffset}});
  info = readDiscInfo(reader(disc.bytes), disc.bytes.size(), error);
  CHECK(info.title.empty(), true);  //the value's past the end, so it's not read
  CHECK(info.discId == "ULUS10025", true);  //DISC_ID's value is fine

  //an ICON0 over 1 MiB: taken as none
  auto bigIcon = std::vector<u8>(2 * 1024 * 1024, 0);
  auto big = disc_image::makeIso({
    {"PSP_GAME/PARAM.SFO", makeParamSFO("Test Game", "ULUS10025")},
    {"PSP_GAME/ICON0.PNG", bigIcon},
  });
  info = readDiscInfo(reader(big.bytes), big.bytes.size(), error);
  CHECK(info.title == "Test Game", true);
  CHECK(info.discId == "ULUS10025", true);
  CHECK(info.icon.empty(), true);  //over 1 MiB: none

  //an empty image: no disc at all
  auto blank = std::vector<u8>(32 * 2048, 0);
  info = readDiscInfo(reader(blank), blank.size(), error);
  CHECK(info.title.empty(), true);
  CHECK(info.discId.empty(), true);
}

//The SFO reader alone: a value by its key's name, empty if the key isn't there.
static auto sfoValues() -> void {
  auto sfo = makeParamSFO("Test Game", "ULUS10025");
  CHECK(sfoValue(sfo, "TITLE") == "Test Game", true);
  CHECK(sfoValue(sfo, "DISC_ID") == "ULUS10025", true);
  CHECK(sfoValue(sfo, "NOSTAL").empty(), true);
  //a SFO with no keys
  auto noKeys = std::vector<u8>(20, 0);
  noKeys[0] = '\0'; noKeys[1] = 'P'; noKeys[2] = 'S'; noKeys[3] = 'F';
  CHECK(sfoValue(noKeys, "TITLE").empty(), true);
  //a SFO that's too short
  auto tooShort = std::vector<u8>(10, 0);
  CHECK(sfoValue(tooShort, "TITLE").empty(), true);
  //not a SFO at all
  auto notSfo = std::vector<u8>(30, 0);
  CHECK(sfoValue(notSfo, "TITLE").empty(), true);
}

auto discInfoTests() -> Tests {
  return {
    {"disc info formats", discInfoFormats},
    {"disc info chd", chdDisc},
    {"disc info regions", regions},
    {"disc info damaged", damagedDiscs},
    {"disc info sfo values", sfoValues},
  };
}

}
