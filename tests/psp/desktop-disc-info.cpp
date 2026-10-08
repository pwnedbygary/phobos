//The desktop Library's PSP disc title and icon cache (desktop/PspDiscInfo.cpp): the cache key (the SHA-256 of
//the file's path+size+mtime), the cache's round trip (the title and disc ID beside the icon's bytes), the title
//the list shows (the disc's title when there's one, read off the image and then served from the cache), and a
//title the list's font can't draw (shown as the file's name instead).
#include "system.hpp"
#include "disc-image.hpp"

#include <ares/psp/kernel/disc-info.hpp>
#include <desktop/PspDiscInfo.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace allegrex_test::psp {

using phobos::desktop::PspIconCache;
using phobos::desktop::listTitle;
using phobos::desktop::pspDiscImage;
using phobos::desktop::pspDiscTitle;
using phobos::desktop::pspIconKey;

//A minimal PARAM.SFO with TITLE and DISC_ID (as tests/psp/disc-info.cpp's).
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

//A disc's image with a PARAM.SFO (title and disc ID) and an ICON0.PNG, laid out as a PSP's discs lay them.
static auto gameDisc(const std::string& title, const std::string& discId) -> disc_image::Image {
  return disc_image::makeIso({
    {"PSP_GAME/PARAM.SFO", makeParamSFO(title, discId)},
    {"PSP_GAME/SYSDIR/EBOOT.BIN", {1, 2, 3, 4}},
    {"PSP_GAME/ICON0.PNG", {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 1, 2, 3, 4}},
  });
}

//The temporary folder the test's files go in (fresh each run), removed when the group's done.
static auto testFolder() -> std::filesystem::path {
  static auto folder = std::filesystem::temp_directory_path() / "phobos-desktop-disc-tests";
  std::error_code error;
  std::filesystem::remove_all(folder, error);
  std::filesystem::create_directories(folder, error);
  return folder;
}

//A host file in the test's folder, as the Library keeps its paths (absolute).
static auto tempFile(const std::filesystem::path& folder, const std::string& name, const std::vector<u8>& bytes) -> std::string {
  auto path = folder / name;
  {
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
  }
  return path.string();
}

//The cache key: the SHA-256 of the file's path+size+mtime; it changes when any of them changes.
static auto iconKeys() -> void {
  auto key = pspIconKey("/games/a.iso", 100, 200);
  CHECK(key.size() == 64, true);
  for(auto c : key) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), true);
  CHECK(pspIconKey("/games/a.iso", 100, 200) == key, true);
  CHECK(pspIconKey("/games/a.iso", 101, 200) != key, true);  //the size changed
  CHECK(pspIconKey("/games/a.iso", 100, 201) != key, true);  //the mtime changed
  CHECK(pspIconKey("/games/b.iso", 100, 200) != key, true);  //the path changed
}

//A PSP disc image: the disc's formats, not a PBP (named by its folder) nor an ELF (a homebrew program).
static auto discImages() -> void {
  CHECK(pspDiscImage("a.iso"), true);
  CHECK(pspDiscImage("b.CHD"), true);
  CHECK(pspDiscImage("c.cso"), true);
  CHECK(pspDiscImage("d.zso"), true);
  CHECK(pspDiscImage("e.dax"), true);
  CHECK(pspDiscImage("f.jso"), true);
  CHECK(pspDiscImage("g.pbp"), false);
  CHECK(pspDiscImage("h.elf"), false);
  CHECK(pspDiscImage("i.bin"), false);
  CHECK(pspDiscImage("noext"), false);
}

//A title the list's font can draw: its characters printable ASCII, else none (the file's name is shown).
static auto listTitles() -> void {
  CHECK(listTitle("Test Game") == "Test Game", true);
  CHECK(listTitle("A B-C_d.e") == "A B-C_d.e", true);
  CHECK(listTitle("").empty(), true);
  CHECK(listTitle(std::string({'T', 'e', 's', 't', (char)0xE5, (char)0x90, (char)0x8D})).empty(), true);  //a UTF-8 title
  CHECK(listTitle(std::string({'A', 0x01, 'B'})).empty(), true);  //a control
}

//The cache's round trip: the title and disc ID beside the icon's bytes, under the file's key; a changed file
//(its mtime touched) has no cache.
static auto cacheRoundTrip() -> void {
  auto folder = testFolder();
  auto file = tempFile(folder, "round.iso", gameDisc("Test Game", "ULUS10025").bytes);
  auto cache = PspIconCache((folder / "cache").string());
  auto icon = std::vector<u8>{1, 2, 3, 4};
  cache.store(file, "Test Game", "ULUS10025", icon);
  auto cached = cache.info(file);
  CHECK(cached.has_value(), true);
  CHECK(cached->first == "Test Game", true);
  CHECK(cached->second == "ULUS10025", true);
  //the files, beside the settings, under the key
  auto size = (u64)std::filesystem::file_size(folder / "round.iso");
  auto mtime = (u64)std::filesystem::last_write_time(folder / "round.iso").time_since_epoch().count();
  auto key = pspIconKey(file, size, mtime);
  CHECK(std::filesystem::exists((folder / "cache" / (key + ".info")).string()), true);
  CHECK(std::filesystem::exists((folder / "cache" / (key + ".png")).string()), true);
  std::ifstream in((folder / "cache" / (key + ".png")).string(), std::ios::binary);
  std::string data;
  in.seekg(0, std::ios::end);
  data.resize((std::size_t)in.tellg());
  in.seekg(0);
  in.read(&data[0], (std::streamsize)data.size());
  CHECK(data == std::string((const char*)icon.data(), icon.size()), true);
  //the file's mtime touched: no cache for it (the key's changed)
  auto stamp = std::filesystem::last_write_time(folder / "round.iso");
  std::filesystem::last_write_time(folder / "round.iso", stamp + std::chrono::seconds(1));
  CHECK(cache.info(file).has_value(), false);
}

//The title from the image, and from the cache after: the image's bytes changed (its stamp kept), and the title
//still comes — the disc wasn't re-opened. A file that's no PSP game's has no title.
static auto titles() -> void {
  auto folder = testFolder();
  auto file = tempFile(folder, "title.iso", gameDisc("Test Game", "ULUS10025").bytes);
  auto stamp = std::filesystem::last_write_time(folder / "title.iso");
  auto cache = PspIconCache((folder / "cache").string());
  CHECK(pspDiscTitle(file, cache) == "Test Game", true);  //read off the image, and cached
  //the image's bytes erased (its size and mtime kept): a re-open would find no game
  auto size = (std::size_t)std::filesystem::file_size(folder / "title.iso");
  {
    std::ofstream out(folder / "title.iso", std::ios::binary);
    out.write(std::string(size, 0).data(), (std::streamsize)size);
  }
  std::filesystem::last_write_time(folder / "title.iso", stamp);
  CHECK(pspDiscTitle(file, cache) == "Test Game", true);  //from the cache: the disc isn't re-opened
  //a file that's no PSP game's: no title
  auto junk = tempFile(folder, "junk.iso", std::vector<u8>{1, 2, 3, 4});
  CHECK(pspDiscTitle(junk, cache).empty(), true);
}

auto desktopDiscInfoTests() -> Tests {
  return {
    {"desktop disc info keys", iconKeys},
    {"desktop disc image files", discImages},
    {"desktop disc info titles", listTitles},
    {"desktop disc info cache", cacheRoundTrip},
    {"desktop disc info from image", titles},
  };
}

}
