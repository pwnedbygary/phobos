#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace phobos::desktop {

// What a hotkey is made of: a key, a pad button, a pad axis pushed one way, or a mouse button. Its kind is in the top
// byte and its code below: an SDL_Scancode, an SDL_GamepadButton, an SDL_GamepadAxis doubled (plus one for the
// positive direction), a mouse button (SDL_BUTTON_LEFT...). The same button on any pad is the same token.
using HotkeyToken = std::uint32_t;
enum class HotkeyKind : std::uint32_t { Key = 1, Button, Axis, Mouse };

constexpr auto keyToken(int scancode) -> HotkeyToken { return 1u << 24 | (std::uint32_t)scancode; }
constexpr auto buttonToken(int button) -> HotkeyToken { return 2u << 24 | (std::uint32_t)button; }
constexpr auto axisToken(int axis, bool positive) -> HotkeyToken {
  return 3u << 24 | (std::uint32_t)axis << 1 | (positive ? 1u : 0u);
}
constexpr auto mouseToken(int button) -> HotkeyToken { return 4u << 24 | (std::uint32_t)button; }
constexpr auto tokenKind(HotkeyToken token) -> HotkeyKind { return HotkeyKind(token >> 24); }
// The scancode, button or mouse button; for an axis, the axis doubled plus its direction (tokenAxis, tokenPositive).
constexpr auto tokenCode(HotkeyToken token) -> int { return (int)(token & 0xffffff); }
constexpr auto tokenAxis(HotkeyToken token) -> int { return tokenCode(token) >> 1; }
constexpr auto tokenPositive(HotkeyToken token) -> bool { return tokenCode(token) & 1; }

// A combo: keys held together, its first key first. That key starts it and is held back from the game.
using HotkeyCombo = std::vector<HotkeyToken>;
// An action's bindings: up to hotkeyBindingLimit combos, any of which runs it, each in its slot (an empty one is a
// cleared slot).
using HotkeyBindings = std::vector<HotkeyCombo>;
// Each action's bindings, by the action's name (settings.ini keeps them as hotkey.<name>).
using HotkeyMap = std::map<std::string, HotkeyBindings>;

// As many bindings as ares allows an action (its BindingLimit).
constexpr int hotkeyBindingLimit = 3;

// The actions' names.
struct Hotkey {
  static constexpr const char* pause = "pause";
  static constexpr const char* pauseWindow = "pause_window";
  static constexpr const char* save = "save";
  static constexpr const char* load = "load";
  static constexpr const char* nextSlot = "inc_slot";
  static constexpr const char* previousSlot = "dec_slot";
  static constexpr const char* screenshot = "screenshot";
  static constexpr const char* fullscreen = "fullscreen";
  static constexpr const char* fastForwardHold = "ff_hold";
  static constexpr const char* fastForwardToggle = "ff_toggle";
  static constexpr const char* frameAdvance = "frame_advance";
  static constexpr const char* slowMotion = "slow_motion";
  static constexpr const char* mute = "mute";
  static constexpr const char* reset = "reset";
  static constexpr const char* reload = "reload";
  static constexpr const char* quit = "quit";
  static constexpr const char* n64Pak = "n64_pak";
  static constexpr const char* disc = "disc";
  static constexpr const char* ps1Analog = "analog_toggle";
  static constexpr const char* saveSlotPrefix = "save_slot_";
  static constexpr const char* loadSlotPrefix = "load_slot_";
};

struct HotkeyAction {
  std::string name;
  std::string label;
};
// Every action, in the Hotkeys page's order, the direct slots (save_slot_1 to load_slot_9) last.
auto hotkeyActions() -> const std::vector<HotkeyAction>&;
auto hotkeyLabel(const std::string& action) -> std::string;
// A save or load of one slot by its number (1 to 9), or 0 for another action.
auto directSlot(const std::string& action) -> int;
// The actions that last while their combo is held (fast-forward hold, slow motion, frame advance), rather than running
// once.
auto isHotkeyHold(const std::string& action) -> bool;

// [bindings] with [slot]'s combo replaced by [combo] (an empty one clears it); the empty slots after the last bound one
// are dropped.
auto withHotkeyBinding(HotkeyBindings bindings, int slot, const HotkeyCombo& combo) -> HotkeyBindings;
// As settings.ini keeps them: each token as k<scancode>, b<button>, a<axis>+ or a<axis>-, m<button>; a combo's tokens
// joined by ",", the bindings by ";", an empty slot kept so the later ones keep their places.
auto encodeHotkeyBindings(const HotkeyBindings& bindings) -> std::string;
// A kept value read back: a token it can't read, or one a combo already has, is left out.
auto parseHotkeyBindings(const std::string& value) -> HotkeyBindings;

// The hotkeys of a player who never set one (Hotkeys.cpp says which).
auto defaultHotkeys() -> const HotkeyMap&;
// Every action's bindings: its own [stored] ones, else its defaults.
auto shownHotkeys(const HotkeyMap& stored) -> HotkeyMap;
// The hotkeys in force: shownHotkeys, less each default binding with a token [gameKey] keeps for the game (a key the
// running computer's keyboard uses), which stays the game's even when stored. Any other binding applies.
auto resolveHotkeys(const HotkeyMap& stored, const std::function<bool(HotkeyToken)>& gameKey) -> HotkeyMap;

// A one-key combo whose key leads longer combos: a tap binding. It runs when its key is let go with nothing else pressed
// meanwhile, so it doesn't run as the longer combos are pressed. A hold's own longer bindings don't make that hold's
// one-key binding a tap: whichever of them is held, the hold is the same, so its key starts it at once (a tap, coming as
// its key is let go, can't start a hold).
auto isTapCombo(const HotkeyMap& hotkeys, const HotkeyCombo& combo) -> bool;

// The tokens held, in the order they were pressed, matched against the combos.
//
// A combo's first key is held back from the game, and so are its other keys while that one is down. Those keys still
// reach the game pressed alone, and a combo runs only when its first key was pressed before the rest, so no key of a
// combo leaks into the game and no game button waits. A tap binding (isTapCombo) runs as its key is let go instead
// (taps). A stick direction or a mouse button counts only where a binding uses one: a stick held for the game or a
// click on the window doesn't keep a combo from matching.
struct HotkeyTracker {
  auto reset() -> void;
  // [token] went down (on its first device): true when the game mustn't see it.
  auto press(const HotkeyMap& hotkeys, HotkeyToken token) -> bool;
  // [token] went up (on its last device): true when its press was held back from the game.
  auto release(HotkeyToken token) -> bool;
  // The combos the presses since the last look completed (matches): a release completes none, only a tap (taps), and a
  // combo still held isn't completed again as other keys come and go.
  auto completions(const HotkeyMap& hotkeys) -> std::vector<std::string>;
  // The actions with a binding whose combo is exactly the set held, its first key pressed first, never a tap binding:
  // each action once, however many of its bindings match.
  auto matches(const HotkeyMap& hotkeys) const -> std::vector<std::string>;
  // The actions a tap binding of the last release completed, each reported once.
  auto taps(const HotkeyMap& hotkeys) -> std::vector<std::string>;
  // Whether every key of [combo] is held, others or not: how long a hold action lasts.
  auto holding(const HotkeyCombo& combo) const -> bool;
  // Whether [token]'s press was held back from the game.
  auto taken(HotkeyToken token) const -> bool;

private:
  auto takenOnPress(const HotkeyMap& hotkeys, HotkeyToken token) const -> bool;
  std::vector<HotkeyToken> order;
  std::set<HotkeyToken> heldBack;
  std::optional<HotkeyToken> tapCandidate;
  std::optional<HotkeyToken> tapped;
  bool pressedSinceLook = false;
};

// Which devices hold each token: the same button held on two pads goes up when both let go of it.
struct HotkeyHolders {
  // [device] pressed [token]: true when no device held it before.
  auto press(HotkeyToken token, std::uint64_t device) -> bool;
  // [device] let go of [token]: true when it held it and no device does now.
  auto release(HotkeyToken token, std::uint64_t device) -> bool;
  // [device] went away: the tokens no device holds now.
  auto remove(std::uint64_t device) -> std::vector<HotkeyToken>;

private:
  std::map<HotkeyToken, std::set<std::uint64_t>> devices;
};

// An axis's directions held: past half presses one, below 40% lets it go, so a noisy axis near the threshold can't
// chatter (Input.cpp's hysteresis for the game's buttons).
struct AxisDirections {
  bool negative = false;
  bool positive = false;
};
// The directions held at [value] (-1 to 1), given [held] before.
auto latchAxis(AxisDirections held, float value) -> AxisDirections;

// [action]'s binding in [slot] collides with another, as [kind].
struct HotkeyConflict {
  enum class Kind { Exact, Overlap, CrossRole };
  std::string action;
  int slot = 0;
  Kind kind = Kind::Exact;
};
// The other actions' bindings that binding [combo] to one of [action]'s would overwrite: the same set, a combo that is
// part of it or contains it, or a key that is one combo's first key and another's later one. Two combos that share only
// their first key are fine, and so are a tap binding and the longer combos its key leads (isTapCombo), unless the tap is
// a hold action, which can't wait for its key to be let go. [action]'s own other bindings aren't checked, since
// whichever of them runs, the action is the same; but where a combo of two keys or more is part of another of the same
// action, not a hold, pressing the longer can run the action twice, the shorter completing on the way.
auto hotkeyConflicts(const HotkeyMap& hotkeys, const std::string& action, const HotkeyCombo& combo)
    -> std::vector<HotkeyConflict>;
// The question a conflicting binding asks. An exact conflict asks whether to overwrite the action(s) bound to the same
// combo and names any that only overlap it after; with no exact one, the overlaps are asked about. Overwriting clears
// only the colliding bindings, so the overlapped actions keep their others.
auto hotkeyConflictMessage(const std::vector<HotkeyConflict>& conflicts) -> std::string;
// The pairs of actions whose bindings already collide, by hotkeyConflicts' rules, each pair once.
auto existingHotkeyConflicts(const HotkeyMap& hotkeys) -> std::vector<std::pair<std::string, std::string>>;
// The bindings to store after [action]'s binding in [slot] is set to [combo] (an empty one clears it) and each
// (action, slot) in [overwrite] is cleared, from [shown] (shownHotkeys): each changed action with all its bindings.
auto bindingChanges(const HotkeyMap& shown, const std::string& action, int slot, const HotkeyCombo& combo,
                    const std::vector<std::pair<std::string, int>>& overwrite) -> HotkeyMap;

// Hotkeys per console and per game: a level stores each action it changes with all of that action's bindings, which
// replace what it inherits whole, an empty value unbinding the action there; a game's over its console's, over all
// consoles', over the defaults.
// The settings.ini key of [action]'s bindings at [scope]: hotkey.<action> for all consoles (no scope), and
// hotkey[<scope>].<action> for a console (its system) or a game (its system, "/", its title), with "%", "=" and line
// breaks in the scope written as %25, %3D, %0D and %0A, so the line reads back as that key.
auto hotkeySettingKey(const std::string& scope, const std::string& action) -> std::string;
// [narrower]'s stored actions over [broader]'s, each replacing the action's bindings whole.
auto overHotkeys(HotkeyMap broader, const HotkeyMap& narrower) -> HotkeyMap;
// What a console or game level stores for [changes] (bindingChanges) when what it inherits is [inherited] (shownHotkeys
// of the levels above it): an action whose bindings come out as it inherits them isn't stored (nullopt), so the level
// keeps only what it changes.
auto levelHotkeyChanges(const HotkeyMap& changes, const HotkeyMap& inherited)
    -> std::map<std::string, std::optional<HotkeyBindings>>;

}
