#include "Platform.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace phobos::desktop {

auto attachParentConsole() -> void {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  std::freopen("CONOUT$", "w", stdout);
  std::freopen("CONOUT$", "w", stderr);
}

}
