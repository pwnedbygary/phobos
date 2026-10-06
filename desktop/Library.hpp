#pragma once
#include <optional>
#include <string>
#include <vector>

namespace phobos::desktop {

struct Game {
  // The file name without its extension; a multi-disc game's title has no disc number.
  std::string title;
  // A runner system name, or "Auto" to let mia identify the file.
  std::string system;
  // The files to load, in disc order; one for most games.
  std::vector<std::string> discs;
};

// The games in `folder` and the two levels of folders inside it, sorted by title. A folder
// named after a system (psx, megacd, msx, neogeo...) decides between systems that share an
// extension; cue sheets and .m3u playlists hide the files they list.
auto scanLibrary(const std::string& folder) -> std::vector<Game>;

// The game a single file is (for `phobos <file>` from a frontend); its folder's name can
// decide the system, as in the library. Empty when no system loads the file.
auto gameForFile(const std::string& file) -> std::optional<Game>;

// Copies of the Android app's RomNames.kt helpers.
auto romTitle(const std::string& fileName) -> std::string;
auto withoutDiscNumber(const std::string& title) -> std::string;

}
