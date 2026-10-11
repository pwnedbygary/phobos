#include "Library.hpp"
#include "Platform.hpp"
#include "PhobosRunner.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace phobos::desktop {

static auto lower(std::string text) -> std::string {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return text;
}

// The extensions each system loads, as the Android app lists them (systemExtensions in
// PhobosJNI.cpp), plus .zip for Neo Geo, whose games are zipped ROM sets.
static const std::vector<std::pair<std::string, std::vector<std::string>>> systemExtensions = {
  {"Atari 2600", {"a26", "bin"}},
  {"ColecoVision", {"col", "cv"}},
  {"Famicom", {"fc", "nes", "unf", "unif", "unh", "fds"}},
  {"Super Famicom", {"sfc", "smc", "swc", "fig", "bs", "st"}},
  {"Super Game Boy", {"gb"}},
  {"Arcade", {"zip"}},
  {"Nintendo 64", {"n64", "v64", "z64", "n64dd", "ndd", "d64"}},
  {"Game Boy", {"gb"}},
  {"Game Boy Color", {"gb", "gbc", "nbc"}},
  {"Game Boy Advance", {"gba"}},
  {"SG-1000", {"sg1000", "sg"}},
  {"Master System", {"ms", "sms"}},
  {"Mega Drive", {"md", "gen", "bin"}},
  {"Mega 32X", {"32x", "bin"}},
  {"Game Gear", {"gg"}},
  {"Mega CD", {"cue", "chd", "iso"}},
  {"Mega CD 32X", {"cue", "chd", "iso"}},
  {"PlayStation", {"cue", "chd", "exe", "ps-exe", "pbp", "iso", "mdf", "img"}},
  {"PlayStation Portable", {"iso", "cso", "zso", "dax", "jso", "chd", "pbp", "elf"}},
  {"Neo Geo", {"ng", "neo", "zip"}},
  {"Neo Geo CD", {"ngc", "cue", "chd", "iso", "bin", "zip"}},
  {"Neo Geo Pocket", {"ngp", "nap"}},
  {"Neo Geo Pocket Color", {"ngpc", "ngc", "nbc"}},
  {"ZX Spectrum", {"wav", "tzx", "tap"}},
  {"ZX Spectrum 128", {"wav", "tzx", "tap"}},
  {"PC Engine", {"pce", "tg16"}},
  {"PC Engine CD", {"cue", "chd"}},
  {"SuperGrafx", {"sgx"}},
  {"WonderSwan", {"ws"}},
  {"WonderSwan Color", {"wsc"}},
  {"MSX", {"msx", "rom", "wav", "tzx", "tsx", "cas"}},
  {"MSX2", {"msx2", "rom", "wav", "tzx", "tsx", "cas"}},
};

// The system for an extension several systems share, when no folder name decides.
static const std::map<std::string, std::string> sharedExtensionDefaults = {
  {"bin", "Mega Drive"},
  {"gb", "Game Boy"},
  {"nbc", "Game Boy Color"},
  {"ngc", "Neo Geo Pocket Color"},
  {"cue", "PlayStation"},
  {"chd", "PlayStation"},
  {"iso", "PlayStation"},
  {"pbp", "PlayStation"},
  {"zip", "Auto"},
  {"wav", "ZX Spectrum"},
  {"tzx", "ZX Spectrum"},
  {"tap", "ZX Spectrum"},
  {"rom", "MSX"},
  {"tsx", "MSX"},
  {"cas", "MSX"},
};

// Folder names (lowercased, without spaces, dashes or underscores) that name a system.
static const std::map<std::string, std::string> folderSystems = {
  {"a26", "Atari 2600"}, {"atari2600", "Atari 2600"}, {"2600", "Atari 2600"},
  {"coleco", "ColecoVision"}, {"colecovision", "ColecoVision"},
  {"nes", "Famicom"}, {"famicom", "Famicom"}, {"fds", "Famicom"},
  {"snes", "Super Famicom"}, {"sfc", "Super Famicom"}, {"superfamicom", "Super Famicom"}, {"supernintendo", "Super Famicom"},
  {"sgb", "Super Game Boy"}, {"supergameboy", "Super Game Boy"},
  {"arcade", "Arcade"}, {"mame", "Arcade"}, {"fbneo", "Arcade"},
  {"n64", "Nintendo 64"}, {"nintendo64", "Nintendo 64"}, {"n64dd", "Nintendo 64"},
  {"gb", "Game Boy"}, {"gameboy", "Game Boy"},
  {"gbc", "Game Boy Color"}, {"gameboycolor", "Game Boy Color"},
  {"gba", "Game Boy Advance"}, {"gameboyadvance", "Game Boy Advance"},
  {"sg1000", "SG-1000"},
  {"sms", "Master System"}, {"mastersystem", "Master System"},
  {"md", "Mega Drive"}, {"megadrive", "Mega Drive"}, {"genesis", "Mega Drive"},
  {"32x", "Mega 32X"}, {"sega32x", "Mega 32X"}, {"mega32x", "Mega 32X"},
  {"gg", "Game Gear"}, {"gamegear", "Game Gear"},
  {"megacd", "Mega CD"}, {"segacd", "Mega CD"}, {"mcd", "Mega CD"},
  {"megacd32x", "Mega CD 32X"}, {"segacd32x", "Mega CD 32X"},
  {"psx", "PlayStation"}, {"ps1", "PlayStation"}, {"playstation", "PlayStation"},
  {"psp", "PlayStation Portable"}, {"playstationportable", "PlayStation Portable"},
  {"neogeo", "Neo Geo"}, {"mvs", "Neo Geo"}, {"aes", "Neo Geo"},
  {"neogeocd", "Neo Geo CD"}, {"ngcd", "Neo Geo CD"},
  {"ngp", "Neo Geo Pocket"}, {"neogeopocket", "Neo Geo Pocket"},
  {"ngpc", "Neo Geo Pocket Color"}, {"neogeopocketcolor", "Neo Geo Pocket Color"},
  {"zx", "ZX Spectrum"}, {"zxspectrum", "ZX Spectrum"}, {"spectrum", "ZX Spectrum"},
  {"zx128", "ZX Spectrum 128"}, {"zxspectrum128", "ZX Spectrum 128"},
  {"pce", "PC Engine"}, {"pcengine", "PC Engine"}, {"tg16", "PC Engine"}, {"turbografx16", "PC Engine"},
  {"pcecd", "PC Engine CD"}, {"pcenginecd", "PC Engine CD"}, {"tgcd", "PC Engine CD"}, {"turbografxcd", "PC Engine CD"},
  {"sgx", "SuperGrafx"}, {"supergrafx", "SuperGrafx"},
  {"ws", "WonderSwan"}, {"wonderswan", "WonderSwan"},
  {"wsc", "WonderSwan Color"}, {"wonderswancolor", "WonderSwan Color"},
  {"msx", "MSX"}, {"msx1", "MSX"}, {"msx2", "MSX2"},
};

static auto extensionOf(const fs::path& path) -> std::string {
  auto extension = fromPath(path.extension());
  if (!extension.empty() && extension[0] == '.') extension.erase(0, 1);
  return lower(extension);
}

static auto systemsFor(const std::string& extension) -> std::vector<std::string> {
  std::vector<std::string> systems;
  for (auto& [system, extensions] : systemExtensions) {
    if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end()) systems.push_back(system);
  }
  return systems;
}

static auto folderSystem(const fs::path& folder) -> std::optional<std::string> {
  std::string key;
  for (char c : lower(fromPath(folder.filename()))) {
    if (c != ' ' && c != '-' && c != '_') key += c;
  }
  auto it = folderSystems.find(key);
  if (it == folderSystems.end()) return std::nullopt;
  return it->second;
}

// The system a file belongs to, or nothing when no system loads its extension.
static auto systemFor(const fs::path& file, const fs::path& root) -> std::optional<std::string> {
  auto extension = extensionOf(file);
  if (extension == "m3u") return std::nullopt;
  auto systems = systemsFor(extension);
  if (systems.empty()) return std::nullopt;
  // The PlayStation's games share .iso, .chd and .pbp with the PSP's, which the PSP's medium tells by what's in them:
  // those are the PSP's wherever they are. Others go by the rules below, where only a psp folder still names the PSP
  // (whose medium then says what the file is when it's started).
  bool psp = std::find(systems.begin(), systems.end(), "PlayStation Portable") != systems.end();
  if (psp && systems.size() > 1 && ares::isPspGame(fromPath(file).c_str())) return "PlayStation Portable";
  for (auto folder = file.parent_path(); !folder.empty(); folder = folder.parent_path()) {
    if (auto named = folderSystem(folder)) {
      if (std::find(systems.begin(), systems.end(), *named) != systems.end()) return *named;
    }
    if (folder == root || folder == folder.parent_path()) break;
  }
  if (systems.size() == 1) return systems.front();
  auto shared = sharedExtensionDefaults.find(extension);
  return shared != sharedExtensionDefaults.end() ? shared->second : systems.front();
}

auto romTitle(const std::string& fileName) -> std::string {
  static const std::regex extension(R"(\.[A-Za-z0-9]{1,5}$)");
  auto title = std::regex_replace(fileName, extension, "");
  return title.empty() ? fileName : title;
}

auto pspProgramName(const std::string& fileName, const std::string& folderName) -> std::string {
  if (lower(fileName) != "eboot.pbp" || folderName.find_first_not_of(" \t") == std::string::npos) return fileName;
  return folderName + ".pbp";
}

// A game's title: its file's name without the extension, or for a PSP program in an EBOOT.PBP, its folder's name.
static auto titleOf(const fs::path& file, const std::string& system) -> std::string {
  auto name = fromPath(file.filename());
  if (system == "PlayStation Portable") name = pspProgramName(name, fromPath(file.parent_path().filename()));
  return romTitle(name);
}

auto withoutDiscNumber(const std::string& title) -> std::string {
  static const std::regex discNumber(R"(\s*[(\[]\s*(?:disc|disk|cd)\s*\d+(?:\s*of\s*\d+)?\s*[)\]])", std::regex::icase);
  auto stripped = std::regex_replace(title, discNumber, "");
  auto first = stripped.find_first_not_of(" \t");
  auto last = stripped.find_last_not_of(" \t");
  stripped = first == std::string::npos ? "" : stripped.substr(first, last - first + 1);
  return stripped.empty() ? title : stripped;
}

// The disc number in a file name ("Final Fantasy VII (USA) (Disc 2).chd" -> 2), or 0.
static auto discNumber(const std::string& fileName) -> int {
  static const std::regex discTag(R"([(\[]\s*(?:disc|disk|cd)\s*(\d+)(?:\s*of\s*\d+)?\s*[)\]])", std::regex::icase);
  std::smatch match;
  if (!std::regex_search(fileName, match, discTag)) return 0;
  return std::stoi(match[1].str());
}

static auto readLines(const fs::path& file) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  if (!lines.empty() && lines[0].rfind("\xEF\xBB\xBF", 0) == 0) lines[0].erase(0, 3);
  return lines;
}

// The track files a cue sheet names (FILE "Game (Track 1).bin" BINARY), resolved beside it.
static auto cueTracks(const fs::path& cue) -> std::vector<fs::path> {
  static const std::regex fileLine(R"cue(^\s*FILE\s+(?:"([^"]+)"|(\S+)).*$)cue", std::regex::icase);
  std::vector<fs::path> tracks;
  for (auto& line : readLines(cue)) {
    std::smatch match;
    if (!std::regex_match(line, match, fileLine)) continue;
    auto name = match[1].matched ? match[1].str() : match[2].str();
    std::replace(name.begin(), name.end(), '\\', '/');
    tracks.push_back((cue.parent_path() / toPath(name)).lexically_normal());
  }
  return tracks;
}

// The files an .m3u playlist lists, in order, resolved against the playlist's folder.
static auto m3uEntries(const fs::path& playlist) -> std::vector<fs::path> {
  std::vector<fs::path> entries;
  for (auto line : readLines(playlist)) {
    auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') continue;
    line = line.substr(first, line.find_last_not_of(" \t") - first + 1);
    std::replace(line.begin(), line.end(), '\\', '/');
    auto entry = toPath(line);
    entries.push_back(entry.is_absolute() ? entry : (playlist.parent_path() / entry).lexically_normal());
  }
  return entries;
}

auto scanLibrary(const std::string& folder) -> std::vector<Game> {
  std::vector<Game> games;
  fs::path root = toPath(folder);
  std::error_code error;
  if (folder.empty() || !fs::is_directory(root, error)) return games;

  std::vector<fs::path> files;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, error);
       !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
    auto name = fromPath(it->path().filename());
    if (!name.empty() && name[0] == '.') {
      if (it->is_directory(error)) it.disable_recursion_pending();
      continue;
    }
    if (it->is_directory(error)) {
      if (it.depth() >= 2) it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(error)) files.push_back(it->path().lexically_normal());
  }
  std::sort(files.begin(), files.end());

  // Compared without case: cue sheets made on Windows don't always match the files' case.
  std::set<std::string> listed;
  auto key = [](const fs::path& path) { return lower(fromPath(path)); };
  for (auto& file : files) {
    auto extension = extensionOf(file);
    if (extension == "cue") {
      for (auto& track : cueTracks(file)) listed.insert(key(track));
    } else if (extension == "m3u") {
      Game game{romTitle(fromPath(file.filename())), "", {}};
      for (auto& entry : m3uEntries(file)) {
        if (!fs::is_regular_file(entry, error)) continue;
        if (game.system.empty()) game.system = systemFor(entry, root).value_or("");
        game.discs.push_back(fromPath(entry));
        listed.insert(key(entry));
      }
      if (!game.discs.empty() && !game.system.empty()) games.push_back(game);
    }
  }

  // Discs of one game: files of one folder and format whose names differ only in the disc number.
  std::map<std::tuple<fs::path, std::string, std::string>, std::vector<std::pair<int, fs::path>>> discSets;
  std::vector<std::pair<fs::path, std::string>> singles;
  for (auto& file : files) {
    if (listed.count(key(file))) continue;
    auto system = systemFor(file, root);
    if (!system) continue;
    auto name = fromPath(file.filename());
    // (the PSP can't change discs yet, so its are each listed on their own, as in the Android app)
    if (int disc = *system == "PlayStation Portable" ? 0 : discNumber(name)) {
      auto set = std::make_tuple(file.parent_path(), lower(withoutDiscNumber(romTitle(name))), extensionOf(file));
      discSets[set].push_back({disc, file});
    } else {
      singles.push_back({file, *system});
    }
  }
  for (auto& [set, discs] : discSets) {
    if (discs.size() == 1) {
      auto& file = discs.front().second;
      singles.push_back({file, *systemFor(file, root)});
      continue;
    }
    std::sort(discs.begin(), discs.end());
    Game game{withoutDiscNumber(romTitle(fromPath(discs.front().second.filename()))), *systemFor(discs.front().second, root), {}};
    for (auto& [number, file] : discs) game.discs.push_back(fromPath(file));
    games.push_back(game);
  }
  for (auto& [file, system] : singles) {
    games.push_back({titleOf(file, system), system, {fromPath(file)}});
  }

  std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
    auto left = lower(a.title), right = lower(b.title);
    return left != right ? left < right : a.system < b.system;
  });
  return games;
}

auto gameForFile(const std::string& file) -> std::optional<Game> {
  auto path = toPath(file).lexically_normal();
  std::error_code error;
  if (!fs::is_regular_file(path, error)) return std::nullopt;
  if (extensionOf(path) == "m3u") {
    Game game{romTitle(fromPath(path.filename())), "", {}};
    for (auto& entry : m3uEntries(path)) {
      if (!fs::is_regular_file(entry, error)) continue;
      if (game.system.empty()) game.system = systemFor(entry, entry.parent_path()).value_or("");
      game.discs.push_back(fromPath(entry));
    }
    if (game.discs.empty() || game.system.empty()) return std::nullopt;
    return game;
  }
  auto system = systemFor(path, path.parent_path());
  if (!system) return std::nullopt;
  return Game{titleOf(path, *system), *system, {fromPath(path)}};
}

}
