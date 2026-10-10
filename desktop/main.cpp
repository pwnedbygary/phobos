#include "DesktopHost.hpp"
#include "Firmware.hpp"
#include "Input.hpp"
#include "Library.hpp"
#include "Platform.hpp"
#include "PspDiscInfo.hpp"
#include "Settings.hpp"
#include "PhobosHost.hpp"
#include "PhobosRunner.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace phobos::desktop;

namespace {

struct Color {
  Uint8 r, g, b, a;
};
constexpr Color backgroundColor{16, 18, 24, 255};
constexpr Color panelColor{27, 31, 42, 255};
constexpr Color highlightColor{47, 111, 223, 255};
constexpr Color textColor{230, 230, 230, 255};
constexpr Color dimColor{138, 144, 160, 255};
constexpr Color shadeColor{0, 0, 0, 170};
// SDL's debug font: 8 x 8 pixel characters, ASCII only.
constexpr float glyph = 8.0f;

enum class Screen { Library, Playing, Menu };

// What a folder picked in the system's dialog is for.
enum class Pick { Games, PspFonts, PspMemoryStick };

// The PSP's drawing-threads choices, as the Android app offers them (PspDrawingThreads): 0 is all cores but one.
constexpr int pspDrawingThreads[] = {0, 1, 2, 4, 6, 8};
// Who draws the PSP's pictures (the core's "Renderer"), as the app's PspRenderer: 0 software, 1 Vulkan.
constexpr const char* pspRenderers[] = {"Software", "Vulkan"};

struct MenuItem {
  std::string label;
  // 0 for A or Enter, -1 or +1 for left or right.
  std::function<void(int)> choose;
};

// A held direction steps once, then repeats after a short delay.
struct Repeat {
  Uint64 since = 0;
  Uint64 last = 0;
  auto step(bool held, Uint64 now) -> bool {
    if (!held) {
      since = 0;
      return false;
    }
    if (!since) {
      since = last = now;
      return true;
    }
    if (now - since < 350 || now - last < 70) return false;
    last = now;
    return true;
  }
};

struct Shell {
  auto run(int argc, char* argv[]) -> int;

private:
  auto setup(int argc, char* argv[]) -> bool;
  auto applySettings() -> void;
  auto rescan() -> void;
  auto chooseFolder(Pick pick) -> void;
  auto picked(Pick pick, const std::string& folder) -> void;
  auto open(const std::string& path) -> void;
  auto launch(const Game& entry) -> void;
  auto unloadGame() -> void;
  auto quitGame() -> void;
  auto openMenu() -> void;
  auto closeMenu() -> void;
  auto releaseKeys() -> void;
  auto statePath() const -> std::string;
  auto saveState() -> void;
  auto loadState() -> void;
  auto takeScreenshot() -> void;
  auto changeDisc(int direction) -> void;
  auto toggleFullscreen() -> void;
  auto menuItems() -> std::vector<MenuItem>;
  auto pspMenuItems(std::vector<MenuItem>& items) -> void;
  auto setPspFonts(const std::string& picked) -> std::string;
  auto handleKey(const SDL_KeyboardEvent& key) -> void;
  auto update() -> void;
  auto render() -> void;
  auto drawLibrary(float width, float height) -> void;
  auto drawMenu(float width, float height) -> void;
  auto fill(float x, float y, float w, float h, Color color) -> void;
  auto print(float x, float y, const std::string& line, Color color, float maxWidth = 0) -> void;
  auto show(const std::string& line) -> void;

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  bool vsync = false;
  SDL_Texture* frameTexture = nullptr;
  std::vector<std::uint32_t> pixels;
  std::uint32_t frameWidth = 0;
  std::uint32_t frameHeight = 0;
  std::uint64_t frameSerial = 0;
  // The whole multiple the runner was last told to draw the PSP's picture at (0: not yet told).
  int pictureMultiple = 0;
  // The PSP fonts' item, worked out as the setting is applied or changed: the folder may be a slow or vanished share,
  // which the menu, drawn each frame, mustn't list.
  std::string pspFontsHeld = "none";

  Settings settings;
  Input input;
  std::string dataFolder;
  std::string gamesFolder;
  std::string firmwareFolder;
  bool vulkanAvailable = false;
  // Started with a game file (by a frontend): leaving the game quits Phobos.
  bool launchedWithGame = false;

  std::vector<Game> games;
  int cursor = 0;
  Screen screen = Screen::Library;
  std::optional<Game> game;
  int disc = 0;
  int menuCursor = 0;
  int stateSlot = 1;
  bool fastForward = false;
  bool fastForwardKey = false;
  bool fastForwardApplied = false;
  bool muted = false;
  // Keys of the ZX Spectrum or MSX keyboard held, with how many host keys hold each.
  std::map<std::string, int> heldKeys;
  int previousButtons = 0;
  // Buttons still held from the library or the menu, kept from the game until released.
  int suppressedButtons = 0;
  Repeat up, down, left, right;

  std::string message;
  Uint64 messageUntil = 0;

  std::mutex pickedMutex;
  Pick picking = Pick::Games;
  std::optional<std::string> pickedFolder;
  bool pickerFailed = false;
  bool quit = false;

  // The PSP's titles, filled in after the list shows (PspDiscInfo): the thread that fills them, the scan's
  // generation (a rescan supersedes its unfinished work), and the lock the list, the launch and the fill share.
  std::mutex gamesMutex;
  std::atomic<int> scanGeneration{0};
  std::thread pspTitlesThread;
  std::atomic<bool> quitting{false};
};

// Boot ROMs (System) and game databases (Database) ship beside the program: in the build folder
// and the Windows zip, in usr/share/phobos in the AppImage, and in Phobos.app's Resources, which
// is SDL's base path there. Missing files are copied into the data folder the runner reads.
static auto installSystemFiles(const std::string& dataFolder) -> void {
  const char* base = SDL_GetBasePath();
  std::error_code error;
  for (auto relative : {"", "../share/phobos/"}) {
    auto resources = toPath(std::string(base ? base : "") + relative);
    if (!fs::is_directory(resources / "System", error)) continue;
    for (auto folder : {"Database", "System"}) {
      fs::copy(resources / folder, toPath(dataFolder) / folder, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
      if (error) SDL_Log("Couldn't copy %s into %s: %s", folder, dataFolder.c_str(), error.message().c_str());
    }
    return;
  }
  SDL_Log("No System folder beside Phobos: games that need its boot ROMs won't start");
}

auto Shell::run(int argc, char* argv[]) -> int {
  if (!setup(argc, argv)) return 1;
  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      input.handle(event);
      switch (event.type) {
      case SDL_EVENT_QUIT: quit = true; break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: handleKey(event.key); break;
      case SDL_EVENT_DROP_FILE: if (event.drop.data) open(event.drop.data); break;
      default: break;
      }
    }
    {
      std::lock_guard<std::mutex> lock(pickedMutex);
      if (pickedFolder) {
        picked(picking, *pickedFolder);
        pickedFolder.reset();
      }
      if (pickerFailed) {
        show(picking == Pick::Games ? "No folder picker here: start Phobos with the games folder as its argument"
                                    : "No folder picker here: set the folder in " + dataFolder + "settings.ini");
        pickerFailed = false;
      }
    }
    update();
    render();
    // Without vsync the loop would spin; ~120 Hz is plenty to pick up the runner's frames.
    if (!vsync) SDL_DelayNS(8'000'000);
  }
  if (game) quitGame();
  // The titles' thread, stopped and joined (it's at most one disc open in flight when it checks next).
  quitting = true;
  if (pspTitlesThread.joinable()) pspTitlesThread.join();
  input.close();
  if (frameTexture) SDL_DestroyTexture(frameTexture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

auto Shell::setup(int argc, char* argv[]) -> bool {
  attachParentConsole();
  SDL_SetAppMetadata("Phobos", nullptr, "com.phobos.emulator");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  // A machine without sound still runs games; the runner discards their audio.
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) SDL_Log("No audio: %s", SDL_GetError());

  char* pref = SDL_GetPrefPath(nullptr, "Phobos");
  dataFolder = pref ? pref : "./";
  SDL_free(pref);
  std::error_code error;
  for (auto folder : {"saves", "states", "firmware", "vulkan", "tmp"}) {
    fs::create_directories(toPath(dataFolder + folder), error);
  }
  installSystemFiles(dataFolder);
  settings.load(dataFolder + "settings.ini");
  gamesFolder = settings.text("games");
  firmwareFolder = settings.text("firmware", dataFolder + "firmware");

  ares::setHomePath(dataFolder.c_str());
  ares::setSavesPath((dataFolder + "saves").c_str());
  ares::setVulkanCachePath((dataFolder + "vulkan").c_str());
  ares::setTempFilePath((dataFolder + "tmp").c_str());
  vulkanAvailable = phobos::host::loadVulkan(nullptr, nullptr, nullptr);
  if (!vulkanAvailable) SDL_Log("No Vulkan loader found: Nintendo 64 games can't run");
  mapFirmware(firmwareFolder);

  window = SDL_CreateWindow("Phobos", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }
  renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return false;
  }
  vsync = SDL_SetRenderVSync(renderer, 1);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  if (settings.flag("fullscreen", false)) SDL_SetWindowFullscreen(window, true);
  input.openConnected();

  std::string argument = argc > 1 ? argv[1] : "";
  if (!argument.empty() && fs::is_directory(toPath(argument), error)) {
    gamesFolder = argument;
    settings.setText("games", gamesFolder);
    argument.clear();
  }
  rescan();
  if (!argument.empty()) {
    launchedWithGame = true;
    open(argument);
    launchedWithGame = game.has_value();
  }
  return true;
}

auto Shell::applySettings() -> void {
  ares::setFastForwardSpeed((float)settings.number("fastForwardSpeed", 2));
  ares::setN64Upscale(settings.number("n64.upscale", 1));
  ares::setN64Recompiler(settings.flag("n64.recompiler", true));
  ares::setN64ExpansionPak(settings.flag("n64.expansionPak", true));
  ares::setN64AsyncRdp(settings.flag("n64.asyncRdp", false));
  ares::setN64Pak(settings.text("n64.pak", "None").c_str());
  ares::setPs1AnalogMode(settings.flag("ps1.analog", true));
  ares::setVideoSettings(settings.flag("video.overscan", false), settings.flag("video.colorEmulation", true),
                         settings.flag("video.interframeBlending", true));
  // The PSP's memory stick (a folder picked, else the one all games share in the saves folder, as on Android), the
  // user's own system fonts (none unless a folder is picked), its drawing threads (0: all cores but one), its
  // renderer and the Vulkan renderer's internal resolution (1, native, to 10 times).
  ares::setPspMemoryStickPath(settings.text("psp.memoryStick").c_str());
  setPspFonts(settings.text("psp.fonts"));
  ares::setPspDrawingThreads(settings.number("psp.drawingThreads", 0));
  ares::setPspRenderer(settings.number("psp.renderer", 0));
  ares::setPspResolution(settings.number("psp.resolution", 1));
}

auto Shell::rescan() -> void {
  std::vector<std::string> files;
  {
    std::lock_guard<std::mutex> lock(gamesMutex);
    games = scanLibrary(gamesFolder);
    cursor = std::clamp(cursor, 0, std::max(0, (int)games.size() - 1));
    ++scanGeneration;
    for (auto& game : games)
      if (game.system == "PlayStation Portable" && pspDiscImage(game.discs.front()))
        files.push_back(game.discs.front());
  }
  // The PSP's titles, filled in after the list shows (a CHD's open takes a while, so the list isn't held for it),
  // on a thread of their own. A rescan supersedes its unfinished work: the old thread stops at its next check
  // and is joined (so it can't outlive the Shell) before the new one starts.
  if (pspTitlesThread.joinable()) pspTitlesThread.join();
  pspTitlesThread = std::thread([this, files, generation = scanGeneration.load()] {
    auto cache = PspIconCache(dataFolder + "psp-icons");
    for (auto& file : files) {
      if (quitting || generation != scanGeneration.load()) break;
      auto title = listTitle(pspDiscTitle(file, cache));
      std::lock_guard<std::mutex> lock(gamesMutex);
      if (generation != scanGeneration.load()) break;
      for (auto& game : games)
        if (game.discs.front() == file) game.discTitle = title;
    }
  });
}

auto Shell::chooseFolder(Pick pick) -> void {
  std::string from = pick == Pick::Games ? gamesFolder
                   : pick == Pick::PspFonts ? settings.text("psp.fonts") : settings.text("psp.memoryStick");
  {
    std::lock_guard<std::mutex> lock(pickedMutex);
    picking = pick;
  }
  SDL_ShowOpenFolderDialog([](void* userdata, const char* const* files, int) {
    auto* shell = (Shell*)userdata;
    std::lock_guard<std::mutex> lock(shell->pickedMutex);
    if (!files) shell->pickerFailed = true;
    else if (files[0]) shell->pickedFolder = std::string(files[0]);
  }, this, window, from.empty() ? nullptr : from.c_str(), false);
}

auto Shell::picked(Pick pick, const std::string& folder) -> void {
  switch (pick) {
  case Pick::Games:
    gamesFolder = folder;
    settings.setText("games", gamesFolder);
    rescan();
    return;
  case Pick::PspFonts: {
    if (pspFontFolder(folder).empty()) return show("No PSP fonts (jpn0, ltn0-ltn15, kr0.pgf) in that folder");
    settings.setText("psp.fonts", folder);
    return show(setPspFonts(folder) + " of the PSP's 18 fonts: they're read as a game starts");
  }
  case Pick::PspMemoryStick:
    settings.setText("psp.memoryStick", folder);
    ares::setPspMemoryStickPath(folder.c_str());
    return show("The PSP's memory stick is that folder from the next start");
  }
}

// A dropped or passed path: a folder becomes the games folder, a file starts.
auto Shell::open(const std::string& path) -> void {
  std::error_code error;
  if (fs::is_directory(toPath(path), error)) {
    gamesFolder = path;
    settings.setText("games", gamesFolder);
    rescan();
    return;
  }
  if (auto entry = gameForFile(path)) return launch(*entry);
  show("No system loads " + fromPath(toPath(path).filename()));
}

auto Shell::launch(const Game& entry) -> void {
  if (entry.system == "Nintendo 64" && !vulkanAvailable) {
    return show("Nintendo 64 games need a Vulkan driver, and none was found");
  }
  mapFirmware(firmwareFolder);
  auto missing = ares::missingFirmware(entry.system.c_str());
  if (!missing.empty()) {
    return show("Missing " + firmwareName((const char*)missing[0]) + ": put it in " + firmwareFolder);
  }
  // A game dropped on a running one: the runner's per-game keys below must not change until that one is saved.
  if (game) unloadGame();
  applySettings();
  const auto& file = entry.discs.front();
  ares::setMemoryCardKey(withoutDiscNumber(entry.title).c_str());
  ares::setPause(false);
  ares::setRomPath(file.c_str());
  auto fileName = fromPath(toPath(file).filename());
  if (entry.system == "PlayStation Portable") fileName = pspProgramName(fileName, fromPath(toPath(file).parent_path().filename()));
  if (!ares::initialize(entry.system.c_str(), file.c_str(), fileName.c_str())) {
    std::string problem = (const char*)ares::lastLoadProblem();
    return show(problem.empty() ? "Couldn't start " + entry.title + " (" + entry.system + ")" : problem);
  }
  game = entry;
  disc = 0;
  screen = Screen::Playing;
  frameSerial = 0;
  frameWidth = frameHeight = 0;
  pictureMultiple = 0;
  fastForward = fastForwardKey = fastForwardApplied = false;
  ares::setFastForward(false);
  muted = false;
  ares::setMuteAudio(false);
  suppressedButtons = previousButtons;
  SDL_SetWindowTitle(window, ("Phobos - " + entry.title).c_str());
}

// Saves and unloads the running game and goes back to the library.
auto Shell::unloadGame() -> void {
  releaseKeys();
  input.rumble(false);
  ares::setEmulationRunning(false);
  ares::unloadSystem();
  game.reset();
  screen = Screen::Library;
  SDL_SetWindowTitle(window, "Phobos");
}

auto Shell::quitGame() -> void {
  unloadGame();
  if (launchedWithGame) quit = true;
}

auto Shell::openMenu() -> void {
  screen = Screen::Menu;
  menuCursor = 0;
  fastForwardKey = false;
  releaseKeys();
  ares::setInput(0, 0, 0, 0, 0);
  ares::setPause(true);
}

auto Shell::closeMenu() -> void {
  screen = Screen::Playing;
  suppressedButtons = previousButtons;
  ares::setPause(false);
}

auto Shell::releaseKeys() -> void {
  for (auto& [label, count] : heldKeys) {
    if (count > 0) ares::setKeyboardKey(label.c_str(), false);
  }
  heldKeys.clear();
}

auto Shell::statePath() const -> std::string {
  auto folder = dataFolder + "states/" + game->system;
  std::error_code error;
  fs::create_directories(toPath(folder), error);
  return folder + "/" + game->title + " (" + std::to_string(stateSlot) + ").state";
}

auto Shell::saveState() -> void {
  if (!game) return;
  bool saved = ares::saveState(statePath().c_str());
  show(saved ? "Saved state " + std::to_string(stateSlot) : "Couldn't save state " + std::to_string(stateSlot));
}

auto Shell::loadState() -> void {
  if (!game) return;
  bool loaded = ares::loadState(statePath().c_str());
  show(loaded ? "Loaded state " + std::to_string(stateSlot) : "No state " + std::to_string(stateSlot) + " to load");
}

auto Shell::takeScreenshot() -> void {
  if (!game) return;
  // The core's own frame as a PNG, numbered after the last one so no earlier picture is replaced.
  auto folder = dataFolder + "screenshots/" + game->system;
  std::error_code error;
  fs::create_directories(toPath(folder), error);
  std::string path;
  for (int number = 1; path.empty() || fs::exists(toPath(path), error); number++) {
    path = folder + "/" + game->title + " (" + std::to_string(number) + ").png";
  }
  show(ares::takeScreenshot(path.c_str()) ? "Saved screenshot" : "Couldn't save a screenshot");
}

auto Shell::changeDisc(int direction) -> void {
  int count = (int)game->discs.size();
  int next = (disc + direction + count) % count;
  const auto& file = game->discs[next];
  ares::setSecondaryRomPath(file.c_str());
  if (ares::loadSecondaryRom(game->system.c_str(), file.c_str())) {
    disc = next;
    show("Disc " + std::to_string(disc + 1) + " is in the drive");
  } else {
    show("Couldn't change to disc " + std::to_string(next + 1));
  }
}

auto Shell::toggleFullscreen() -> void {
  bool fullscreen = !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
  SDL_SetWindowFullscreen(window, fullscreen);
  settings.setFlag("fullscreen", fullscreen);
}

auto Shell::menuItems() -> std::vector<MenuItem> {
  auto onOff = [](bool on) { return std::string(on ? "on" : "off"); };
  std::vector<MenuItem> items;
  items.push_back({"Resume", [this](int d) { if (d == 0) closeMenu(); }});
  items.push_back({"Save state", [this](int d) { if (d == 0) saveState(); }});
  items.push_back({"Load state", [this](int d) { if (d == 0) loadState(); }});
  items.push_back({"State slot: " + std::to_string(stateSlot), [this](int d) { stateSlot = (stateSlot - 1 + (d < 0 ? 8 : 1)) % 9 + 1; }});
  items.push_back({"Screenshot", [this](int d) { if (d == 0) takeScreenshot(); }});
  items.push_back({"Reset", [this](int d) { if (d == 0) { ares::resetSystem(); closeMenu(); } }});
  items.push_back({"Fast forward: " + onOff(fastForward), [this](int) { fastForward = !fastForward; }});
  items.push_back({"Mute: " + onOff(muted), [this](int) { muted = !muted; ares::setMuteAudio(muted); }});
  if (game->discs.size() > 1) {
    items.push_back({"Disc: " + std::to_string(disc + 1) + " of " + std::to_string(game->discs.size()),
                     [this](int d) { changeDisc(d < 0 ? -1 : 1); }});
  }
  if (game->system == "Nintendo 64") {
    items.push_back({"N64 upscale: " + std::to_string(settings.number("n64.upscale", 1)) + "x", [this](int d) {
      int upscale = (settings.number("n64.upscale", 1) - 1 + (d < 0 ? 3 : 1)) % 4 + 1;
      settings.setNumber("n64.upscale", upscale);
      ares::setN64Upscale(upscale);
    }});
    items.push_back({"N64 recompiler: " + onOff(settings.flag("n64.recompiler", true)) + " (after reset)", [this](int) {
      bool on = !settings.flag("n64.recompiler", true);
      settings.setFlag("n64.recompiler", on);
      ares::setN64Recompiler(on);
    }});
    items.push_back({"Asynchronous RDP: " + onOff(settings.flag("n64.asyncRdp", false)), [this](int) {
      bool on = !settings.flag("n64.asyncRdp", false);
      settings.setFlag("n64.asyncRdp", on);
      ares::setN64AsyncRdp(on);
    }});
    items.push_back({"Controller pak: " + settings.text("n64.pak", "None"), [this](int d) {
      static const std::vector<std::string> paks = {"None", "Rumble Pak", "Controller Pak"};
      auto it = std::find(paks.begin(), paks.end(), settings.text("n64.pak", "None"));
      int index = it == paks.end() ? 0 : (int)(it - paks.begin());
      auto pak = paks[(index + (d < 0 ? 2 : 1)) % 3];
      settings.setText("n64.pak", pak);
      ares::setN64Pak(pak.c_str());
    }});
  }
  if (game->system == "PlayStation") {
    items.push_back({"Analog controller: " + onOff(settings.flag("ps1.analog", true)), [this](int) {
      settings.setFlag("ps1.analog", ares::togglePs1AnalogMode());
    }});
  }
  if (game->system == "PlayStation Portable") pspMenuItems(items);
  items.push_back({"Fullscreen: " + onOff(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN), [this](int) { toggleFullscreen(); }});
  items.push_back({launchedWithGame ? "Quit" : "Quit to library", [this](int d) { if (d == 0) quitGame(); }});
  return items;
}

// Gives the runner the fonts in (or under) the folder picked, and keeps how many there are for the menu ("12 of 18",
// "none"); returns the count.
auto Shell::setPspFonts(const std::string& picked) -> std::string {
  auto fonts = pspFontFolder(picked);
  ares::setPspFontsPath(fonts.c_str());
  auto count = std::to_string(pspFontCount(fonts));
  pspFontsHeld = fonts.empty() ? std::string("none") : count + " of 18";
  return count;
}

// The PSP's settings, taken as a game starts. A picks a folder for the fonts or the memory stick; left or right
// goes back to none, or to the shared memory stick.
auto Shell::pspMenuItems(std::vector<MenuItem>& items) -> void {
  items.push_back({"PSP fonts: " + pspFontsHeld + " (next start)", [this](int d) {
    if (d == 0) return chooseFolder(Pick::PspFonts);
    settings.setText("psp.fonts", "");
    setPspFonts("");
  }});
  auto stick = settings.text("psp.memoryStick");
  auto stickName = stick.empty() ? std::string("shared") : fromPath(toPath(stick).filename());
  items.push_back({"PSP memory stick: " + stickName + " (next start)", [this](int d) {
    if (d == 0) return chooseFolder(Pick::PspMemoryStick);
    settings.setText("psp.memoryStick", "");
    ares::setPspMemoryStickPath("");
  }});
  int threads = settings.number("psp.drawingThreads", 0);
  items.push_back({"PSP drawing threads: " + (threads ? std::to_string(threads) : std::string("Auto")) + " (next start)",
                   [this, threads](int d) {
    constexpr int count = (int)std::size(pspDrawingThreads);
    auto at = std::find(std::begin(pspDrawingThreads), std::end(pspDrawingThreads), threads);
    int index = at == std::end(pspDrawingThreads) ? 0 : (int)(at - std::begin(pspDrawingThreads));
    int next = pspDrawingThreads[(index + (d < 0 ? count - 1 : 1)) % count];
    settings.setNumber("psp.drawingThreads", next);
    ares::setPspDrawingThreads(next);
  }});
  int renderer = std::clamp(settings.number("psp.renderer", 0), 0, (int)std::size(pspRenderers) - 1);
  items.push_back({std::string("PSP renderer: ") + pspRenderers[renderer] + " (next start)", [this, renderer](int d) {
    int next = (renderer + (d < 0 ? (int)std::size(pspRenderers) - 1 : 1)) % (int)std::size(pspRenderers);
    settings.setNumber("psp.renderer", next);
    ares::setPspRenderer(next);
  }});
  //(the window shows the GPU's picture read back at up to 4 times the PSP's size: the core's MostShown)
  int resolution = std::clamp(settings.number("psp.resolution", 1), 1, 10);
  items.push_back({"PSP resolution (Vulkan): " + (resolution == 1 ? std::string("Native") :
                   std::to_string(resolution) + "x") + " (next start)", [this, resolution](int d) {
    int next = (resolution - 1 + (d < 0 ? 9 : 1)) % 10 + 1;
    settings.setNumber("psp.resolution", next);
    ares::setPspResolution(next);
  }});
}

auto Shell::handleKey(const SDL_KeyboardEvent& key) -> void {
  bool computer = game && isKeyboardComputer(game->system);
  bool msx = game && (game->system == "MSX" || game->system == "MSX2");
  if (key.down && !key.repeat) {
    if (key.scancode == SDL_SCANCODE_F11) return toggleFullscreen();
    switch (screen) {
    case Screen::Library:
      if (key.scancode == SDL_SCANCODE_F3) chooseFolder(Pick::Games);
      if (key.scancode == SDL_SCANCODE_F5) rescan();
      return;
    case Screen::Menu:
      if (key.scancode == SDL_SCANCODE_ESCAPE || key.scancode == SDL_SCANCODE_BACKSPACE || key.scancode == SDL_SCANCODE_F12) closeMenu();
      return;
    case Screen::Playing:
      // The MSX keyboard has Escape and F1 to F5 of its own; F12 and the controller open the menu.
      if (key.scancode == SDL_SCANCODE_F12 || (key.scancode == SDL_SCANCODE_ESCAPE && !msx)) return openMenu();
      if (key.scancode == SDL_SCANCODE_F5 && !msx) return saveState();
      if (key.scancode == SDL_SCANCODE_F9) return loadState();
      if (key.scancode == SDL_SCANCODE_F8) return takeScreenshot();
      if (key.scancode == SDL_SCANCODE_TAB && !computer) {
        fastForwardKey = true;
        return;
      }
      break;
    }
  }
  if (!key.down && key.scancode == SDL_SCANCODE_TAB) fastForwardKey = false;
  if (screen != Screen::Playing || !computer || key.repeat) return;
  for (auto* label : keyboardLabels(game->system, key.scancode)) {
    int& count = heldKeys[label];
    if (key.down && count++ == 0) ares::setKeyboardKey(label, true);
    if (!key.down && count > 0 && --count == 0) ares::setKeyboardKey(label, false);
  }
}

auto Shell::update() -> void {
  // (the PSP's Vulkan renderer couldn't start, or stopped: said once, the software renderer drawing instead)
  if (game) {
    auto notice = ares::takePspNotice();
    if (!notice.empty()) show(notice);
  }
  bool computer = game && isKeyboardComputer(game->system);
  bool n64 = game && game->system == "Nintendo 64";
  auto pad = input.poll(screen != Screen::Playing || !computer, screen == Screen::Playing && n64);
  auto pressed = [&](int bit) { return (pad.buttons & bit) && !(previousButtons & bit); };
  Uint64 now = SDL_GetTicks();
  bool menuNavigation = screen != Screen::Playing;
  bool navUp = up.step(menuNavigation && ((pad.buttons & PadUp) || pad.ly < -0.5f), now);
  bool navDown = down.step(menuNavigation && ((pad.buttons & PadDown) || pad.ly > 0.5f), now);
  bool navLeft = left.step(menuNavigation && ((pad.buttons & PadLeft) || pad.lx < -0.5f), now);
  bool navRight = right.step(menuNavigation && ((pad.buttons & PadRight) || pad.lx > 0.5f), now);
  bool confirm = pressed(PadA) || pressed(PadStart);
  int buttons = pad.buttons;
  previousButtons = buttons;

  switch (screen) {
  case Screen::Playing: {
    // The Guide button, or Select and Start together, opens the menu.
    bool chord = (buttons & PadSelect) && (buttons & PadStart) && (pressed(PadSelect) || pressed(PadStart));
    if (pressed(PadHome) || chord) return openMenu();
    suppressedButtons &= buttons;
    ares::setInput(pad.lx, pad.ly, pad.rx, pad.ry, buttons & ~(suppressedButtons | PadHome));
    bool fast = fastForward || fastForwardKey;
    if (fast != fastForwardApplied) {
      ares::setFastForward(fast);
      fastForwardApplied = fast;
    }
    input.rumble(ares::getRumbleState());
    return;
  }
  case Screen::Menu: {
    auto items = menuItems();
    int count = (int)items.size();
    if (navUp) menuCursor = (menuCursor + count - 1) % count;
    if (navDown) menuCursor = (menuCursor + 1) % count;
    menuCursor = std::clamp(menuCursor, 0, count - 1);
    if (pressed(PadB) || pressed(PadHome)) return closeMenu();
    if (confirm) return items[menuCursor].choose(0);
    if (navLeft) return items[menuCursor].choose(-1);
    if (navRight) return items[menuCursor].choose(+1);
    return;
  }
  case Screen::Library: {
    if (pressed(PadY)) chooseFolder(Pick::Games);
    if (pressed(PadX)) rescan();
    Game entry;
    {
      std::lock_guard<std::mutex> lock(gamesMutex);
      int count = (int)games.size();
      if (!count) return;
      constexpr int page = 10;
      if (navUp) cursor = (cursor + count - 1) % count;
      if (navDown) cursor = (cursor + 1) % count;
      if (navLeft || pressed(PadL1)) cursor = std::max(0, cursor - page);
      if (navRight || pressed(PadR1)) cursor = std::min(count - 1, cursor + page);
      entry = games[cursor];
    }
    if (confirm) launch(entry);
    return;
  }
  }
}

auto Shell::render() -> void {
  int outputWidth = 0, outputHeight = 0;
  SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  fill(0, 0, (float)outputWidth, (float)outputHeight, backgroundColor);

  if (game && phobos::host::takeFrame(frameSerial, pixels, frameWidth, frameHeight)) {
    float textureWidth = 0, textureHeight = 0;
    if (frameTexture) SDL_GetTextureSize(frameTexture, &textureWidth, &textureHeight);
    if (!frameTexture || (std::uint32_t)textureWidth != frameWidth || (std::uint32_t)textureHeight != frameHeight) {
      if (frameTexture) SDL_DestroyTexture(frameTexture);
      // The runner's window pixels are RGBA bytes.
      frameTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, (int)frameWidth, (int)frameHeight);
      if (frameTexture) {
        SDL_SetTextureScaleMode(frameTexture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(frameTexture, SDL_BLENDMODE_NONE);
      }
    }
    if (frameTexture) SDL_UpdateTexture(frameTexture, nullptr, pixels.data(), (int)frameWidth * 4);
  }
  if (game && frameTexture && frameWidth && frameHeight) {
    // The core's display size includes its pixel aspect (a 4:3 TV picture for N64 and PS1).
    auto geometry = ares::getVideoGeometry();
    float aspect = geometry.width > 0 && geometry.height > 0 ? geometry.width / geometry.height : (float)frameWidth / frameHeight;
    float w = (float)outputWidth, h = w / aspect;
    if (h > outputHeight) {
      h = (float)outputHeight;
      w = h * aspect;
    }
    // The PSP's 480x272 comes drawn at the whole multiple of its size nearest the window's, each pixel repeated, and
    // is scaled smoothly the little rest of the way, so its one-pixel lines stay sharp and even ("sharp bilinear", as
    // the Android app draws it); the other systems' pictures are scaled by nearest pixels.
    bool psp = game->system == "PlayStation Portable";
    if (psp && geometry.height > 0) {
      int multiple = std::clamp((int)std::lround(h / geometry.height), 1, 4);
      if (multiple != pictureMultiple) ares::setPictureMultiple(pictureMultiple = multiple);
    }
    SDL_SetTextureScaleMode(frameTexture, psp ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_FRect target{(outputWidth - w) / 2, (outputHeight - h) / 2, w, h};
    SDL_RenderTexture(renderer, frameTexture, nullptr, &target);
  }

  float scale = std::max(1.0f, std::floor(std::min(outputWidth / 640.0f, outputHeight / 360.0f)));
  SDL_SetRenderScale(renderer, scale, scale);
  float width = outputWidth / scale, height = outputHeight / scale;
  if (screen == Screen::Library) drawLibrary(width, height);
  if (screen == Screen::Menu) drawMenu(width, height);
  if (screen == Screen::Playing && !ares::isFirstFrameRendered()) print(12, 12, "Loading " + game->title + "...", textColor);
  if (SDL_GetTicks() < messageUntil) {
    float w = std::min(width - 16, (message.size() + 2) * glyph);
    fill(8, height - 44, w, 16, panelColor);
    print(16, height - 40, message, textColor, w - 16);
  }
  SDL_RenderPresent(renderer);
}

auto Shell::drawLibrary(float width, float height) -> void {
  std::lock_guard<std::mutex> lock(gamesMutex);
  fill(0, 0, width, 22, panelColor);
  print(8, 7, "PHOBOS", textColor);
  print(72, 7, gamesFolder.empty() ? "No games folder" : gamesFolder, dimColor, width - 80);
  fill(0, height - 20, width, 20, panelColor);
  print(8, height - 14, "Enter/A Play   F3/Y Games folder   F5/X Rescan   F11 Fullscreen", dimColor, width - 16);

  float top = 30, rowHeight = 12;
  if (games.empty()) {
    std::vector<std::string> lines = {
      gamesFolder.empty() ? "Choose your games folder: press F3 or Y." : "No games in this folder or the folders inside it.",
      "You can also drop a folder or a game on this window, or start",
      "Phobos with one: phobos <folder or game>.",
      "",
      "Firmware (BIOS) files go in:",
      firmwareFolder,
    };
    if (!vulkanAvailable) {
      lines.push_back("");
      lines.push_back("No Vulkan driver was found, so Nintendo 64 games can't run.");
    }
    for (size_t i = 0; i < lines.size(); i++) print(16, top + 8 + i * rowHeight, lines[i], i < 3 ? textColor : dimColor, width - 32);
    return;
  }
  int visible = std::max(1, (int)((height - top - 52) / rowHeight));
  int count = (int)games.size();
  int first = std::clamp(cursor - visible / 2, 0, std::max(0, count - visible));
  for (int i = first; i < std::min(count, first + visible); i++) {
    float y = top + (i - first) * rowHeight;
    if (i == cursor) fill(4, y - 2, width - 8, rowHeight, highlightColor);
    const auto& entry = games[i];
    float systemWidth = entry.system.size() * glyph;
    // The disc's own title when it's there and the font can draw it, the file's name when it isn't; the states
    // and saves keep their key in the file's name (entry.title).
    print(10, y, entry.discTitle.empty() ? entry.title : entry.discTitle, textColor, width - systemWidth - 36);
    print(width - systemWidth - 10, y, entry.system, i == cursor ? textColor : dimColor);
  }
}

auto Shell::drawMenu(float width, float height) -> void {
  fill(0, 0, width, height, shadeColor);
  auto items = menuItems();
  float boxWidth = std::min(width - 32, 360.0f);
  float boxHeight = 44 + items.size() * 14.0f;
  float x = (width - boxWidth) / 2, y = std::max(8.0f, (height - boxHeight) / 2);
  fill(x, y, boxWidth, boxHeight, panelColor);
  print(x + 12, y + 10, game->title, textColor, boxWidth - 24);
  for (size_t i = 0; i < items.size(); i++) {
    float rowY = y + 28 + i * 14.0f;
    if ((int)i == menuCursor) fill(x + 6, rowY - 3, boxWidth - 12, 14, highlightColor);
    print(x + 14, rowY, items[i].label, textColor, boxWidth - 28);
  }
  print(x + 12, y + boxHeight - 12, "A Select   B Back   Left/Right Change", dimColor, boxWidth - 24);
}

auto Shell::fill(float x, float y, float w, float h, Color color) -> void {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRect(renderer, &rect);
}

auto Shell::print(float x, float y, const std::string& line, Color color, float maxWidth) -> void {
  std::string shown = line;
  if (maxWidth > 0) {
    size_t fits = (size_t)std::max(0.0f, maxWidth / glyph);
    if (shown.size() > fits) shown = fits > 3 ? shown.substr(0, fits - 3) + "..." : shown.substr(0, fits);
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDebugText(renderer, x, y, shown.c_str());
}

auto Shell::show(const std::string& line) -> void {
  message = line;
  messageUntil = SDL_GetTicks() + 4000;
  SDL_Log("%s", line.c_str());
}

}

int main(int argc, char* argv[]) {
  Shell shell;
  return shell.run(argc, argv);
}
