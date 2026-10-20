#include "DesktopHost.hpp"
#include "Firmware.hpp"
#include "Hotkeys.hpp"
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
#include <set>
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
constexpr Color warningColor{232, 170, 72, 255};
// SDL's debug font: 8 x 8 pixel characters, ASCII only.
constexpr float glyph = 8.0f;

enum class Screen { Library, Playing, Menu, Hotkeys };

// Where hotkeys are set (hotkeySettingKey): for every game, for the running game's console, or for the running game.
enum class HotkeyLevel { AllConsoles, Console, Game };

// Frame advance runs a frame at each press, then one every frameStepMs while held, after frameStepDelayMs (the Android
// app's pace).
constexpr Uint64 frameStepDelayMs = 300;
constexpr Uint64 frameStepMs = 100;
// Slow motion's speed while its hotkey is held (as on Android).
constexpr float slowMotionSpeed = 0.5f;
// A binding being captured is done once no new key has come for captureSettleMs (as on Android), and given up when no
// key at all has come for captureTimeoutMs, which leaves a controller alone a way out.
constexpr Uint64 captureSettleMs = 700;
constexpr Uint64 captureTimeoutMs = 5000;

// What a folder picked in the system's dialog is for.
enum class Pick { Games, PspFonts, PspMemoryStick };

// The PSP's drawing-threads choices, as the Android app offers them (PspDrawingThreads): 0 is all cores but one.
constexpr int pspDrawingThreads[] = {0, 1, 2, 4, 6, 8};
// Who draws the PSP's pictures (the core's "Renderer"), as the app's PspRenderer: 0 software, 1 Vulkan (accurate),
// 2 Vulkan (fast).
constexpr const char* pspRenderers[] = {"Software", "Vulkan (accurate)", "Vulkan (fast)"};

struct MenuItem {
  std::string label;
  // 0 for A or Enter, -1 or +1 for left or right.
  std::function<void(int)> choose;
};

// A row of the Hotkeys page: the level shown, an action, one of its bindings, the switch that shows the direct slots,
// or the level's reset.
struct HotkeyRow {
  enum class Kind { Level, Action, Binding, Slots, Reset } kind;
  std::string action;
  int slot = -1;
};

// A binding being captured: the keys pressed, in order, since it started.
struct HotkeyCapture {
  std::string action;
  int slot = 0;
  HotkeyCombo combo;
  Uint64 started = 0;
  Uint64 lastAdded = 0;
};

// A question waiting on the Hotkeys page: Enter or A answers yes, Escape or B no.
struct Question {
  std::string text;
  std::string yes;
  std::string no;
  std::function<void()> confirm;
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

// [text] in lines of at most [columns] characters, broken at its spaces.
static auto wrap(const std::string& text, size_t columns) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::string line;
  for (size_t start = 0; start < text.size();) {
    auto space = text.find(' ', start);
    auto word = text.substr(start, space == std::string::npos ? std::string::npos : space - start);
    if (!line.empty() && line.size() + 1 + word.size() > columns) {
      lines.push_back(line);
      line.clear();
    }
    line += (line.empty() ? "" : " ") + word;
    if (space == std::string::npos) break;
    start = space + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

struct Shell {
  auto run(int argc, char* argv[]) -> int;

private:
  auto setup(int argc, char* argv[]) -> bool;
  auto applySettings() -> void;
  auto rescan() -> void;
  auto startPspTitles() -> void;
  auto stopPspTitles() -> void;
  auto chooseFolder(Pick pick) -> void;
  auto picked(Pick pick, const std::string& folder) -> void;
  auto open(const std::string& path) -> void;
  auto launch(const Game& entry) -> void;
  auto unloadGame() -> void;
  auto quitGame() -> void;
  auto askToQuit() -> void;
  auto answerQuit(bool quitting) -> void;
  auto openMenu() -> void;
  auto closeMenu() -> void;
  auto releaseKeys() -> void;
  auto statePath(int slot) const -> std::string;
  auto saveState(int slot) -> void;
  auto loadState(int slot) -> void;
  auto setStateSlot(int slot) -> void;
  auto takeScreenshot() -> void;
  auto changeDisc(int direction) -> void;
  auto cycleN64Pak(int direction) -> std::string;
  auto togglePs1Analog() -> bool;
  auto setFullscreen(bool fullscreen) -> void;
  auto toggleFullscreen() -> void;
  auto startFrameStep() -> void;
  auto hotkeyScope(HotkeyLevel level) const -> std::string;
  auto storedHotkeys(HotkeyLevel level) const -> HotkeyMap;
  auto hotkeysAt(HotkeyLevel level) const -> HotkeyMap;
  auto hotkeySource(HotkeyLevel level, const std::string& action) const -> HotkeyLevel;
  auto storeHotkeys(HotkeyLevel level, const HotkeyMap& changes) -> void;
  auto refreshHotkeys() -> void;
  auto hotkeysTakeKeys() const -> bool;
  auto hotkeyEvent(const SDL_Event& event) -> void;
  auto runHotkey(const std::string& action) -> void;
  auto updateHolds(const std::vector<std::string>& completed) -> void;
  auto endHolds() -> void;
  auto openHotkeys() -> void;
  auto hotkeyRows() const -> std::vector<HotkeyRow>;
  auto updateHotkeys(bool navUp, bool navDown, bool navLeft, bool navRight, bool confirm, bool back, bool clear)
      -> void;
  auto hotkeysBack() -> void;
  auto clearHotkey(const HotkeyRow& row) -> void;
  auto finishCapture() -> void;
  auto menuItems() -> std::vector<MenuItem>;
  auto pspMenuItems(std::vector<MenuItem>& items) -> void;
  auto setPspFonts(const std::string& picked) -> std::string;
  auto handleKey(const SDL_KeyboardEvent& key) -> void;
  auto update() -> void;
  auto render() -> void;
  auto drawLibrary(float width, float height) -> void;
  auto drawMenu(float width, float height) -> void;
  auto drawHotkeys(float width, float height) -> void;
  auto drawBox(float width, float height, const std::vector<std::string>& lines, const std::string& hint) -> void;
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
  bool fastForwardHeld = false;
  bool fastForwardApplied = false;
  bool slowMotionHeld = false;
  bool slowMotionApplied = false;
  bool muted = false;
  // Frame advance: the game stopped with its picture showing (startFrameStep), and its hotkey held.
  bool frameStepping = false;
  bool frameAdvanceHeld = false;
  Uint64 frameStepNext = 0;
  // The game waiting while another window has the keyboard (Pause when unfocused; update).
  bool focusPaused = false;
  // The Quit hotkey's question, over the stopped game (askToQuit).
  bool quitAsked = false;
  // The hotkeys in force (resolveHotkeys) and the keys held, matched against them, with the screen each was pressed on
  // while the window had the keyboard (a tap runs only there).
  HotkeyMap hotkeys;
  HotkeyTracker hotkeyTracker;
  std::map<HotkeyToken, Screen> pressedOn;
  // The Hotkeys page: the level it shows, its cursor, the action whose bindings are open, whether every direct slot's
  // row shows, the binding being captured, and a question waiting.
  HotkeyLevel hotkeyLevel = HotkeyLevel::AllConsoles;
  int hotkeyCursor = 0;
  std::string hotkeyOpen;
  bool slotHotkeysShown = false;
  std::optional<HotkeyCapture> capture;
  std::optional<Question> question;
  // Keys of the ZX Spectrum or MSX keyboard held, with how many host keys hold each, and the host keys whose press
  // reached it (a key a hotkey took didn't).
  std::map<std::string, int> heldKeys;
  std::set<int> typedKeys;
  int previousButtons = 0;
  // Held buttons kept out until they're let go of (update says which).
  int suppressedButtons = 0;
  // Whether the hotkeys took keys at the last update (hotkeysTakeKeys).
  bool hotkeysTook = false;
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
      // The hotkeys first, so the keys they take are held back from what follows. A key whose hotkey changed the
      // screen, or asked whether to quit, is the hotkey's alone: the new screen, or the question, doesn't act on it.
      Screen before = screen;
      bool askedBefore = quitAsked;
      hotkeyEvent(event);
      input.handle(event);
      switch (event.type) {
      case SDL_EVENT_QUIT: quit = true; break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: if (screen == before && quitAsked == askedBefore) handleKey(event.key); break;
      case SDL_EVENT_DROP_FILE: if (event.drop.data) open(event.drop.data); break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        // A binding still being captured (its keys still coming) takes none of the keys that switched windows (Alt for
        // Alt+Tab, say); a key held longer than the capture's settling time is in the binding already.
        if (capture) {
          capture.reset();
          show("The window lost focus: the binding is unchanged");
        }
        break;
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
  stateSlot = std::clamp(settings.number("stateSlot", 1), 1, 9);
  refreshHotkeys();
  input.taken = [this](HotkeyToken token) { return hotkeysTakeKeys() && hotkeyTracker.taken(token); };

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
  {
    std::lock_guard<std::mutex> lock(gamesMutex);
    games = scanLibrary(gamesFolder);
    cursor = std::clamp(cursor, 0, std::max(0, (int)games.size() - 1));
    ++scanGeneration;
  }
  startPspTitles();
}

//The PSP's titles, filled in after the list shows (a CHD's open takes a while, so the list isn't held for it),
//on a thread of their own. The old thread, if one's running, is joined (so it can't outlive the Shell) before
//the new one starts; its work is superseded by the rescan's generation. No titles are filled in while a game
//runs: the thread stops when a game starts and restarts when the Library shows.
auto Shell::startPspTitles() -> void {
  if (pspTitlesThread.joinable()) pspTitlesThread.join();
  std::vector<std::string> files;
  {
    std::lock_guard<std::mutex> lock(gamesMutex);
    for (auto& game : games)
      if (game.system == "PlayStation Portable" && pspDiscImage(game.discs.front()))
        files.push_back(game.discs.front());
  }
  pspTitlesThread = std::thread([this, files, generation = scanGeneration.load()] {
    auto cache = PspIconCache(dataFolder + "psp-icons");
    for (auto& file : files) {
      //The scan gives up when it's superseded (a rescan) or Phobos is quitting: the disc's reader returns 0,
      //so its result isn't read to the end or cached.
      auto title = listTitle(pspDiscTitle(file, cache, quitting, generation, scanGeneration,
                                          [](bool q, int g, std::atomic<int>& sg) -> bool {
                                            return q || g != sg.load();
                                          }));
      std::lock_guard<std::mutex> lock(gamesMutex);
      if (generation != scanGeneration.load()) break;
      for (auto& game : games)
        if (game.discs.front() == file) game.discTitle = title;
    }
  });
}

//The titles' thread, stopped while a game runs (joined, so it can't outlive the Shell).
auto Shell::stopPspTitles() -> void {
  if (pspTitlesThread.joinable()) pspTitlesThread.join();
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
  stopPspTitles();
  frameSerial = 0;
  frameWidth = frameHeight = 0;
  pictureMultiple = 0;
  fastForward = fastForwardHeld = fastForwardApplied = false;
  ares::setFastForward(false);
  slowMotionHeld = slowMotionApplied = false;
  ares::setSlowMotion(1.0f);
  frameStepping = frameAdvanceHeld = false;
  // Started running (setPause above); update pauses it again if another window has the keyboard.
  focusPaused = quitAsked = false;
  muted = false;
  ares::setMuteAudio(false);
  suppressedButtons = previousButtons;
  // A computer's keyboard keeps the keys it uses from the default hotkeys.
  refreshHotkeys();
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
  frameStepping = focusPaused = quitAsked = false;
  endHolds();
  capture.reset();
  question.reset();
  refreshHotkeys();
  startPspTitles();
  SDL_SetWindowTitle(window, "Phobos");
}

auto Shell::quitGame() -> void {
  unloadGame();
  if (launchedWithGame) quit = true;
}

// The Quit hotkey asks first, as on Android, the game stopped under the question.
auto Shell::askToQuit() -> void {
  quitAsked = true;
  frameStepping = focusPaused = false;
  endHolds();
  releaseKeys();
  ares::setInput(0, 0, 0, 0, 0);
  input.rumble(false);
  ares::setPause(true);
}

// The question answered: quit, or go back to the game. Every button held then stays out until it's let go of, from the
// game and from the library alike.
auto Shell::answerQuit(bool quitting) -> void {
  quitAsked = false;
  suppressedButtons = ~0;
  if (quitting) return quitGame();
  ares::setPause(false);
}

auto Shell::openMenu() -> void {
  screen = Screen::Menu;
  menuCursor = 0;
  endHolds();
  releaseKeys();
  ares::setInput(0, 0, 0, 0, 0);
  ares::setPause(true);
}

auto Shell::closeMenu() -> void {
  screen = Screen::Playing;
  suppressedButtons = previousButtons;
  frameStepping = false;
  ares::setPause(false);
}

auto Shell::releaseKeys() -> void {
  for (auto& [label, count] : heldKeys) {
    if (count > 0) ares::setKeyboardKey(label.c_str(), false);
  }
  heldKeys.clear();
  typedKeys.clear();
}

auto Shell::statePath(int slot) const -> std::string {
  auto folder = dataFolder + "states/" + game->system;
  std::error_code error;
  fs::create_directories(toPath(folder), error);
  return folder + "/" + game->title + " (" + std::to_string(slot) + ").state";
}

auto Shell::saveState(int slot) -> void {
  if (!game) return;
  bool saved = ares::saveState(statePath(slot).c_str());
  show(saved ? "Saved state " + std::to_string(slot) : "Couldn't save state " + std::to_string(slot));
}

auto Shell::loadState(int slot) -> void {
  if (!game) return;
  bool loaded = ares::loadState(statePath(slot).c_str());
  show(loaded ? "Loaded state " + std::to_string(slot) : "No state " + std::to_string(slot) + " to load");
}

// The slot Save state and Load state use, kept for the next start.
auto Shell::setStateSlot(int slot) -> void {
  stateSlot = slot;
  settings.setNumber("stateSlot", slot);
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

// The N64's controller pak, the next or the previous of the three; returns it.
auto Shell::cycleN64Pak(int direction) -> std::string {
  static const std::vector<std::string> paks = {"None", "Rumble Pak", "Controller Pak"};
  auto it = std::find(paks.begin(), paks.end(), settings.text("n64.pak", "None"));
  int index = it == paks.end() ? 0 : (int)(it - paks.begin());
  auto pak = paks[(index + (direction < 0 ? 2 : 1)) % 3];
  settings.setText("n64.pak", pak);
  ares::setN64Pak(pak.c_str());
  return pak;
}

// The PlayStation's analog controller switched on or off; returns whether it's on.
auto Shell::togglePs1Analog() -> bool {
  bool on = ares::togglePs1AnalogMode();
  settings.setFlag("ps1.analog", on);
  return on;
}

auto Shell::setFullscreen(bool fullscreen) -> void {
  SDL_SetWindowFullscreen(window, fullscreen);
  settings.setFlag("fullscreen", fullscreen);
}

auto Shell::toggleFullscreen() -> void { setFullscreen(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)); }

// Frame advance: the game stops where it is, with no menu over its picture, and runs a frame at each press of the
// hotkey (updateHolds; update repeats them while it's held), the buttons held going into each. The menu lets it go on.
auto Shell::startFrameStep() -> void {
  if (frameStepping) return;
  frameStepping = true;
  ares::setPause(true);
}

// A level's scope (hotkeySettingKey): none for all consoles, the running game's system for its console, and its system
// and title, the pair its states go by, for the game.
auto Shell::hotkeyScope(HotkeyLevel level) const -> std::string {
  if (!game || level == HotkeyLevel::AllConsoles) return "";
  return level == HotkeyLevel::Console ? game->system : game->system + "/" + game->title;
}

// Each action's bindings the player stored in settings.ini at [level]: an empty one unbinds the action there.
auto Shell::storedHotkeys(HotkeyLevel level) const -> HotkeyMap {
  HotkeyMap stored;
  auto scope = hotkeyScope(level);
  for (auto& action : hotkeyActions()) {
    auto key = hotkeySettingKey(scope, action.name);
    if (settings.has(key)) stored[action.name] = parseHotkeyBindings(settings.text(key));
  }
  return stored;
}

// What [level] and the levels above it store, the narrowest deciding each action (shownHotkeys adds the defaults).
auto Shell::hotkeysAt(HotkeyLevel level) const -> HotkeyMap {
  auto stored = storedHotkeys(HotkeyLevel::AllConsoles);
  if (level != HotkeyLevel::AllConsoles) stored = overHotkeys(stored, storedHotkeys(HotkeyLevel::Console));
  if (level == HotkeyLevel::Game) stored = overHotkeys(stored, storedHotkeys(HotkeyLevel::Game));
  return stored;
}

// The level deciding [action] at [level]: the narrowest one storing it, else all consoles (with the defaults).
auto Shell::hotkeySource(HotkeyLevel level, const std::string& action) const -> HotkeyLevel {
  for (int at = (int)level; at > (int)HotkeyLevel::AllConsoles; at--) {
    if (settings.has(hotkeySettingKey(hotkeyScope((HotkeyLevel)at), action))) return (HotkeyLevel)at;
  }
  return HotkeyLevel::AllConsoles;
}

// All consoles keep what they're given; a console or a game keeps only what differs from what it inherits, an action
// that comes out as inherited going back to inheriting (levelHotkeyChanges).
auto Shell::storeHotkeys(HotkeyLevel level, const HotkeyMap& changes) -> void {
  auto scope = hotkeyScope(level);
  if (level == HotkeyLevel::AllConsoles) {
    for (auto& [action, bindings] : changes) {
      settings.setText(hotkeySettingKey(scope, action), encodeHotkeyBindings(bindings));
    }
  } else {
    auto inherited = shownHotkeys(hotkeysAt((HotkeyLevel)((int)level - 1)));
    std::vector<std::string> inheriting;
    for (auto& [action, bindings] : levelHotkeyChanges(changes, inherited)) {
      auto key = hotkeySettingKey(scope, action);
      if (bindings) settings.setText(key, encodeHotkeyBindings(*bindings));
      else if (settings.has(key)) inheriting.push_back(key);
    }
    if (!inheriting.empty()) settings.erase(inheriting);
  }
  refreshHotkeys();
}

// The hotkeys in force: the running game's over its console's over all consoles' (the library has all consoles'); on a
// computer, the default ones on keys its keyboard uses (and Tab, as before) are left to it.
auto Shell::refreshHotkeys() -> void {
  std::function<bool(HotkeyToken)> gameKey;
  if (game && isKeyboardComputer(game->system)) {
    gameKey = [system = game->system](HotkeyToken token) {
      if (tokenKind(token) != HotkeyKind::Key) return false;
      auto key = (SDL_Scancode)tokenCode(token);
      return key == SDL_SCANCODE_TAB || !keyboardLabels(system, key).empty();
    };
  }
  hotkeys = resolveHotkeys(hotkeysAt(HotkeyLevel::Game), gameKey);
}

// Only a game and its menu lose keys to the hotkeys: the library's, the Hotkeys page's and the Quit question's are
// their own.
auto Shell::hotkeysTakeKeys() const -> bool { return game && screen != Screen::Hotkeys && !quitAsked; }

// An event's keys go to the hotkeys (and to a binding being captured): a press can complete a combo, a release a tap,
// and the hold actions last while theirs are held. The Hotkeys page and the Quit question run none: their keys are
// their own. Nothing runs while another window has the keyboard: a press comes then only as a click on the window (the
// keys go to that window, and SDL holds back the pads' presses), releases still come (SDL's own as the window loses the
// keyboard included), and a tap runs only on the screen its key was pressed on with the window having it.
auto Shell::hotkeyEvent(const SDL_Event& event) -> void {
  auto changes = input.hotkeyChanges(event);
  if (changes.empty()) return;
  bool focused = SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
  bool tapHere = focused;
  for (auto [token, down] : changes) {
    if (down) {
      hotkeyTracker.press(hotkeys, token);
      // A key pressed with the Quit question up is the question's: let go of after it's answered, it taps nothing.
      if (focused && !quitAsked) pressedOn[token] = screen;
      else pressedOn.erase(token);
    } else {
      hotkeyTracker.release(token);
      auto it = pressedOn.find(token);
      tapHere = tapHere && it != pressedOn.end() && it->second == screen;
      if (it != pressedOn.end()) pressedOn.erase(it);
    }
    // Escape first cancels the capture (handleKey) rather than starting the combo.
    if (!capture || !down || (capture->combo.empty() && token == keyToken(SDL_SCANCODE_ESCAPE))) continue;
    if (std::find(capture->combo.begin(), capture->combo.end(), token) != capture->combo.end()) continue;
    capture->combo.push_back(token);
    capture->lastAdded = SDL_GetTicks();
  }
  auto completed = hotkeyTracker.completions(hotkeys);
  auto tapped = hotkeyTracker.taps(hotkeys);
  // A pad going away lets go of its buttons, which taps nothing.
  if (event.type == SDL_EVENT_GAMEPAD_REMOVED || !tapHere) tapped.clear();
  if (!focused) completed.clear();
  if (screen == Screen::Hotkeys || quitAsked) return;
  // Quit runs first, and once it has asked, the question has the keys: nothing else the event brought runs (an action
  // bound to the same keys, a collision the page shows: Load state, say, would throw progress away before it asked).
  auto quitFirst = [](std::vector<std::string>& actions) {
    auto quit = std::find(actions.begin(), actions.end(), Hotkey::quit);
    if (quit != actions.end()) std::rotate(actions.begin(), quit, quit + 1);
  };
  quitFirst(completed);
  quitFirst(tapped);
  for (auto& action : completed) {
    if (!isHotkeyHold(action)) runHotkey(action);
    if (quitAsked) return;
  }
  for (auto& action : tapped) {
    if (!isHotkeyHold(action)) runHotkey(action);
    if (quitAsked) return;
  }
  updateHolds(completed);
}

// A hotkey's action. Fullscreen works anywhere; the Pause menu hotkey opens or closes the menu; the rest wait for the
// game to be playing (the menu has them too).
auto Shell::runHotkey(const std::string& action) -> void {
  if (action == Hotkey::fullscreen) return toggleFullscreen();
  if (!game) return;
  if (action == Hotkey::pause) return screen == Screen::Menu ? closeMenu() : openMenu();
  if (action == Hotkey::pauseWindow) {
    setFullscreen(false);
    if (screen == Screen::Playing) openMenu();
    return;
  }
  if (screen != Screen::Playing) return;
  if (int slot = directSlot(action)) {
    return action.compare(0, std::string(Hotkey::saveSlotPrefix).size(), Hotkey::saveSlotPrefix) == 0 ? saveState(slot)
                                                                                                        : loadState(slot);
  }
  if (action == Hotkey::save) return saveState(stateSlot);
  if (action == Hotkey::load) return loadState(stateSlot);
  if (action == Hotkey::nextSlot || action == Hotkey::previousSlot) {
    setStateSlot(action == Hotkey::nextSlot ? stateSlot % 9 + 1 : (stateSlot + 7) % 9 + 1);
    return show("State slot " + std::to_string(stateSlot));
  }
  if (action == Hotkey::screenshot) return takeScreenshot();
  if (action == Hotkey::fastForwardToggle) {
    fastForward = !fastForward;
    return show(fastForward ? "Fast forward on" : "Fast forward off");
  }
  if (action == Hotkey::mute) {
    muted = !muted;
    ares::setMuteAudio(muted);
    return show(muted ? "Muted" : "Sound on");
  }
  if (action == Hotkey::reset) {
    ares::resetSystem();
    return show("Reset");
  }
  if (action == Hotkey::reload) {
    auto entry = *game;
    return launch(entry);
  }
  if (action == Hotkey::quit) return askToQuit();
  if (action == Hotkey::n64Pak && game->system == "Nintendo 64") return show("Controller pak: " + cycleN64Pak(1));
  if (action == Hotkey::disc && game->discs.size() > 1) return changeDisc(1);
  if (action == Hotkey::ps1Analog && game->system == "PlayStation") {
    return show(togglePs1Analog() ? "Analog controller on" : "Analog controller off");
  }
}

// A hold action starts as a press completes one of its combos and lasts while every key of one of them stays held,
// others or not; it ends as the game stops playing. Each press of Frame Advance runs its frame at once, so a quick tap
// still gets one.
auto Shell::updateHolds(const std::vector<std::string>& completed) -> void {
  auto holds = [&](const char* action, bool held) {
    if (!game || screen != Screen::Playing) return false;
    if (!held) return std::find(completed.begin(), completed.end(), action) != completed.end();
    auto it = hotkeys.find(action);
    return it != hotkeys.end() && std::any_of(it->second.begin(), it->second.end(), [&](const HotkeyCombo& combo) {
      return hotkeyTracker.holding(combo);
    });
  };
  fastForwardHeld = holds(Hotkey::fastForwardHold, fastForwardHeld);
  slowMotionHeld = holds(Hotkey::slowMotion, slowMotionHeld);
  bool frameAdvance = holds(Hotkey::frameAdvance, frameAdvanceHeld);
  if (frameAdvance && !frameAdvanceHeld) {
    startFrameStep();
    ares::frameAdvance();
    frameStepNext = SDL_GetTicks() + frameStepDelayMs;
  }
  frameAdvanceHeld = frameAdvance;
}

auto Shell::endHolds() -> void { fastForwardHeld = slowMotionHeld = frameAdvanceHeld = false; }

auto Shell::openHotkeys() -> void {
  screen = Screen::Hotkeys;
  // On all consoles' first hotkey, the level's row just above it.
  hotkeyLevel = HotkeyLevel::AllConsoles;
  hotkeyCursor = 1;
  hotkeyOpen.clear();
  capture.reset();
  question.reset();
}

auto Shell::hotkeyRows() const -> std::vector<HotkeyRow> {
  auto shown = shownHotkeys(hotkeysAt(hotkeyLevel));
  std::vector<HotkeyRow> rows{{HotkeyRow::Kind::Level, "", -1}};
  bool slotsSwitch = false;
  for (auto& action : hotkeyActions()) {
    // The direct slots come after their switch; hidden, those bound still show.
    if (directSlot(action.name)) {
      if (!slotsSwitch) rows.push_back({HotkeyRow::Kind::Slots, "", -1});
      slotsSwitch = true;
      if (!slotHotkeysShown && !shown.count(action.name)) continue;
    }
    rows.push_back({HotkeyRow::Kind::Action, action.name, -1});
    if (hotkeyOpen != action.name) continue;
    for (int slot = 0; slot < hotkeyBindingLimit; slot++) rows.push_back({HotkeyRow::Kind::Binding, action.name, slot});
  }
  rows.push_back({HotkeyRow::Kind::Reset, "", -1});
  return rows;
}

// The Hotkeys page: up and down choose a row; on the level's row, left, right or A change the level; A opens an
// action's bindings, captures one, shows the direct slots or resets the level; X clears; B closes the bindings or goes
// back to the menu.
auto Shell::updateHotkeys(bool navUp, bool navDown, bool navLeft, bool navRight, bool confirm, bool back, bool clear)
    -> void {
  Uint64 now = SDL_GetTicks();
  if (capture) {
    if (!capture->combo.empty() && now - capture->lastAdded >= captureSettleMs) return finishCapture();
    if (capture->combo.empty() && now - capture->started >= captureTimeoutMs) {
      capture.reset();
      show("Nothing pressed: the binding is unchanged");
    }
    return;
  }
  if (question) {
    if (confirm) {
      auto answer = question->confirm;
      question.reset();
      answer();
    } else if (back) {
      question.reset();
    }
    return;
  }
  auto rows = hotkeyRows();
  int count = (int)rows.size();
  if (navUp) hotkeyCursor = (hotkeyCursor + count - 1) % count;
  if (navDown) hotkeyCursor = (hotkeyCursor + 1) % count;
  hotkeyCursor = std::clamp(hotkeyCursor, 0, count - 1);
  auto& row = rows[hotkeyCursor];
  if (back) return hotkeysBack();
  if (clear) return clearHotkey(row);
  if (row.kind == HotkeyRow::Kind::Level && (navLeft || navRight || confirm)) {
    hotkeyLevel = (HotkeyLevel)(((int)hotkeyLevel + (navLeft ? 2 : 1)) % 3);
    return;
  }
  if (!confirm) return;
  switch (row.kind) {
  case HotkeyRow::Kind::Level:
    return;
  case HotkeyRow::Kind::Action:
    hotkeyOpen = hotkeyOpen == row.action ? "" : row.action;
    return;
  case HotkeyRow::Kind::Binding:
    capture = HotkeyCapture{row.action, row.slot, {}, now, now};
    return;
  case HotkeyRow::Kind::Slots:
    slotHotkeysShown = !slotHotkeysShown;
    return;
  case HotkeyRow::Kind::Reset: {
    // All consoles go back to the defaults, what a console or a game sets staying; a console or a game, to inheriting.
    auto scope = hotkeyScope(hotkeyLevel);
    std::string text, done;
    if (hotkeyLevel == HotkeyLevel::AllConsoles) {
      text = "Reset all consoles' hotkeys to the defaults? What a console or a game sets for itself stays.";
      done = "All consoles have the default hotkeys again";
    } else if (hotkeyLevel == HotkeyLevel::Console) {
      text = "Reset " + game->system + "'s hotkeys? Its games then have all consoles', but for what a game sets.";
      done = game->system + " has all consoles' hotkeys again";
    } else {
      text = "Reset this game's hotkeys? It then has " + game->system + "'s.";
      done = "This game has " + game->system + "'s hotkeys again";
    }
    question = Question{text, "Reset", "Cancel", [this, scope, done] {
      std::vector<std::string> keys;
      for (auto& action : hotkeyActions()) keys.push_back(hotkeySettingKey(scope, action.name));
      settings.erase(keys);
      refreshHotkeys();
      show(done);
    }};
    return;
  }
  }
}

// B or Escape: an open action's bindings close, else the page goes back to the menu.
auto Shell::hotkeysBack() -> void {
  if (hotkeyOpen.empty()) {
    screen = Screen::Menu;
    return;
  }
  auto rows = hotkeyRows();
  for (int index = 0; index < (int)rows.size(); index++) {
    if (rows[index].kind == HotkeyRow::Kind::Action && rows[index].action == hotkeyOpen) hotkeyCursor = index;
  }
  hotkeyOpen.clear();
}

// X or Delete: a binding row's binding is cleared, an action row's every binding; but for a console or a game, an
// action it sets goes back to what it inherits, and only one it inherits is unbound for it.
auto Shell::clearHotkey(const HotkeyRow& row) -> void {
  if (row.kind == HotkeyRow::Kind::Action) {
    auto key = hotkeySettingKey(hotkeyScope(hotkeyLevel), row.action);
    if (hotkeyLevel != HotkeyLevel::AllConsoles && settings.has(key)) {
      settings.erase({key});
      return refreshHotkeys();
    }
    return storeHotkeys(hotkeyLevel, {{row.action, {}}});
  }
  if (row.kind == HotkeyRow::Kind::Binding) {
    storeHotkeys(hotkeyLevel, bindingChanges(shownHotkeys(hotkeysAt(hotkeyLevel)), row.action, row.slot, {}, {}));
  }
}

// The keys captured become the binding at the level shown, or, where they collide with other actions' bindings there,
// the question whether to overwrite those (hotkeyConflictMessage). The keys still held stay away from the page until
// they're let go of.
auto Shell::finishCapture() -> void {
  auto done = *capture;
  capture.reset();
  suppressedButtons = previousButtons;
  auto level = hotkeyLevel;
  auto conflicts = hotkeyConflicts(shownHotkeys(hotkeysAt(level)), done.action, done.combo);
  std::vector<std::pair<std::string, int>> overwrite;
  for (auto& conflict : conflicts) overwrite.emplace_back(conflict.action, conflict.slot);
  auto bind = [this, level, done, overwrite] {
    storeHotkeys(level, bindingChanges(shownHotkeys(hotkeysAt(level)), done.action, done.slot, done.combo, overwrite));
  };
  if (conflicts.empty()) return bind();
  question = Question{hotkeyConflictMessage(conflicts), "Overwrite", "Cancel", bind};
}

auto Shell::menuItems() -> std::vector<MenuItem> {
  auto onOff = [](bool on) { return std::string(on ? "on" : "off"); };
  std::vector<MenuItem> items;
  items.push_back({"Resume", [this](int d) { if (d == 0) closeMenu(); }});
  items.push_back({"Save state", [this](int d) { if (d == 0) saveState(stateSlot); }});
  items.push_back({"Load state", [this](int d) { if (d == 0) loadState(stateSlot); }});
  items.push_back({"State slot: " + std::to_string(stateSlot), [this](int d) { setStateSlot((stateSlot - 1 + (d < 0 ? 8 : 1)) % 9 + 1); }});
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
    items.push_back({"Controller pak: " + settings.text("n64.pak", "None"), [this](int d) { cycleN64Pak(d); }});
  }
  if (game->system == "PlayStation") {
    items.push_back({"Analog controller: " + onOff(settings.flag("ps1.analog", true)), [this](int) { togglePs1Analog(); }});
  }
  if (game->system == "PlayStation Portable") pspMenuItems(items);
  items.push_back({"Hotkeys", [this](int d) { if (d == 0) openHotkeys(); }});
  bool pauseUnfocused = settings.flag("pauseWhenUnfocused", true);
  items.push_back({"Pause when unfocused: " + onOff(pauseUnfocused),
                   [this, pauseUnfocused](int) { settings.setFlag("pauseWhenUnfocused", !pauseUnfocused); }});
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
  int renderer = settings.number("psp.renderer", 0);
  if(renderer < 0 || renderer >= (int)std::size(pspRenderers)) renderer = 0;  //(what the core is told: Software)
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

// The keys the hotkeys don't have: the library's, the menu's and the Hotkeys page's, and an emulated computer keyboard's.
auto Shell::handleKey(const SDL_KeyboardEvent& key) -> void {
  // The Quit question has the keys: Enter quits, Escape or Backspace goes back to the game.
  if (quitAsked) {
    if (!key.down || key.repeat) return;
    if (key.scancode == SDL_SCANCODE_RETURN || key.scancode == SDL_SCANCODE_KP_ENTER) answerQuit(true);
    if (key.scancode == SDL_SCANCODE_ESCAPE || key.scancode == SDL_SCANCODE_BACKSPACE) answerQuit(false);
    return;
  }
  bool computer = game && isKeyboardComputer(game->system);
  if (key.down && !key.repeat) {
    switch (screen) {
    case Screen::Library:
      if (key.scancode == SDL_SCANCODE_F3) chooseFolder(Pick::Games);
      if (key.scancode == SDL_SCANCODE_F5) rescan();
      return;
    case Screen::Menu:
      // Unless a hotkey took it: then the key does only what its hotkey does (a tap's runs as the key is let go of).
      if ((key.scancode == SDL_SCANCODE_ESCAPE || key.scancode == SDL_SCANCODE_BACKSPACE) &&
          !hotkeyTracker.taken(keyToken(key.scancode))) {
        closeMenu();
      }
      return;
    case Screen::Hotkeys:
      if (capture) {
        if (key.scancode == SDL_SCANCODE_ESCAPE && capture->combo.empty()) capture.reset();
        return;
      }
      if (key.scancode == SDL_SCANCODE_ESCAPE) {
        if (question) question.reset();
        else hotkeysBack();
      } else if ((key.scancode == SDL_SCANCODE_DELETE || key.scancode == SDL_SCANCODE_BACKSPACE) && !question) {
        auto rows = hotkeyRows();
        if (hotkeyCursor < (int)rows.size()) clearHotkey(rows[hotkeyCursor]);
      }
      return;
    case Screen::Playing:
      break;
    }
  }
  if (screen != Screen::Playing || !computer || key.repeat) return;
  // A key a hotkey took doesn't type; a release lets go of only what its press typed.
  if (key.down ? hotkeyTracker.taken(keyToken(key.scancode)) || !typedKeys.insert(key.scancode).second
               : !typedKeys.erase(key.scancode)) {
    return;
  }
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
  // Buttons held as the menu closed, a game started or a binding was captured stay out until they're let go of; so does
  // every button held as the hotkeys stop taking keys (the game unloaded, the Hotkeys page opened), those they took
  // included, rather than reaching the screen as a new press.
  bool hotkeysTake = hotkeysTakeKeys();
  if (hotkeysTook && !hotkeysTake) suppressedButtons = ~0;
  hotkeysTook = hotkeysTake;
  suppressedButtons &= pad.buttons;
  int buttons = pad.buttons & ~suppressedButtons;
  // Pressed: held, and not in the frame before (kept: previousButtons becomes this frame's before the screens ask).
  int before = previousButtons;
  auto pressed = [&](int bit) { return (buttons & bit) && !(before & bit); };
  Uint64 now = SDL_GetTicks();
  bool menuNavigation = screen != Screen::Playing && !capture;
  bool navUp = up.step(menuNavigation && ((buttons & PadUp) || pad.ly < -0.5f), now);
  bool navDown = down.step(menuNavigation && ((buttons & PadDown) || pad.ly > 0.5f), now);
  bool navLeft = left.step(menuNavigation && ((buttons & PadLeft) || pad.lx < -0.5f), now);
  bool navRight = right.step(menuNavigation && ((buttons & PadRight) || pad.lx > 0.5f), now);
  bool confirm = pressed(PadA) || pressed(PadStart);
  previousButtons = pad.buttons;

  // Pause when unfocused: while another window has the keyboard the game waits, going on as this window gets it back
  // (the Quit question keeps it stopped either way).
  bool focused = SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
  bool unfocusedPause = game && screen == Screen::Playing && !frameStepping && !quitAsked && !focused &&
                        settings.flag("pauseWhenUnfocused", true);
  if (unfocusedPause != focusPaused) {
    focusPaused = unfocusedPause;
    if (focusPaused) {
      endHolds();
      releaseKeys();
      ares::setInput(0, 0, 0, 0, 0);
      input.rumble(false);
      ares::setPause(true);
    } else if (screen == Screen::Playing && !frameStepping) {
      // The buttons held now stay out until they're let go of, this frame's included.
      suppressedButtons = previousButtons;
      buttons &= ~suppressedButtons;
      ares::setPause(false);
    }
  }

  // The Quit question, over the stopped game: A quits (not Start, which a player may press meaning to go on), B goes
  // back to the game.
  if (quitAsked) {
    if (pressed(PadA)) answerQuit(true);
    else if (pressed(PadB)) answerQuit(false);
    return;
  }

  switch (screen) {
  case Screen::Playing: {
    if (focusPaused) return;
    // Select and Start together, and no other button, open the menu: the route a Steam Deck has, its Guide button being
    // Steam's. With another button down they're the game's (A, B, Select and Start reset many games).
    bool chord = buttons == (PadSelect | PadStart) && (pressed(PadSelect) || pressed(PadStart));
    if (chord) return openMenu();
    ares::setInput(pad.lx, pad.ly, pad.rx, pad.ry, buttons & ~PadHome);
    bool fast = fastForward || fastForwardHeld;
    if (fast != fastForwardApplied) {
      ares::setFastForward(fast);
      fastForwardApplied = fast;
    }
    // Fast forward wins over slow motion while both are on (the runner's pacing).
    if (slowMotionHeld != slowMotionApplied) {
      ares::setSlowMotion(slowMotionHeld ? slowMotionSpeed : 1.0f);
      slowMotionApplied = slowMotionHeld;
    }
    if (frameStepping && frameAdvanceHeld && now >= frameStepNext) {
      ares::frameAdvance();
      frameStepNext = now + frameStepMs;
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
    if (pressed(PadB)) return closeMenu();
    if (confirm) return items[menuCursor].choose(0);
    if (navLeft) return items[menuCursor].choose(-1);
    if (navRight) return items[menuCursor].choose(+1);
    return;
  }
  case Screen::Hotkeys:
    return updateHotkeys(navUp, navDown, navLeft, navRight, confirm, pressed(PadB), pressed(PadX));
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
  if (screen == Screen::Hotkeys) drawHotkeys(width, height);
  bool badged = screen == Screen::Playing && (frameStepping || focusPaused);
  if (screen == Screen::Playing && !badged && !ares::isFirstFrameRendered()) {
    print(12, 12, "Loading " + game->title + "...", textColor);
  }
  if (badged) {
    std::string badge = frameStepping ? "Frame advance - pause for the menu" : "Paused while unfocused";
    fill(8, 8, (badge.size() + 2) * glyph, 16, panelColor);
    print(16, 12, badge, textColor);
  }
  if (quitAsked) {
    drawBox(width, height, wrap("Are you sure you want to stop emulating " + game->title + "?", 52),
            "Enter/A Quit   Esc/B Cancel");
  }
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

auto Shell::drawHotkeys(float width, float height) -> void {
  fill(0, 0, width, height, shadeColor);
  float x = 8, y = 8, w = width - 16, h = height - 16;
  fill(x, y, w, h, panelColor);
  // The levels as the page names them: all consoles, the running game's system, this game.
  auto levelName = [&](HotkeyLevel level) -> std::string {
    if (level == HotkeyLevel::AllConsoles) return "All consoles";
    return level == HotkeyLevel::Console ? game->system : "This game";
  };
  auto inPhrase = [&](HotkeyLevel level) -> std::string {
    if (level == HotkeyLevel::AllConsoles) return "all consoles";
    return level == HotkeyLevel::Console ? game->system : "this game";
  };
  bool scoped = hotkeyLevel != HotkeyLevel::AllConsoles;
  auto title = "Hotkeys - " + levelName(hotkeyLevel);
  if (hotkeyLevel == HotkeyLevel::Game) title += " (" + game->title + ")";
  print(x + 8, y + 8, title, textColor, w - 16);
  float top = y + 24;
  auto line = [&](const std::string& text, Color color) {
    print(x + 8, top, text, color, w - 16);
    top += 12;
  };
  line("Up to 3 bindings each. A binding's first key starts it and is kept from", dimColor);
  line("the game; a key that starts others acts alone when tapped.", dimColor);
  auto shown = shownHotkeys(hotkeysAt(hotkeyLevel));
  std::string collisions;
  for (auto& [first, second] : existingHotkeyConflicts(shown)) {
    collisions += (collisions.empty() ? "These collide: " : "; ") + hotkeyLabel(first) + " and " + hotkeyLabel(second);
  }
  if (!collisions.empty()) line(collisions, warningColor);
  if (game && isKeyboardComputer(game->system)) line("Default keys this computer's keyboard uses, and Tab, stay its own.", dimColor);
  top += 4;

  auto rows = hotkeyRows();
  auto bindingsOf = [&](const std::string& action) {
    auto it = shown.find(action);
    return it == shown.end() ? HotkeyBindings{} : it->second;
  };
  float rowHeight = 12, bottom = y + h - 32, valueX = x + 8 + 30 * glyph;
  int visible = std::max(1, (int)((bottom - top) / rowHeight));
  int count = (int)rows.size();
  int first = std::clamp(hotkeyCursor - visible / 2, 0, std::max(0, count - visible));
  for (int i = first; i < std::min(count, first + visible); i++) {
    auto& row = rows[i];
    float rowY = top + (i - first) * rowHeight;
    if (i == hotkeyCursor && !capture && !question) fill(x + 4, rowY - 2, w - 8, rowHeight, highlightColor);
    std::string label, value;
    Color valueColor = textColor;
    float indent = 0;
    // For a console or a game, the actions it sets itself are bright, those it inherits dim.
    bool inherited = scoped && !row.action.empty() && hotkeySource(hotkeyLevel, row.action) != hotkeyLevel;
    switch (row.kind) {
    case HotkeyRow::Kind::Level: {
      label = "Level";
      // The three side by side, the one shown bright.
      float optionX = valueX;
      for (auto level : {HotkeyLevel::AllConsoles, HotkeyLevel::Console, HotkeyLevel::Game}) {
        auto option = levelName(level);
        float room = x + w - optionX - 8;
        if (room >= glyph) print(optionX, rowY, option, level == hotkeyLevel ? textColor : dimColor, room);
        optionX += (option.size() + 2) * glyph;
      }
      break;
    }
    case HotkeyRow::Kind::Action:
      label = hotkeyLabel(row.action);
      for (auto& combo : bindingsOf(row.action)) {
        if (!combo.empty()) value += (value.empty() ? "" : " or ") + hotkeyComboLabel(combo);
      }
      if (value.empty()) value = "Not bound";
      if (scoped ? inherited : value == "Not bound") valueColor = dimColor;
      break;
    case HotkeyRow::Kind::Binding: {
      indent = 16;
      label = "Binding " + std::to_string(row.slot + 1);
      auto bindings = bindingsOf(row.action);
      if (row.slot < (int)bindings.size()) value = hotkeyComboLabel(bindings[row.slot]);
      if (value.empty()) value = "-";
      if (inherited || value == "-") valueColor = dimColor;
      break;
    }
    case HotkeyRow::Kind::Slots:
      label = "Direct slot hotkeys";
      value = slotHotkeysShown ? "all shown" : "bound ones shown";
      break;
    case HotkeyRow::Kind::Reset:
      if (hotkeyLevel == HotkeyLevel::AllConsoles) label = "Reset hotkeys to the defaults";
      else if (hotkeyLevel == HotkeyLevel::Console) label = "Reset " + game->system + "'s hotkeys";
      else label = "Reset this game's hotkeys";
      break;
    }
    print(x + 8 + indent, rowY, label, textColor, valueX - x - 16 - indent);
    print(valueX, rowY, value, valueColor, x + w - valueX - 8);
  }

  std::string note, hint = "Enter/A Choose   Esc/B Back";
  Color noteColor = warningColor;
  if (hotkeyCursor < count) {
    auto& row = rows[hotkeyCursor];
    if (row.kind == HotkeyRow::Kind::Level) {
      hint = "Left/Right or Enter/A Change   Esc/B Back";
      noteColor = dimColor;
      if (hotkeyLevel == HotkeyLevel::AllConsoles) {
        note = "For every game, unless its console or the game changes them.";
      } else if (hotkeyLevel == HotkeyLevel::Console) {
        note = "For every " + game->system + " game, unless the game changes them.";
      } else {
        note = "For " + game->title + " only.";
      }
    }
    if (row.kind == HotkeyRow::Kind::Action) {
      // A console's or a game's own action goes back to what it inherits; one it inherits is unbound for it.
      std::string clear = "Clear all";
      if (scoped) {
        auto source = hotkeySource(hotkeyLevel, row.action);
        auto parent = (HotkeyLevel)((int)hotkeyLevel - 1);
        noteColor = dimColor;
        note = (source == hotkeyLevel ? "Set for " : "From ") + inPhrase(source);
        clear = source == hotkeyLevel ? "Use " + inPhrase(parent) + (parent == HotkeyLevel::AllConsoles ? "'" : "'s")
                                      : "Unbind for " + inPhrase(hotkeyLevel);
      }
      hint = std::string(hotkeyOpen == row.action ? "Enter/A Close" : "Enter/A Bindings") + "   Del/X " + clear +
             "   Esc/B Back";
    }
    if (row.kind == HotkeyRow::Kind::Binding) {
      hint = "Enter/A Capture   Del/X Clear   Esc/B Back";
      auto bindings = bindingsOf(row.action);
      if (row.slot < (int)bindings.size() && !bindings[row.slot].empty()) {
        // A computer's keyboard is the game's where it has the key; its controller's buttons aren't on the keyboard.
        auto first = bindings[row.slot][0];
        bool gameButton = game && isKeyboardComputer(game->system) && tokenKind(first) == HotkeyKind::Key
                            ? !keyboardLabels(game->system, (SDL_Scancode)tokenCode(first)).empty()
                            : isGameButton(first);
        if (gameButton) note = "Its first key is also a game button, which the game then doesn't get.";
      }
    }
  }
  print(x + 8, y + h - 26, note, noteColor, w - 16);
  print(x + 8, y + h - 14, hint, dimColor, w - 16);

  if (capture) {
    auto keys = capture->combo.empty() ? std::string("(press a key or a button)") : hotkeyComboLabel(capture->combo);
    drawBox(width, height,
            {hotkeyLabel(capture->action) + ", binding " + std::to_string(capture->slot + 1),
             "Hold its keys together, the first one first.", "", keys},
            "Escape (or 5 seconds of nothing) cancels");
  } else if (question) {
    drawBox(width, height, wrap(question->text, 52), "Enter/A " + question->yes + "   Esc/B " + question->no);
  }
}

// A box over the page: [lines], then [hint].
auto Shell::drawBox(float width, float height, const std::vector<std::string>& lines, const std::string& hint) -> void {
  float boxWidth = std::min(width - 32, 440.0f);
  float boxHeight = 38 + lines.size() * 12.0f;
  float x = (width - boxWidth) / 2, y = (height - boxHeight) / 2;
  fill(0, 0, width, height, shadeColor);
  fill(x, y, boxWidth, boxHeight, panelColor);
  for (size_t i = 0; i < lines.size(); i++) print(x + 12, y + 12 + i * 12.0f, lines[i], textColor, boxWidth - 24);
  print(x + 12, y + boxHeight - 16, hint, dimColor, boxWidth - 24);
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
