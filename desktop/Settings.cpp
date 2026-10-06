#include "Settings.hpp"
#include "Platform.hpp"

#include <fstream>

namespace phobos::desktop {

auto Settings::load(const std::string& path) -> void {
  file = path;
  values.clear();
  std::ifstream in(toPath(file));
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto equals = line.find('=');
    if (equals == std::string::npos || line[0] == '#') continue;
    values[line.substr(0, equals)] = line.substr(equals + 1);
  }
}

auto Settings::text(const std::string& key, const std::string& fallback) const -> std::string {
  auto it = values.find(key);
  return it == values.end() ? fallback : it->second;
}

auto Settings::flag(const std::string& key, bool fallback) const -> bool {
  auto it = values.find(key);
  if (it == values.end()) return fallback;
  return it->second == "1" || it->second == "true";
}

auto Settings::number(const std::string& key, int fallback) const -> int {
  auto it = values.find(key);
  if (it == values.end()) return fallback;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return fallback;
  }
}

auto Settings::setText(const std::string& key, const std::string& value) -> void {
  values[key] = value;
  save();
}

auto Settings::setFlag(const std::string& key, bool value) -> void { setText(key, value ? "1" : "0"); }

auto Settings::setNumber(const std::string& key, int value) -> void { setText(key, std::to_string(value)); }

auto Settings::save() const -> void {
  if (file.empty()) return;
  std::ofstream out(toPath(file), std::ios::trunc);
  for (auto& [key, value] : values) out << key << '=' << value << '\n';
}

}
