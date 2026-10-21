#include "Hotkeys.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>

namespace phobos::desktop {

namespace {

template<typename T> auto contains(const std::vector<T>& list, const T& item) -> bool {
  return std::find(list.begin(), list.end(), item) != list.end();
}

auto addOnce(std::vector<std::string>& names, const std::string& name) -> void {
  if (!contains(names, name)) names.push_back(name);
}

// Every bound combo with its action: each action's bindings in slot order, the empty slots left out.
auto bound(const HotkeyMap& hotkeys) -> std::vector<std::pair<std::string, HotkeyCombo>> {
  std::vector<std::pair<std::string, HotkeyCombo>> combos;
  for (auto& [action, bindings] : hotkeys) {
    for (auto& combo : bindings) {
      if (!combo.empty()) combos.emplace_back(action, combo);
    }
  }
  return combos;
}

auto sameSet(const HotkeyCombo& a, const HotkeyCombo& b) -> bool {
  return std::set<HotkeyToken>(a.begin(), a.end()) == std::set<HotkeyToken>(b.begin(), b.end());
}

auto includes(const HotkeyCombo& outer, const HotkeyCombo& inner) -> bool {
  return std::all_of(inner.begin(), inner.end(), [&](HotkeyToken token) { return contains(outer, token); });
}

// Whether [token] is one of [combo]'s keys after its first.
auto later(const HotkeyCombo& combo, HotkeyToken token) -> bool {
  return combo.size() > 1 && std::find(combo.begin() + 1, combo.end(), token) != combo.end();
}

// A stick direction or a mouse button: it counts only where a binding uses it (HotkeyTracker). A trigger is the game's
// L2 or R2 button, and counts as a button does.
auto countsWhereBound(HotkeyToken token) -> bool {
  if (tokenKind(token) == HotkeyKind::Mouse) return true;
  return tokenKind(token) == HotkeyKind::Axis && tokenAxis(token) != SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
         tokenAxis(token) != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
}

auto tapAndCombo(const std::string& tapAction, const HotkeyCombo& tap, const HotkeyCombo& longer) -> bool {
  return tap.size() == 1 && !isHotkeyHold(tapAction) && longer.size() > 1 && longer[0] == tap[0];
}

auto andList(const std::vector<std::string>& names) -> std::string {
  std::string list;
  for (size_t i = 0; i < names.size(); i++) {
    if (i) list += i + 1 == names.size() ? " and " : ", ";
    list += names[i];
  }
  return list;
}

auto parseToken(const std::string& text) -> std::optional<HotkeyToken> {
  auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  auto token = text.substr(first, text.find_last_not_of(' ') - first + 1);
  if (token.size() < 2) return {};
  char kind = token[0];
  auto digits = token.substr(1);
  bool positive = true;
  if (kind == 'a') {
    if (digits.back() != '+' && digits.back() != '-') return {};
    positive = digits.back() == '+';
    digits.pop_back();
  }
  // At most six digits, so an axis's doubled code still fits below the kind's byte.
  if (digits.empty() || digits.size() > 6) return {};
  if (!std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
  int code = std::stoi(digits);
  switch (kind) {
  case 'k': return keyToken(code);
  case 'b': return buttonToken(code);
  case 'a': return axisToken(code, positive);
  case 'm': return mouseToken(code);
  default: return {};
  }
}

}

auto hotkeyActions() -> const std::vector<HotkeyAction>& {
  static const std::vector<HotkeyAction> actions = [] {
    std::vector<HotkeyAction> list = {
      {Hotkey::pause, "Pause menu"},
      {Hotkey::pauseWindow, "Fullscreen and pause (toggle)"},
      {Hotkey::save, "Save state"},
      {Hotkey::load, "Load state"},
      {Hotkey::nextSlot, "Next state slot"},
      {Hotkey::previousSlot, "Previous state slot"},
      {Hotkey::screenshot, "Screenshot"},
      {Hotkey::fullscreen, "Fullscreen"},
      {Hotkey::fastForwardHold, "Fast forward (hold)"},
      {Hotkey::fastForwardToggle, "Fast forward (toggle)"},
      {Hotkey::slowMotion, "Slow motion (hold)"},
      {Hotkey::frameAdvance, "Frame advance"},
      {Hotkey::mute, "Mute"},
      {Hotkey::reset, "Reset"},
      {Hotkey::reload, "Reload game"},
      {Hotkey::quit, "Quit game"},
      {Hotkey::n64Pak, "N64 controller pak"},
      {Hotkey::disc, "Next disc"},
      {Hotkey::ps1Analog, "PS1 analog controller"},
    };
    for (int slot = 1; slot <= 9; slot++) {
      list.push_back({Hotkey::saveSlotPrefix + std::to_string(slot), "Save state " + std::to_string(slot)});
    }
    for (int slot = 1; slot <= 9; slot++) {
      list.push_back({Hotkey::loadSlotPrefix + std::to_string(slot), "Load state " + std::to_string(slot)});
    }
    return list;
  }();
  return actions;
}

auto hotkeyLabel(const std::string& action) -> std::string {
  for (auto& entry : hotkeyActions()) {
    if (entry.name == action) return entry.label;
  }
  return action;
}

auto directSlot(const std::string& action) -> int {
  for (std::string prefix : {Hotkey::saveSlotPrefix, Hotkey::loadSlotPrefix}) {
    if (action.size() == prefix.size() + 1 && action.compare(0, prefix.size(), prefix) == 0) {
      char digit = action.back();
      if (digit >= '1' && digit <= '9') return digit - '0';
    }
  }
  return 0;
}

auto isHotkeyHold(const std::string& action) -> bool {
  return action == Hotkey::fastForwardHold || action == Hotkey::slowMotion || action == Hotkey::frameAdvance;
}

auto withHotkeyBinding(HotkeyBindings bindings, int slot, const HotkeyCombo& combo) -> HotkeyBindings {
  if (slot < 0 || slot >= hotkeyBindingLimit) return bindings;
  if ((int)bindings.size() <= slot) bindings.resize(slot + 1);
  bindings[slot] = combo;
  while (!bindings.empty() && bindings.back().empty()) bindings.pop_back();
  return bindings;
}

auto encodeHotkeyBindings(const HotkeyBindings& bindings) -> std::string {
  size_t count = bindings.size();
  while (count && bindings[count - 1].empty()) count--;
  std::string text;
  for (size_t slot = 0; slot < count; slot++) {
    if (slot) text += ';';
    for (size_t i = 0; i < bindings[slot].size(); i++) {
      if (i) text += ',';
      auto token = bindings[slot][i];
      switch (tokenKind(token)) {
      case HotkeyKind::Key: text += 'k' + std::to_string(tokenCode(token)); break;
      case HotkeyKind::Button: text += 'b' + std::to_string(tokenCode(token)); break;
      case HotkeyKind::Axis: text += 'a' + std::to_string(tokenAxis(token)) + (tokenPositive(token) ? '+' : '-'); break;
      case HotkeyKind::Mouse: text += 'm' + std::to_string(tokenCode(token)); break;
      }
    }
  }
  return text;
}

auto parseHotkeyBindings(const std::string& value) -> HotkeyBindings {
  HotkeyBindings bindings;
  size_t start = 0;
  while (start <= value.size() && (int)bindings.size() < hotkeyBindingLimit) {
    auto end = std::min(value.find(';', start), value.size());
    HotkeyCombo combo;
    for (size_t at = start; at < end;) {
      auto comma = std::min(value.find(',', at), end);
      auto token = parseToken(value.substr(at, comma - at));
      if (token && !contains(combo, *token)) combo.push_back(*token);
      at = comma + 1;
    }
    bindings.push_back(combo);
    start = end + 1;
  }
  while (!bindings.empty() && bindings.back().empty()) bindings.pop_back();
  return bindings;
}

// Guide (Home) leads every controller hotkey, so Select, Start and every other button stay the game's: tapped alone,
// Guide opens the pause menu; held with one other button, it runs that button's action (Quit's asks first). The
// keyboard's keys are the desktop's long-standing ones: F12 and Escape for the menu, F5 to save, F9 to load, F8 for a
// screenshot, F11 for fullscreen, Tab to hold fast forward. Reset, fullscreen and pause together, the next disc and
// the direct slots are unbound: the menu has their like, and a player can bind them on purpose.
auto defaultHotkeys() -> const HotkeyMap& {
  static const HotkeyMap defaults = [] {
    auto guide = buttonToken(SDL_GAMEPAD_BUTTON_GUIDE);
    auto with = [&](SDL_GamepadButton button) { return HotkeyCombo{guide, buttonToken(button)}; };
    auto key = [](SDL_Scancode scancode) { return HotkeyCombo{keyToken(scancode)}; };
    return HotkeyMap{
      {Hotkey::pause, {{guide}, key(SDL_SCANCODE_F12), key(SDL_SCANCODE_ESCAPE)}},
      {Hotkey::save, {with(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), key(SDL_SCANCODE_F5)}},
      {Hotkey::load, {with(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), key(SDL_SCANCODE_F9)}},
      {Hotkey::screenshot, {with(SDL_GAMEPAD_BUTTON_RIGHT_STICK), key(SDL_SCANCODE_F8)}},
      {Hotkey::fullscreen, {key(SDL_SCANCODE_F11)}},
      {Hotkey::fastForwardHold, {key(SDL_SCANCODE_TAB)}},
      {Hotkey::fastForwardToggle, {{guide, axisToken(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, true)}}},
      {Hotkey::slowMotion, {{guide, axisToken(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true)}}},
      {Hotkey::nextSlot, {with(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)}},
      {Hotkey::previousSlot, {with(SDL_GAMEPAD_BUTTON_DPAD_LEFT)}},
      {Hotkey::frameAdvance, {with(SDL_GAMEPAD_BUTTON_START)}},
      {Hotkey::n64Pak, {with(SDL_GAMEPAD_BUTTON_DPAD_DOWN)}},
      {Hotkey::ps1Analog, {with(SDL_GAMEPAD_BUTTON_DPAD_UP)}},
      {Hotkey::mute, {with(SDL_GAMEPAD_BUTTON_SOUTH)}},
      {Hotkey::reload, {with(SDL_GAMEPAD_BUTTON_WEST)}},
      {Hotkey::quit, {with(SDL_GAMEPAD_BUTTON_EAST)}},
    };
  }();
  return defaults;
}

auto shownHotkeys(const HotkeyMap& stored) -> HotkeyMap {
  HotkeyMap shown;
  for (auto& action : hotkeyActions()) {
    auto own = stored.find(action.name);
    auto& source = own != stored.end() ? stored : defaultHotkeys();
    auto bindings = source.find(action.name);
    if (bindings == source.end()) continue;
    if (std::any_of(bindings->second.begin(), bindings->second.end(), [](auto& combo) { return !combo.empty(); })) {
      shown[action.name] = bindings->second;
    }
  }
  return shown;
}

auto resolveHotkeys(const HotkeyMap& stored, const std::function<bool(HotkeyToken)>& gameKey) -> HotkeyMap {
  auto hotkeys = shownHotkeys(stored);
  if (!gameKey) return hotkeys;
  for (auto& [action, defaults] : defaultHotkeys()) {
    auto it = hotkeys.find(action);
    if (it == hotkeys.end()) continue;
    auto& bindings = it->second;
    for (auto& combo : bindings) {
      if (contains(defaults, combo) && std::any_of(combo.begin(), combo.end(), gameKey)) combo.clear();
    }
    while (!bindings.empty() && bindings.back().empty()) bindings.pop_back();
    if (bindings.empty()) hotkeys.erase(it);
  }
  return hotkeys;
}

auto isTapCombo(const HotkeyMap& hotkeys, const HotkeyCombo& combo) -> bool {
  if (combo.size() != 1) return false;
  for (auto& [action, other] : bound(hotkeys)) {
    if (other.size() > 1 && other[0] == combo[0] && !(isHotkeyHold(action) && contains(hotkeys.at(action), combo))) {
      return true;
    }
  }
  return false;
}

auto HotkeyTracker::reset() -> void {
  order.clear();
  heldBack.clear();
  tapCandidate.reset();
  tapped.reset();
  pressedSinceLook = false;
}

auto HotkeyTracker::press(const HotkeyMap& hotkeys, HotkeyToken token) -> bool {
  if (countsWhereBound(token)) {
    auto combos = bound(hotkeys);
    if (std::none_of(combos.begin(), combos.end(), [&](auto& entry) { return contains(entry.second, token); })) {
      return false;
    }
  }
  if (contains(order, token)) return taken(token);
  tapped.reset();
  tapCandidate = order.empty() ? std::optional<HotkeyToken>(token) : std::nullopt;
  order.push_back(token);
  bool take = takenOnPress(hotkeys, token);
  if (take) heldBack.insert(token);
  pressedSinceLook = true;
  return take;
}

auto HotkeyTracker::release(HotkeyToken token) -> bool {
  auto at = std::find(order.begin(), order.end(), token);
  if (at == order.end()) return false;
  order.erase(at);
  bool wasTaken = heldBack.erase(token) > 0;
  if (tapCandidate == token) {
    tapped = token;
    tapCandidate.reset();
  }
  return wasTaken;
}

auto HotkeyTracker::completions(const HotkeyMap& hotkeys) -> std::vector<std::string> {
  if (!pressedSinceLook) return {};
  pressedSinceLook = false;
  return matches(hotkeys);
}

auto HotkeyTracker::matches(const HotkeyMap& hotkeys) const -> std::vector<std::string> {
  auto position = [&](HotkeyToken token) { return std::find(order.begin(), order.end(), token) - order.begin(); };
  std::vector<std::string> actions;
  for (auto& [action, combo] : bound(hotkeys)) {
    if (!sameSet(combo, order) || isTapCombo(hotkeys, combo)) continue;
    auto first = position(combo[0]);
    if (std::all_of(combo.begin() + 1, combo.end(), [&](HotkeyToken token) { return position(token) > first; })) {
      addOnce(actions, action);
    }
  }
  return actions;
}

auto HotkeyTracker::taps(const HotkeyMap& hotkeys) -> std::vector<std::string> {
  if (!tapped) return {};
  HotkeyCombo key{*tapped};
  tapped.reset();
  std::vector<std::string> actions;
  for (auto& [action, combo] : bound(hotkeys)) {
    if (combo == key && isTapCombo(hotkeys, combo)) addOnce(actions, action);
  }
  return actions;
}

auto HotkeyTracker::holding(const HotkeyCombo& combo) const -> bool {
  return !combo.empty() && includes(order, combo);
}

auto HotkeyTracker::taken(HotkeyToken token) const -> bool { return heldBack.count(token) > 0; }

auto HotkeyTracker::takenOnPress(const HotkeyMap& hotkeys, HotkeyToken token) const -> bool {
  auto combos = bound(hotkeys);
  // A combo's first key is always its own, never the game's.
  if (std::any_of(combos.begin(), combos.end(), [&](auto& entry) { return entry.second[0] == token; })) return true;
  auto at = std::find(order.begin(), order.end(), token);
  return std::any_of(combos.begin(), combos.end(), [&](auto& entry) {
    auto first = std::find(order.begin(), order.end(), entry.second[0]);
    return later(entry.second, token) && first != order.end() && first < at;
  });
}

auto HotkeyHolders::press(HotkeyToken token, std::uint64_t device) -> bool {
  auto& holders = devices[token];
  bool first = holders.empty();
  holders.insert(device);
  return first;
}

auto HotkeyHolders::release(HotkeyToken token, std::uint64_t device) -> bool {
  auto it = devices.find(token);
  if (it == devices.end() || !it->second.erase(device) || !it->second.empty()) return false;
  devices.erase(it);
  return true;
}

auto HotkeyHolders::remove(std::uint64_t device) -> std::vector<HotkeyToken> {
  std::vector<HotkeyToken> letGo;
  for (auto it = devices.begin(); it != devices.end();) {
    if (it->second.erase(device) && it->second.empty()) {
      letGo.push_back(it->first);
      it = devices.erase(it);
    } else {
      ++it;
    }
  }
  return letGo;
}

auto latchAxis(AxisDirections held, float value) -> AxisDirections {
  return {-value > (held.negative ? 0.4f : 0.5f), value > (held.positive ? 0.4f : 0.5f)};
}

auto hotkeyConflicts(const HotkeyMap& hotkeys, const std::string& action, const HotkeyCombo& combo)
    -> std::vector<HotkeyConflict> {
  std::vector<HotkeyConflict> conflicts;
  if (combo.empty()) return conflicts;
  for (auto& [otherAction, bindings] : hotkeys) {
    if (otherAction == action) continue;
    for (int slot = 0; slot < (int)bindings.size(); slot++) {
      auto& other = bindings[slot];
      if (other.empty()) continue;
      std::optional<HotkeyConflict::Kind> kind;
      if (sameSet(combo, other)) kind = HotkeyConflict::Kind::Exact;
      else if (tapAndCombo(action, combo, other) || tapAndCombo(otherAction, other, combo)) kind.reset();
      else if (includes(combo, other) || includes(other, combo)) kind = HotkeyConflict::Kind::Overlap;
      else if (later(other, combo[0]) || later(combo, other[0])) kind = HotkeyConflict::Kind::CrossRole;
      if (kind) conflicts.push_back({otherAction, slot, *kind});
    }
  }
  return conflicts;
}

auto hotkeyConflictMessage(const std::vector<HotkeyConflict>& conflicts) -> std::string {
  std::vector<std::string> exact, overlapping;
  for (auto& conflict : conflicts) {
    if (conflict.kind == HotkeyConflict::Kind::Exact) addOnce(exact, hotkeyLabel(conflict.action));
  }
  for (auto& conflict : conflicts) {
    auto name = hotkeyLabel(conflict.action);
    if (!contains(exact, name)) addOnce(overlapping, name);
  }
  if (exact.empty()) return "The selected hotkey combo overlaps " + andList(overlapping) + ", would you like to overwrite?";
  auto question = "The selected hotkey combo is already bound to " + andList(exact) + ", would you like to overwrite?";
  if (overlapping.empty()) return question;
  auto lost = std::count_if(conflicts.begin(), conflicts.end(), [&](auto& conflict) {
    return contains(overlapping, hotkeyLabel(conflict.action));
  });
  return question + " It also overlaps " + andList(overlapping) + ", which would lose " +
         (lost > 1 ? "those bindings" : "that binding") + " too.";
}

auto existingHotkeyConflicts(const HotkeyMap& hotkeys) -> std::vector<std::pair<std::string, std::string>> {
  std::vector<std::string> actions;
  for (auto& [action, combo] : bound(hotkeys)) addOnce(actions, action);
  std::vector<std::pair<std::string, std::string>> pairs;
  for (size_t index = 0; index < actions.size(); index++) {
    std::vector<std::string> others;
    for (auto& combo : hotkeys.at(actions[index])) {
      for (auto& conflict : hotkeyConflicts(hotkeys, actions[index], combo)) addOnce(others, conflict.action);
    }
    for (auto& other : others) {
      if ((size_t)(std::find(actions.begin(), actions.end(), other) - actions.begin()) > index) {
        pairs.emplace_back(actions[index], other);
      }
    }
  }
  return pairs;
}

auto bindingChanges(const HotkeyMap& shown, const std::string& action, int slot, const HotkeyCombo& combo,
                    const std::vector<std::pair<std::string, int>>& overwrite) -> HotkeyMap {
  HotkeyMap changes;
  auto bindingsOf = [&](const std::string& name) -> HotkeyBindings {
    if (auto it = changes.find(name); it != changes.end()) return it->second;
    if (auto it = shown.find(name); it != shown.end()) return it->second;
    return {};
  };
  auto bindings = withHotkeyBinding(bindingsOf(action), slot, combo);
  changes[action] = bindings;
  for (auto& [other, otherSlot] : overwrite) {
    auto cleared = withHotkeyBinding(bindingsOf(other), otherSlot, {});
    changes[other] = cleared;
  }
  return changes;
}

auto hotkeySettingKey(const std::string& scope, const std::string& action) -> std::string {
  if (scope.empty()) return "hotkey." + action;
  std::string escaped;
  for (char c : scope) {
    switch (c) {
    case '%': escaped += "%25"; break;
    case '=': escaped += "%3D"; break;
    case '\r': escaped += "%0D"; break;
    case '\n': escaped += "%0A"; break;
    default: escaped += c; break;
    }
  }
  return "hotkey[" + escaped + "]." + action;
}

auto overHotkeys(HotkeyMap broader, const HotkeyMap& narrower) -> HotkeyMap {
  for (auto& [action, bindings] : narrower) broader[action] = bindings;
  return broader;
}

auto levelHotkeyChanges(const HotkeyMap& changes, const HotkeyMap& inherited)
    -> std::map<std::string, std::optional<HotkeyBindings>> {
  std::map<std::string, std::optional<HotkeyBindings>> stored;
  for (auto& [action, bindings] : changes) {
    auto it = inherited.find(action);
    bool asInherited = bindings == (it == inherited.end() ? HotkeyBindings{} : it->second);
    stored[action] = asInherited ? std::nullopt : std::optional<HotkeyBindings>(bindings);
  }
  return stored;
}

}
