#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nall/stdint.hpp>

namespace phobos::desktop {

//A PSP disc's title, disc ID and icon, cached beside the settings (the data folder's psp-icons/), keyed by the
//SHA-256 of the file's path+size+mtime, as the Android app's psp-icons/ cache: a second visit doesn't re-open
//the disc. Its files go beside the settings, where the data folder keeps the saves and the states.
struct PspIconCache {
  explicit PspIconCache(std::string directory);

  //The title and disc ID cached for the file (its path, size and mtime); empty when there's no cache for it.
  auto info(const std::string& path) -> std::optional<std::pair<std::string, std::string>>;

  //Caches the disc's title, disc ID and icon (its ICON0.PNG's bytes) under the file's key, beside the settings.
  auto store(const std::string& path, const std::string& title, const std::string& discId,
             const std::vector<u8>& icon) -> void;

private:
  std::string directory;
};

//The cache key: the SHA-256 of the file's path+size+mtime (the Android app's is its URI+size+mtime).
auto pspIconKey(const std::string& path, u64 size, u64 mtime) -> std::string;

//A PSP disc image the list reads its title from: one of the disc's formats (ISO, CSO, ZSO, DAX, JSO, CHD), not
//a PBP (whose title is its folder's name) nor an ELF (a homebrew program, with no disc to read).
auto pspDiscImage(const std::string& file) -> bool;

//The file's title for the list: the disc's own title (its PARAM.SFO's TITLE), from the cache, or read off the
//image by the shared reader (a few sectors at a time, every size bounded) and cached beside its icon. Empty
//when there's no PSP game, the file can't be opened, or the scan is cancelled (a rescan supersedes it).
auto pspDiscTitle(const std::string& file, PspIconCache& cache,
                  bool quitting, int generation, std::atomic<int>& scanGeneration,
                  bool (*cancelled)(bool, int, std::atomic<int>&)) -> std::string;

//The title's characters the list's 8x8 font can draw (printable ASCII), the rest dropped; empty — the file's
//name is shown instead — when the title has none of them.
auto listTitle(const std::string& title) -> std::string;

}
