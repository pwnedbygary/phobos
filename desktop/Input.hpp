#pragma once
#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace phobos::desktop {

// The runner's controller bits (PhobosCore.Input on Android): A is the bottom face button,
// B the right one, X the left one and Y the top one, as on an Xbox controller.
enum : int {
  PadUp = 1 << 0, PadDown = 1 << 1, PadLeft = 1 << 2, PadRight = 1 << 3,
  PadA = 1 << 4, PadB = 1 << 5, PadX = 1 << 6, PadY = 1 << 7,
  PadL1 = 1 << 8, PadR1 = 1 << 9, PadL2 = 1 << 10, PadR2 = 1 << 11,
  PadL3 = 1 << 12, PadR3 = 1 << 13, PadSelect = 1 << 14, PadStart = 1 << 15, PadHome = 1 << 16,
  PadLsUp = 1 << 17, PadLsDown = 1 << 18, PadLsLeft = 1 << 19, PadLsRight = 1 << 20,
  PadRsUp = 1 << 21, PadRsDown = 1 << 22, PadRsLeft = 1 << 23, PadRsRight = 1 << 24,
};

struct PadState {
  int buttons = 0;
  // -1 to 1, down and right positive, raw: the cores apply their own dead zones.
  float lx = 0, ly = 0, rx = 0, ry = 0;
};

// Every connected gamepad and the keyboard drive player 1, as one controller.
struct Input {
  auto openConnected() -> void;
  auto handle(const SDL_Event& event) -> void;
  // keyboardIsPad: the keyboard plays the controller (false while a ZX Spectrum or MSX keyboard
  // has the keys). latchSticks: stick directions also press their bits (the N64 reads its C
  // buttons from the right stick).
  auto poll(bool keyboardIsPad, bool latchSticks) -> PadState;
  auto rumble(bool on) -> void;
  auto close() -> void;

private:
  std::vector<SDL_Gamepad*> pads;
  int stickBits = 0;
  bool rumbling = false;
  // Buttons pressed since the last poll. A tap shorter than a frame is over before poll() reads
  // the held state, so its press is kept for one poll.
  int keyboardTaps = 0;
  int padTaps = 0;
};

// The core's labels for a host key on the ZX Spectrum or MSX keyboard; empty when the key
// has no counterpart there. Some keys press two (Backspace is CAPS SHIFT + 0 on a Spectrum).
auto keyboardLabels(const std::string& system, SDL_Scancode key) -> std::vector<const char*>;

auto isKeyboardComputer(const std::string& system) -> bool;

}
