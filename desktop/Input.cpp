#include "Input.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace phobos::desktop {

static const std::pair<SDL_Scancode, int> keyboardPad[] = {
  {SDL_SCANCODE_UP, PadUp}, {SDL_SCANCODE_DOWN, PadDown}, {SDL_SCANCODE_LEFT, PadLeft}, {SDL_SCANCODE_RIGHT, PadRight},
  {SDL_SCANCODE_X, PadA}, {SDL_SCANCODE_Z, PadB}, {SDL_SCANCODE_S, PadX}, {SDL_SCANCODE_A, PadY},
  {SDL_SCANCODE_Q, PadL1}, {SDL_SCANCODE_W, PadR1}, {SDL_SCANCODE_1, PadL2}, {SDL_SCANCODE_3, PadR2},
  {SDL_SCANCODE_RETURN, PadStart}, {SDL_SCANCODE_RSHIFT, PadSelect},
};

static const std::pair<SDL_GamepadButton, int> padButtons[] = {
  {SDL_GAMEPAD_BUTTON_DPAD_UP, PadUp}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PadDown},
  {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PadLeft}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PadRight},
  {SDL_GAMEPAD_BUTTON_SOUTH, PadA}, {SDL_GAMEPAD_BUTTON_EAST, PadB},
  {SDL_GAMEPAD_BUTTON_WEST, PadX}, {SDL_GAMEPAD_BUTTON_NORTH, PadY},
  {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PadL1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PadR1},
  {SDL_GAMEPAD_BUTTON_LEFT_STICK, PadL3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PadR3},
  {SDL_GAMEPAD_BUTTON_BACK, PadSelect}, {SDL_GAMEPAD_BUTTON_START, PadStart},
  {SDL_GAMEPAD_BUTTON_GUIDE, PadHome},
};

auto Input::openConnected() -> void {
  int count = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
    for (int i = 0; i < count; i++) {
      if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) pads.push_back(pad);
    }
    SDL_free(ids);
  }
}

auto Input::handle(const SDL_Event& event) -> void {
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    for (auto [key, bit] : keyboardPad) {
      if (event.key.scancode == key) keyboardTaps |= bit;
    }
  } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    for (auto [button, bit] : padButtons) {
      if (event.gbutton.button == button) padTaps |= bit;
    }
  } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
    for (auto* pad : pads) {
      if (SDL_GetGamepadID(pad) == event.gdevice.which) return;
    }
    if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) pads.push_back(pad);
  } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    for (auto it = pads.begin(); it != pads.end(); ++it) {
      if (SDL_GetGamepadID(*it) == event.gdevice.which) {
        SDL_CloseGamepad(*it);
        pads.erase(it);
        break;
      }
    }
  }
}

// Stick and trigger directions press their bits past half deflection and release below 40%,
// so a noisy axis near the threshold can't chatter (the Android app's hysteresis).
static auto latch(int& state, int bit, float magnitude) -> bool {
  bool pressed = magnitude > ((state & bit) ? 0.4f : 0.5f);
  state = pressed ? (state | bit) : (state & ~bit);
  return pressed;
}

auto Input::poll(bool keyboardIsPad, bool latchSticks) -> PadState {
  PadState state;
  if (keyboardIsPad) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    for (auto [key, bit] : keyboardPad) {
      if (keys[key]) state.buttons |= bit;
    }
    state.buttons |= keyboardTaps;
    // I, J, K and L push the left stick all the way (the PSP's analog stick, the N64's, a DualShock's left one).
    state.lx = (float)keys[SDL_SCANCODE_L] - (float)keys[SDL_SCANCODE_J];
    state.ly = (float)keys[SDL_SCANCODE_K] - (float)keys[SDL_SCANCODE_I];
  }
  state.buttons |= padTaps;
  keyboardTaps = padTaps = 0;

  // A stick pushed from the keyboard holds unless a pad's is pushed as far.
  float strongest = std::max(std::abs(state.lx), std::abs(state.ly));
  float leftTrigger = 0.0f, rightTrigger = 0.0f;
  for (auto* pad : pads) {
    for (auto [button, bit] : padButtons) {
      if (SDL_GetGamepadButton(pad, button)) state.buttons |= bit;
    }
    auto axis = [&](SDL_GamepadAxis which) { return std::clamp(SDL_GetGamepadAxis(pad, which) / 32767.0f, -1.0f, 1.0f); };
    float lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
    float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    // With several pads connected, the one being moved drives the sticks.
    float deflection = std::max({std::abs(lx), std::abs(ly), std::abs(rx), std::abs(ry)});
    if (deflection >= strongest) {
      strongest = deflection;
      state.lx = lx; state.ly = ly; state.rx = rx; state.ry = ry;
    }
    leftTrigger = std::max(leftTrigger, axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    rightTrigger = std::max(rightTrigger, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
  }
  if (latch(stickBits, PadL2, leftTrigger)) state.buttons |= PadL2;
  if (latch(stickBits, PadR2, rightTrigger)) state.buttons |= PadR2;
  if (latchSticks) {
    if (latch(stickBits, PadLsUp, -state.ly)) state.buttons |= PadLsUp;
    if (latch(stickBits, PadLsDown, state.ly)) state.buttons |= PadLsDown;
    if (latch(stickBits, PadLsLeft, -state.lx)) state.buttons |= PadLsLeft;
    if (latch(stickBits, PadLsRight, state.lx)) state.buttons |= PadLsRight;
    if (latch(stickBits, PadRsUp, -state.ry)) state.buttons |= PadRsUp;
    if (latch(stickBits, PadRsDown, state.ry)) state.buttons |= PadRsDown;
    if (latch(stickBits, PadRsLeft, -state.rx)) state.buttons |= PadRsLeft;
    if (latch(stickBits, PadRsRight, state.rx)) state.buttons |= PadRsRight;
  }
  return state;
}

auto Input::rumble(bool on) -> void {
  if (!on && !rumbling) return;
  // Renewed every frame while the core holds the motor on; 100 ms covers a late frame.
  for (auto* pad : pads) SDL_RumbleGamepad(pad, on ? 0xC000 : 0, on ? 0xC000 : 0, on ? 100 : 0);
  rumbling = on;
}

auto Input::close() -> void {
  for (auto* pad : pads) SDL_CloseGamepad(pad);
  pads.clear();
}

auto isKeyboardComputer(const std::string& system) -> bool {
  return system == "ZX Spectrum" || system == "ZX Spectrum 128" || system == "MSX" || system == "MSX2";
}

// Key names from ares/spec/keyboard/keyboard.cpp.
static auto zxSpectrumLabels(SDL_Scancode key) -> std::vector<const char*> {
  static const char* letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                  "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
  static const char* digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
  if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) return {letters[key - SDL_SCANCODE_A]};
  if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) return {digits[key - SDL_SCANCODE_1]};
  switch (key) {
  case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return {"ENTER"};
  case SDL_SCANCODE_SPACE: return {"SPACE BREAK"};
  case SDL_SCANCODE_LSHIFT: return {"CAPS SHIFT"};
  case SDL_SCANCODE_RSHIFT: case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return {"SYMBOL SHIFT"};
  // The Spectrum's editing keys are CAPS SHIFT with a digit.
  case SDL_SCANCODE_BACKSPACE: return {"CAPS SHIFT", "0"};
  case SDL_SCANCODE_LEFT: return {"CAPS SHIFT", "5"};
  case SDL_SCANCODE_DOWN: return {"CAPS SHIFT", "6"};
  case SDL_SCANCODE_UP: return {"CAPS SHIFT", "7"};
  case SDL_SCANCODE_RIGHT: return {"CAPS SHIFT", "8"};
  default: return {};
  }
}

// Key names from ares/msx/keyboard/keyboard.cpp (the Japanese layout).
static auto msxLabels(SDL_Scancode key) -> std::vector<const char*> {
  static const char* letters[] = {
    "A ち", "B こ", "C そ", "D し", "E い ぃ", "F は", "G き", "H く", "I に", "J ま", "K の", "L り", "M も",
    "N み", "O ら", "P せ", "Q た", "R す", "S と", "T か", "U な", "V ひ", "W て", "X さ", "Y ん", "Z つ っ"};
  static const char* digits[] = {"1 ! ぬ", "2 \" ふ", "3 # あ ぁ", "4 $ う ぅ", "5 % え ぇ",
                                 "6 & お ぉ", "7 ’ や ゃ", "8 ( ゆ ゅ", "9 ) よ ょ", "0 わ を"};
  static const char* functions[] = {"F1 F6", "F2 F7", "F3 F8", "F4 F9", "F5 F10"};
  static const char* keypad[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
  if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) return {letters[key - SDL_SCANCODE_A]};
  if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) return {digits[key - SDL_SCANCODE_1]};
  if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F5) return {functions[key - SDL_SCANCODE_F1]};
  if (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_0) return {keypad[key - SDL_SCANCODE_KP_1]};
  switch (key) {
  case SDL_SCANCODE_MINUS: return {"- = ほ"};
  case SDL_SCANCODE_EQUALS: return {"^ ~ へ"};
  case SDL_SCANCODE_BACKSLASH: return {"¥ | ー"};
  case SDL_SCANCODE_LEFTBRACKET: return {"@ ‘ \""};
  case SDL_SCANCODE_RIGHTBRACKET: return {"[ { 。"};
  case SDL_SCANCODE_SEMICOLON: return {"; + れ"};
  case SDL_SCANCODE_APOSTROPHE: return {": * け"};
  case SDL_SCANCODE_NONUSHASH: return {"] } む"};
  case SDL_SCANCODE_COMMA: return {", < ね `"};
  case SDL_SCANCODE_PERIOD: return {". > る 。"};
  case SDL_SCANCODE_SLASH: return {"/ ? め ."};
  case SDL_SCANCODE_GRAVE: case SDL_SCANCODE_INTERNATIONAL1: return {"- ろ"};
  case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return {"SHIFT"};
  case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return {"CTRL"};
  case SDL_SCANCODE_LALT: return {"GRAPH"};
  case SDL_SCANCODE_RALT: return {"かな"};
  case SDL_SCANCODE_CAPSLOCK: return {"CAPS"};
  case SDL_SCANCODE_ESCAPE: return {"ESC"};
  case SDL_SCANCODE_TAB: return {"TAB"};
  case SDL_SCANCODE_BACKSPACE: return {"BS"};
  case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return {"RETURN"};
  case SDL_SCANCODE_SPACE: return {"SPACE"};
  case SDL_SCANCODE_HOME: return {"CLS/HOME"};
  case SDL_SCANCODE_INSERT: return {"INS"};
  case SDL_SCANCODE_DELETE: return {"DEL"};
  case SDL_SCANCODE_END: return {"STOP"};
  case SDL_SCANCODE_PAGEDOWN: return {"SELECT"};
  case SDL_SCANCODE_LEFT: return {"←"};
  case SDL_SCANCODE_UP: return {"↑"};
  case SDL_SCANCODE_DOWN: return {"↓"};
  case SDL_SCANCODE_RIGHT: return {"→"};
  case SDL_SCANCODE_KP_MULTIPLY: return {"*"};
  case SDL_SCANCODE_KP_PLUS: return {"+"};
  case SDL_SCANCODE_KP_DIVIDE: return {"/"};
  case SDL_SCANCODE_KP_MINUS: return {"-"};
  case SDL_SCANCODE_KP_COMMA: return {","};
  case SDL_SCANCODE_KP_PERIOD: return {"."};
  default: return {};
  }
}

auto keyboardLabels(const std::string& system, SDL_Scancode key) -> std::vector<const char*> {
  if (system == "ZX Spectrum" || system == "ZX Spectrum 128") return zxSpectrumLabels(key);
  if (system == "MSX" || system == "MSX2") return msxLabels(key);
  return {};
}

}
