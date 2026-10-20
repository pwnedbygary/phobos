#pragma once
#include <map>
#include <string>
#include <vector>

namespace phobos::desktop {

// Settings kept as key=value lines in settings.ini in the user's data folder
// (XDG data home on Linux, %APPDATA% on Windows, Application Support on macOS).
struct Settings {
  auto load(const std::string& file) -> void;
  // Whether the file has [key], empty or not.
  auto has(const std::string& key) const -> bool;
  auto text(const std::string& key, const std::string& fallback = "") const -> std::string;
  auto flag(const std::string& key, bool fallback) const -> bool;
  auto number(const std::string& key, int fallback) const -> int;
  // Each setter writes the file.
  auto setText(const std::string& key, const std::string& value) -> void;
  auto setFlag(const std::string& key, bool value) -> void;
  auto setNumber(const std::string& key, int value) -> void;
  // Takes [keys] out of the file, so each one's fallback applies again.
  auto erase(const std::vector<std::string>& keys) -> void;

private:
  auto save() const -> void;
  std::string file;
  std::map<std::string, std::string> values;
};

}
