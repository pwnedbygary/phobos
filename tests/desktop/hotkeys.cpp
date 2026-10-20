// The desktop's hotkey rules (desktop/Hotkeys.cpp), the Android app's (input/Hotkeys.kt) on SDL's keys, buttons, axes
// and mouse buttons: settings.ini's format, a combo's exact keys with its first key pressed first and held back from
// the game, tap bindings, the tokens that count only where bound, pads holding a token together and the keyboards
// theirs as one (desktop/Input.cpp), the axes' hysteresis, conflicts and their question, the defaults, the keys a
// computer's keyboard keeps, a binding's changes to store, and the levels: each one's key in settings.ini
// (desktop/Settings.cpp reading it back), a game's bindings over its console's over all consoles', and what a level
// keeps.
#include <desktop/Hotkeys.hpp>
#include <desktop/Input.hpp>
#include <desktop/Settings.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace phobos::desktop;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(condition) check((condition), #condition, __LINE__)

auto check(bool passed, const char* condition, int line) -> void {
  checks++;
  if (passed) return;
  failures++;
  std::printf("FAILED line %d: %s\n", line, condition);
}

// SDL 3.2.30's values (SDL_scancode.h, SDL_gamepad.h, SDL_mouse.h), written out rather than taken from the code tested.
constexpr int scancodeA = 4, scancodeEscape = 41, scancodeTab = 43, scancodeF5 = 62, scancodeF8 = 65, scancodeF9 = 66,
              scancodeF11 = 68, scancodeF12 = 69, scancodeLeftShift = 225;
constexpr int buttonSouth = 0, buttonWest = 2, buttonBack = 4, buttonGuide = 5, buttonStart = 6, buttonLeftStick = 7,
              buttonRightStick = 8, buttonLeftShoulder = 9, buttonRightShoulder = 10, buttonDpadUp = 11,
              buttonDpadDown = 12, buttonDpadLeft = 13, buttonDpadRight = 14;
constexpr int axisLeftX = 0, axisRightTrigger = 5;
constexpr int mouseLeft = 1, mouseX1 = 4;

const auto escape = keyToken(scancodeEscape), tab = keyToken(scancodeTab), f5 = keyToken(scancodeF5),
           f8 = keyToken(scancodeF8), f9 = keyToken(scancodeF9), f11 = keyToken(scancodeF11),
           f12 = keyToken(scancodeF12), leftShift = keyToken(scancodeLeftShift), keyA = keyToken(scancodeA);
const auto a = buttonToken(buttonSouth), x = buttonToken(buttonWest), select = buttonToken(buttonBack),
           guide = buttonToken(buttonGuide), start = buttonToken(buttonStart), l3 = buttonToken(buttonLeftStick),
           r3 = buttonToken(buttonRightStick), lb = buttonToken(buttonLeftShoulder),
           rb = buttonToken(buttonRightShoulder), up = buttonToken(buttonDpadUp), down = buttonToken(buttonDpadDown),
           left = buttonToken(buttonDpadLeft), right = buttonToken(buttonDpadRight);
const auto stickLeft = axisToken(axisLeftX, false), rt = axisToken(axisRightTrigger, true);
const auto click = mouseToken(mouseLeft), mouse4 = mouseToken(mouseX1);

using Names = std::vector<std::string>;

// An action's bindings in [hotkeys], none when it has none there.
auto of(const HotkeyMap& hotkeys, const std::string& action) -> HotkeyBindings {
  auto it = hotkeys.find(action);
  return it == hotkeys.end() ? HotkeyBindings{} : it->second;
}

auto bindingsAreKeptAsTextAndReadBackInTheirSlots() -> void {
  CHECK(encodeHotkeyBindings({{guide, rb}, {}, {f5}}) == "b5,b10;;k62");
  CHECK(encodeHotkeyBindings({{guide, rt}, {stickLeft, mouse4}}) == "b5,a5+;a0-,m4");
  CHECK(encodeHotkeyBindings({{f12}, {}, {}}) == "k69");
  CHECK(encodeHotkeyBindings({}).empty());
  CHECK(parseHotkeyBindings("b5,b10;;k62") == (HotkeyBindings{{guide, rb}, {}, {f5}}));
  CHECK(parseHotkeyBindings("b5,a5+;a0-,m4") == (HotkeyBindings{{guide, rt}, {stickLeft, mouse4}}));
  CHECK(parseHotkeyBindings("").empty());
  CHECK(parseHotkeyBindings(";k69") == (HotkeyBindings{{}, {f12}}));
  // A token it can't read and a key a combo already has are left out; a fourth binding is beyond the limit.
  CHECK(parseHotkeyBindings("x1,k62,k62, b5 ;a5") == (HotkeyBindings{{f5, guide}}));
  CHECK(parseHotkeyBindings("k41;k43;k62;k65") == (HotkeyBindings{{escape}, {tab}, {f5}}));
  // So is a code of more than six digits, among them one too big for an int.
  CHECK(parseHotkeyBindings("k1234567,k62;a12345678+;b99999999999") == (HotkeyBindings{{f5}}));
}

auto replacingABindingKeepsTheOthersInTheirSlots() -> void {
  CHECK(withHotkeyBinding({{guide, rb}, {f5}}, 0, {}) == (HotkeyBindings{{}, {f5}}));
  CHECK(withHotkeyBinding({{guide, rb}, {f5}}, 1, {}) == (HotkeyBindings{{guide, rb}}));
  CHECK(withHotkeyBinding({{guide, rb}}, 2, {f8}) == (HotkeyBindings{{guide, rb}, {}, {f8}}));
  CHECK(withHotkeyBinding({{guide, rb}}, 3, {f8}) == (HotkeyBindings{{guide, rb}}));
}

auto aComboRunsWhenExactlyItsKeysAreHeldItsFirstKeyFirst() -> void {
  HotkeyMap hotkeys{{Hotkey::save, {{guide, rb}}}};
  HotkeyTracker tracker;
  CHECK(tracker.press(hotkeys, guide));
  CHECK(tracker.press(hotkeys, rb));
  CHECK(tracker.completions(hotkeys) == Names{Hotkey::save});
  // Held on, it isn't completed again as another key comes and goes, nor by a release.
  CHECK(!tracker.press(hotkeys, a));
  CHECK(tracker.completions(hotkeys).empty());
  tracker.release(a);
  CHECK(tracker.completions(hotkeys).empty());
  tracker.release(rb);
  tracker.release(guide);

  // Its other key first: that key reached the game, and the combo doesn't run.
  CHECK(!tracker.press(hotkeys, rb));
  CHECK(tracker.press(hotkeys, guide));
  CHECK(tracker.completions(hotkeys).empty());
  tracker.release(rb);
  tracker.release(guide);

  // Another key held too: not this combo.
  tracker.press(hotkeys, a);
  tracker.press(hotkeys, guide);
  tracker.press(hotkeys, rb);
  CHECK(tracker.completions(hotkeys).empty());
}

auto aCombosFirstKeyIsHeldBackAndItsOtherKeysOnlyWhileThatOneIsDown() -> void {
  HotkeyMap hotkeys{{Hotkey::save, {{guide, rb}}}};
  HotkeyTracker tracker;
  CHECK(!tracker.press(hotkeys, rb));
  CHECK(!tracker.taken(rb));
  CHECK(!tracker.release(rb));
  CHECK(tracker.press(hotkeys, guide));
  CHECK(tracker.taken(guide));
  CHECK(tracker.press(hotkeys, rb));
  CHECK(tracker.release(rb));
  CHECK(tracker.release(guide));
  CHECK(!tracker.taken(guide));
}

auto aKeyThatLeadsLongerCombosRunsAloneWhenTapped() -> void {
  HotkeyMap hotkeys{{Hotkey::pause, {{guide}}}, {Hotkey::save, {{guide, rb}}}};
  HotkeyTracker tracker;
  CHECK(tracker.press(hotkeys, guide));
  CHECK(tracker.completions(hotkeys).empty());
  tracker.release(guide);
  CHECK(tracker.taps(hotkeys) == Names{Hotkey::pause});
  CHECK(tracker.taps(hotkeys).empty());

  // Pressed with another key, it's not a tap.
  tracker.press(hotkeys, guide);
  tracker.press(hotkeys, rb);
  CHECK(tracker.completions(hotkeys) == Names{Hotkey::save});
  tracker.release(rb);
  tracker.release(guide);
  CHECK(tracker.taps(hotkeys).empty());

  // Nor when a key was held as it went down.
  tracker.press(hotkeys, a);
  tracker.press(hotkeys, guide);
  tracker.release(guide);
  CHECK(tracker.taps(hotkeys).empty());
}

auto shiftTabDoesntHoldFastForwardButTabDoes() -> void {
  HotkeyMap hotkeys{{Hotkey::fastForwardHold, {{tab}}}};
  HotkeyTracker tracker;
  tracker.press(hotkeys, leftShift);
  tracker.press(hotkeys, tab);
  CHECK(tracker.completions(hotkeys).empty());
  tracker.release(tab);
  tracker.release(leftShift);
  tracker.press(hotkeys, tab);
  CHECK(tracker.completions(hotkeys) == Names{Hotkey::fastForwardHold});
  CHECK(tracker.holding({tab}));
  tracker.press(hotkeys, a);
  CHECK(tracker.holding({tab}));
  tracker.release(tab);
  CHECK(!tracker.holding({tab}));
}

auto aStickDirectionOrAMouseButtonCountsOnlyWhereABindingUsesOneAndATriggerAlways() -> void {
  HotkeyMap hotkeys{{Hotkey::save, {{guide, rb}}}};
  HotkeyTracker tracker;
  CHECK(!tracker.press(hotkeys, stickLeft));
  CHECK(!tracker.press(hotkeys, click));
  tracker.press(hotkeys, guide);
  tracker.press(hotkeys, rb);
  CHECK(tracker.completions(hotkeys) == Names{Hotkey::save});
  tracker.reset();

  // A trigger is the game's L2 or R2 button: held, it keeps the combo from matching, as a button would.
  tracker.press(hotkeys, rt);
  tracker.press(hotkeys, guide);
  tracker.press(hotkeys, rb);
  CHECK(tracker.completions(hotkeys).empty());
  tracker.reset();

  HotkeyMap bound{{Hotkey::mute, {{guide, stickLeft}}}, {Hotkey::screenshot, {{mouse4}}}};
  tracker.press(bound, guide);
  CHECK(tracker.press(bound, stickLeft));
  CHECK(tracker.completions(bound) == Names{Hotkey::mute});
  tracker.reset();
  CHECK(tracker.press(bound, mouse4));
  CHECK(tracker.completions(bound) == Names{Hotkey::screenshot});
}

auto anActionMatchedByTwoBindingsOrATapBoundTwiceRunsOnce() -> void {
  HotkeyMap hotkeys{{Hotkey::save, {{guide, rb, a}, {guide, a, rb}}}};
  HotkeyTracker tracker;
  tracker.press(hotkeys, guide);
  tracker.press(hotkeys, rb);
  tracker.press(hotkeys, a);
  CHECK(tracker.matches(hotkeys) == Names{Hotkey::save});
  CHECK(tracker.completions(hotkeys) == Names{Hotkey::save});
  tracker.reset();

  HotkeyMap taps{{Hotkey::pause, {{guide}, {guide}}}, {Hotkey::save, {{guide, rb}}}};
  tracker.press(taps, guide);
  tracker.release(guide);
  CHECK(tracker.taps(taps) == Names{Hotkey::pause});
}

auto aHoldsOwnLongerBindingLeavesItsKeyNoTapButAnotherActionsMakesItOne() -> void {
  CHECK(!isTapCombo({{Hotkey::fastForwardHold, {{tab}, {tab, keyA}}}}, {tab}));
  CHECK(isTapCombo({{Hotkey::fastForwardHold, {{tab}}}, {Hotkey::save, {{tab, keyA}}}}, {tab}));
  CHECK(isTapCombo({{Hotkey::save, {{f5}, {f5, keyA}}}}, {f5}));
  CHECK(!isTapCombo({{Hotkey::save, {{guide, rb}}}}, {guide, rb}));

  HotkeyMap hotkeys{{Hotkey::fastForwardHold, {{tab}, {tab, keyA}}}};
  HotkeyTracker tracker;
  tracker.press(hotkeys, tab);
  CHECK(tracker.completions(hotkeys) == Names{Hotkey::fastForwardHold});
}

auto padsHoldingATokenTogetherLetItGoWhenTheLastDoes() -> void {
  HotkeyHolders holders;
  CHECK(holders.press(rb, 1));
  CHECK(!holders.press(rb, 2));
  CHECK(!holders.release(rb, 1));
  CHECK(holders.release(rb, 2));
  CHECK(!holders.release(rb, 2));
  // A release from a device that never pressed it lets nothing go.
  CHECK(holders.press(a, 1));
  CHECK(!holders.release(a, 3));
  CHECK(holders.press(guide, 2));
  CHECK(!holders.press(a, 2));
  CHECK(holders.remove(2) == HotkeyCombo{guide});
  CHECK(holders.release(a, 1));
}

// SDL gives a key the ID of the keyboard that pressed it (Wayland its seat's keyboard's, macOS 1), but lets go of the
// keys held as the window loses focus as keyboard 0's (SDL_ResetKeyboard, in SDL 3.2.30's SDL_keyboard.c).
auto aKeyHeldAsTheWindowLosesFocusIsLetGoOfWhicheverKeyboardPressedIt() -> void {
  for (SDL_KeyboardID keyboard : {0u, 1u, 7u}) {
    Input input;
    auto key = [&](bool down, SDL_KeyboardID from, bool repeat = false) {
      SDL_Event event{};
      event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      event.key.which = from;
      event.key.scancode = SDL_Scancode(scancodeTab);
      event.key.down = down;
      event.key.repeat = repeat;
      return input.hotkeyChanges(event);
    };
    auto changed = [](const std::vector<HotkeyChange>& changes, bool down) {
      return changes.size() == 1 && changes[0].token == tab && changes[0].down == down;
    };
    CHECK(changed(key(true, keyboard), true));
    CHECK(key(true, keyboard, true).empty());
    CHECK(changed(key(false, 0), false));
    CHECK(key(false, keyboard).empty());
  }
}

auto anAxisPressesPastHalfAndLetsGoBelowFortyPercent() -> void {
  auto held = latchAxis({}, 0.45f);
  CHECK(!held.positive && !held.negative);
  held = latchAxis(held, 0.55f);
  CHECK(held.positive);
  held = latchAxis(held, 0.45f);
  CHECK(held.positive);
  held = latchAxis(held, 0.39f);
  CHECK(!held.positive);
  held = latchAxis({}, -0.6f);
  CHECK(held.negative && !held.positive);
  held = latchAxis(held, 1.0f);
  CHECK(held.positive && !held.negative);
}

auto conflictsNameTheCollidingBindingsAndLeaveTheActionsOwnOut() -> void {
  HotkeyMap hotkeys{
    {Hotkey::pause, {{guide}, {f12}}},
    {Hotkey::save, {{guide, rb}, {f5}}},
    {Hotkey::load, {{guide, lb, a}}},
    {Hotkey::mute, {{a, x}}},
  };
  auto conflicts = hotkeyConflicts(hotkeys, Hotkey::screenshot, {f5});
  CHECK(conflicts.size() == 1 && conflicts[0].action == Hotkey::save && conflicts[0].slot == 1 &&
        conflicts[0].kind == HotkeyConflict::Kind::Exact);
  conflicts = hotkeyConflicts(hotkeys, Hotkey::screenshot, {guide, lb});
  CHECK(conflicts.size() == 1 && conflicts[0].action == Hotkey::load &&
        conflicts[0].kind == HotkeyConflict::Kind::Overlap);
  // x leads a combo here and is mute's later key.
  conflicts = hotkeyConflicts(hotkeys, Hotkey::screenshot, {x, r3});
  CHECK(conflicts.size() == 1 && conflicts[0].action == Hotkey::mute &&
        conflicts[0].kind == HotkeyConflict::Kind::CrossRole);
  // A tap and the longer combos its key leads are fine; sharing only a first key is fine.
  CHECK(hotkeyConflicts(hotkeys, Hotkey::screenshot, {guide, r3}).empty());
  // A hold can't be a tap: a hold on the key that leads the others' combos collides with them.
  CHECK(hotkeyConflicts(hotkeys, Hotkey::fastForwardHold, {guide}).size() == 3);
  // The action's own bindings aren't checked.
  CHECK(hotkeyConflicts(hotkeys, Hotkey::save, {f5}).empty());
  CHECK(hotkeyConflicts(hotkeys, Hotkey::screenshot, {}).empty());
}

auto theQuestionNamesTheExactConflictFirstThenTheOverlaps() -> void {
  using Kind = HotkeyConflict::Kind;
  CHECK(hotkeyConflictMessage({{Hotkey::save, 1, Kind::Exact}}) ==
        "The selected hotkey combo is already bound to Save state, would you like to overwrite?");
  CHECK(hotkeyConflictMessage({{Hotkey::save, 0, Kind::Overlap}, {Hotkey::load, 0, Kind::CrossRole},
                               {Hotkey::mute, 0, Kind::Overlap}}) ==
        "The selected hotkey combo overlaps Save state, Load state and Mute, would you like to overwrite?");
  CHECK(hotkeyConflictMessage({{Hotkey::save, 1, Kind::Exact}, {Hotkey::load, 0, Kind::Overlap}}) ==
        "The selected hotkey combo is already bound to Save state, would you like to overwrite? It also overlaps "
        "Load state, which would lose that binding too.");
  CHECK(hotkeyConflictMessage({{Hotkey::save, 1, Kind::Exact}, {Hotkey::load, 0, Kind::Overlap},
                               {Hotkey::load, 1, Kind::Overlap}}) ==
        "The selected hotkey combo is already bound to Save state, would you like to overwrite? It also overlaps "
        "Load state, which would lose those bindings too.");
}

auto existingConflictsNameEachPairOnce() -> void {
  HotkeyMap hotkeys{{Hotkey::save, {{f5}, {guide, rb}}}, {Hotkey::load, {{f5}, {guide, rb}}}, {Hotkey::mute, {{f8}}}};
  auto pairs = existingHotkeyConflicts(hotkeys);
  CHECK(pairs.size() == 1);
  CHECK(pairs.size() == 1 && ((pairs[0].first == Hotkey::load && pairs[0].second == Hotkey::save) ||
                              (pairs[0].first == Hotkey::save && pairs[0].second == Hotkey::load)));
}

auto theDefaultsLeaveSelectAndStartToTheGameKeepTheKeyboardsKeysAndDontCollide() -> void {
  auto& defaults = defaultHotkeys();
  CHECK(existingHotkeyConflicts(defaults).empty());
  for (auto& [action, bindings] : defaults) {
    for (auto& combo : bindings) {
      for (auto token : combo) CHECK(token != select && token != start);
    }
  }
  auto bindings = [&](const char* action) { return of(defaults, action); };
  CHECK(bindings(Hotkey::pause) == (HotkeyBindings{{guide}, {f12}, {escape}}));
  CHECK(bindings(Hotkey::save) == (HotkeyBindings{{guide, rb}, {f5}}));
  CHECK(bindings(Hotkey::load) == (HotkeyBindings{{guide, lb}, {f9}}));
  CHECK(bindings(Hotkey::screenshot) == (HotkeyBindings{{guide, r3}, {f8}}));
  CHECK(bindings(Hotkey::fullscreen) == (HotkeyBindings{{f11}}));
  CHECK(bindings(Hotkey::fastForwardHold) == (HotkeyBindings{{tab}}));
  CHECK(bindings(Hotkey::fastForwardToggle) == (HotkeyBindings{{guide, rt}}));
  CHECK(bindings(Hotkey::nextSlot) == (HotkeyBindings{{guide, right}}));
  CHECK(bindings(Hotkey::previousSlot) == (HotkeyBindings{{guide, left}}));
  CHECK(bindings(Hotkey::frameAdvance) == (HotkeyBindings{{guide, l3}}));
  CHECK(bindings(Hotkey::n64Pak) == (HotkeyBindings{{guide, down}}));
  CHECK(bindings(Hotkey::ps1Analog) == (HotkeyBindings{{guide, up}}));
  CHECK(bindings(Hotkey::mute) == (HotkeyBindings{{guide, a}}));
  CHECK(bindings(Hotkey::reload) == (HotkeyBindings{{guide, x}}));
  for (auto action : {Hotkey::reset, Hotkey::quit, Hotkey::pauseWindow, Hotkey::disc}) CHECK(bindings(action).empty());

  // Guide tapped opens the menu; Guide held with RB saves.
  HotkeyTracker tracker;
  tracker.press(defaults, guide);
  tracker.release(guide);
  CHECK(tracker.taps(defaults) == Names{Hotkey::pause});
  tracker.press(defaults, guide);
  tracker.press(defaults, rb);
  CHECK(tracker.completions(defaults) == Names{Hotkey::save});
}

auto aStoredActionReplacesItsDefaultsAndAnEmptyOneUnbindsIt() -> void {
  auto shown = shownHotkeys({{Hotkey::save, {{f8}}}, {Hotkey::pause, {}}, {Hotkey::reset, {{}, {guide, start}}}});
  CHECK(of(shown, Hotkey::save) == (HotkeyBindings{{f8}}));
  CHECK(!shown.count(Hotkey::pause));
  CHECK(of(shown, Hotkey::reset) == (HotkeyBindings{{}, {guide, start}}));
  CHECK(of(shown, Hotkey::load) == (HotkeyBindings{{guide, lb}, {f9}}));
}

auto aDefaultBindingOnAKeyTheComputersKeyboardUsesStaysTheGames() -> void {
  // An MSX's keyboard has F5 and Escape; Tab stays a computer's.
  auto msxKey = [](HotkeyToken token) { return token == f5 || token == escape || token == tab; };
  auto hotkeys = resolveHotkeys({}, msxKey);
  CHECK(of(hotkeys, Hotkey::pause) == (HotkeyBindings{{guide}, {f12}}));
  CHECK(of(hotkeys, Hotkey::save) == (HotkeyBindings{{guide, rb}}));
  CHECK(of(hotkeys, Hotkey::load) == (HotkeyBindings{{guide, lb}, {f9}}));
  CHECK(!hotkeys.count(Hotkey::fastForwardHold));
  // A binding of the player's own on such a key applies; a default one stays off even stored.
  hotkeys = resolveHotkeys({{Hotkey::load, {{f5}}}, {Hotkey::save, {{f5}, {guide, rb}}}}, msxKey);
  CHECK(of(hotkeys, Hotkey::load) == (HotkeyBindings{{f5}}));
  CHECK(of(hotkeys, Hotkey::save) == (HotkeyBindings{{}, {guide, rb}}));
  CHECK(resolveHotkeys({}, nullptr) == shownHotkeys({}));
}

auto aBindingStoresItsActionAndEachBindingItOverwritesWhole() -> void {
  auto shown = shownHotkeys({});
  auto changes = bindingChanges(shown, Hotkey::screenshot, 1, {f5}, {{Hotkey::save, 1}});
  CHECK(changes.size() == 2);
  CHECK(of(changes, Hotkey::screenshot) == (HotkeyBindings{{guide, r3}, {f5}}));
  CHECK(of(changes, Hotkey::save) == (HotkeyBindings{{guide, rb}}));
  changes = bindingChanges(shown, Hotkey::reset, 2, {guide, start}, {});
  CHECK(changes.size() == 1 && of(changes, Hotkey::reset) == (HotkeyBindings{{}, {}, {guide, start}}));
  changes = bindingChanges(shown, Hotkey::save, 0, {}, {});
  CHECK(of(changes, Hotkey::save) == (HotkeyBindings{{}, {f5}}));
}

auto theDirectSlotsAndTheHoldsAreKnownByName() -> void {
  CHECK(directSlot("save_slot_3") == 3);
  CHECK(directSlot("load_slot_9") == 9);
  CHECK(directSlot("save_slot_0") == 0);
  CHECK(directSlot("save_slot_10") == 0);
  CHECK(directSlot(Hotkey::save) == 0);
  CHECK(hotkeyLabel("load_slot_9") == "Load state 9");
  CHECK(isHotkeyHold(Hotkey::fastForwardHold) && isHotkeyHold(Hotkey::frameAdvance) && !isHotkeyHold(Hotkey::save));
  CHECK(hotkeyActions().size() == 36);
}

auto eachLevelKeepsItsBindingsUnderItsOwnKeyWhateverTheTitle() -> void {
  CHECK(hotkeySettingKey("", Hotkey::save) == "hotkey.save");
  CHECK(hotkeySettingKey("Game Boy", Hotkey::save) == "hotkey[Game Boy].save");
  CHECK(hotkeySettingKey("Game Boy/Tetris (World)", Hotkey::load) == "hotkey[Game Boy/Tetris (World)].load");
  // "=" would end the key in settings.ini and a line break its line; "%" written out keeps two titles apart.
  CHECK(hotkeySettingKey("MSX/A=B%C\r\nD", Hotkey::save) == "hotkey[MSX/A%3DB%25C%0D%0AD].save");
  CHECK(hotkeySettingKey("MSX/A%3DB", Hotkey::save) != hotkeySettingKey("MSX/A=B", Hotkey::save));
  // Written to a file and read back.
  auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto file = std::filesystem::temp_directory_path() / ("phobos-hotkeys-test-" + std::to_string(stamp) + ".ini");
  auto oddTitle = hotkeySettingKey("MSX/A=B\nC", Hotkey::save), allConsoles = hotkeySettingKey("", Hotkey::save);
  {
    Settings written;
    written.load(file.string());
    written.setText(oddTitle, "k62");
    written.setText(allConsoles, "k63");
  }
  Settings read;
  read.load(file.string());
  CHECK(read.text(oddTitle) == "k62");
  CHECK(read.text(allConsoles) == "k63");
  std::filesystem::remove(file);
}

auto aGamesBindingsGoOverItsConsolesWhichGoOverAllConsoles() -> void {
  HotkeyMap allConsoles{{Hotkey::save, {{f8}}}, {Hotkey::mute, {{f11}}}};
  HotkeyMap console{{Hotkey::save, {{f5}, {guide, rb}}}, {Hotkey::pause, {}}};
  HotkeyMap game{{Hotkey::save, {{}, {f12}}}, {Hotkey::load, {{escape}}}};
  auto atConsole = shownHotkeys(overHotkeys(allConsoles, console));
  CHECK(of(atConsole, Hotkey::save) == (HotkeyBindings{{f5}, {guide, rb}}));
  CHECK(!atConsole.count(Hotkey::pause));
  CHECK(of(atConsole, Hotkey::mute) == (HotkeyBindings{{f11}}));
  CHECK(of(atConsole, Hotkey::load) == (HotkeyBindings{{guide, lb}, {f9}}));
  // A game's action replaces its console's whole: the console's F5 doesn't show through the game's empty first slot.
  auto atGame = shownHotkeys(overHotkeys(overHotkeys(allConsoles, console), game));
  CHECK(of(atGame, Hotkey::save) == (HotkeyBindings{{}, {f12}}));
  CHECK(of(atGame, Hotkey::load) == (HotkeyBindings{{escape}}));
  CHECK(!atGame.count(Hotkey::pause));
  CHECK(of(atGame, Hotkey::mute) == (HotkeyBindings{{f11}}));
  CHECK(overHotkeys(allConsoles, {}) == allConsoles);
}

auto aConsoleOrAGameStoresOnlyWhatItChanges() -> void {
  // What a game inherits: its console's Save (F5, then Guide+RB), and the defaults.
  auto inherited = shownHotkeys({{Hotkey::save, {{f5}, {guide, rb}}}});
  auto stored = levelHotkeyChanges(bindingChanges(inherited, Hotkey::save, 0, {f8}, {}), inherited);
  CHECK(stored.size() == 1 && stored[Hotkey::save] == (HotkeyBindings{{f8}, {guide, rb}}));
  // Bound again as it inherits it: no longer the level's own.
  stored = levelHotkeyChanges(bindingChanges(inherited, Hotkey::save, 0, {f5}, {}), inherited);
  CHECK(stored.size() == 1 && stored.count(Hotkey::save) && !stored[Hotkey::save]);
  // Unbinding what it inherits bound is stored, as nothing; what it inherits unbound has nothing to store.
  stored = levelHotkeyChanges({{Hotkey::pause, {}}, {Hotkey::reset, {}}}, inherited);
  CHECK(stored[Hotkey::pause] == HotkeyBindings{});
  CHECK(stored.count(Hotkey::reset) && !stored[Hotkey::reset]);
  // An overwritten binding: the action it belongs to keeps its others, as the level's own.
  stored = levelHotkeyChanges(bindingChanges(inherited, Hotkey::screenshot, 1, {f5}, {{Hotkey::save, 0}}), inherited);
  CHECK(stored[Hotkey::screenshot] == (HotkeyBindings{{guide, r3}, {f5}}));
  CHECK(stored[Hotkey::save] == (HotkeyBindings{{}, {guide, rb}}));
}

}

int main() {
  bindingsAreKeptAsTextAndReadBackInTheirSlots();
  replacingABindingKeepsTheOthersInTheirSlots();
  aComboRunsWhenExactlyItsKeysAreHeldItsFirstKeyFirst();
  aCombosFirstKeyIsHeldBackAndItsOtherKeysOnlyWhileThatOneIsDown();
  aKeyThatLeadsLongerCombosRunsAloneWhenTapped();
  shiftTabDoesntHoldFastForwardButTabDoes();
  aStickDirectionOrAMouseButtonCountsOnlyWhereABindingUsesOneAndATriggerAlways();
  anActionMatchedByTwoBindingsOrATapBoundTwiceRunsOnce();
  aHoldsOwnLongerBindingLeavesItsKeyNoTapButAnotherActionsMakesItOne();
  padsHoldingATokenTogetherLetItGoWhenTheLastDoes();
  aKeyHeldAsTheWindowLosesFocusIsLetGoOfWhicheverKeyboardPressedIt();
  anAxisPressesPastHalfAndLetsGoBelowFortyPercent();
  conflictsNameTheCollidingBindingsAndLeaveTheActionsOwnOut();
  theQuestionNamesTheExactConflictFirstThenTheOverlaps();
  existingConflictsNameEachPairOnce();
  theDefaultsLeaveSelectAndStartToTheGameKeepTheKeyboardsKeysAndDontCollide();
  aStoredActionReplacesItsDefaultsAndAnEmptyOneUnbindsIt();
  aDefaultBindingOnAKeyTheComputersKeyboardUsesStaysTheGames();
  aBindingStoresItsActionAndEachBindingItOverwritesWhole();
  theDirectSlotsAndTheHoldsAreKnownByName();
  eachLevelKeepsItsBindingsUnderItsOwnKeyWhateverTheTitle();
  aGamesBindingsGoOverItsConsolesWhichGoOverAllConsoles();
  aConsoleOrAGameStoresOnlyWhatItChanges();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
