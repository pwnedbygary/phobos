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
  // The PSP disc's own title (its PARAM.SFO's TITLE), filled in after the list shows; empty when there's none.
  // The states and saves are keyed by `title` (the file's name), never by this.
  std::string discTitle;
};

// The games in `folder` and the two levels of folders inside it, sorted by title. A folder
// named after a system (psx, psp, megacd, msx, neogeo...) decides between systems that share an
// extension, but PSP games are told from the PlayStation's by what's in them; cue sheets and
// .m3u playlists hide the files they list.
auto scanLibrary(const std::string& folder) -> std::vector<Game>;

// The game a single file is (for `phobos <file>` from a frontend); its folder's name can
// decide the system, as in the library. Empty when no system loads the file.
auto gameForFile(const std::string& file) -> std::optional<Game>;

// Copies of the Android app's RomNames.kt helpers.
auto romTitle(const std::string& fileName) -> std::string;
auto withoutDiscNumber(const std::string& title) -> std::string;
// A copy of the app's LaunchSystems.pspProgramName(): a PSP program in an EBOOT.PBP goes by its
// folder's name ("Cube/EBOOT.PBP" is "Cube.pbp"), so homebrew doesn't share one name and its states.
auto pspProgramName(const std::string& fileName, const std::string& folderName) -> std::string;

}
