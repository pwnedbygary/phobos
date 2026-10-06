#pragma once
#include <filesystem>
#include <string>

namespace phobos::desktop {

// The shell keeps paths as UTF-8 strings, as the runner and SDL take them. These convert
// without going through the Windows ANSI code page.
inline auto toPath(const std::string& utf8) -> std::filesystem::path {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline auto fromPath(const std::filesystem::path& path) -> std::string {
  auto utf8 = path.u8string();
  return std::string(utf8.begin(), utf8.end());
}

#if defined(_WIN32)
// Phobos.exe is a GUI program; started from a console, its log goes to that console.
auto attachParentConsole() -> void;
#else
inline auto attachParentConsole() -> void {}
#endif

}
