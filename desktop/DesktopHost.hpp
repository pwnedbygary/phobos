#pragma once
#include <cstdint>
#include <vector>

namespace phobos::host {

// The runner presents on its video thread; the shell draws on the main thread. This copies the
// newest presented frame (RGBA bytes) when it is newer than `serial`, and updates `serial`.
auto takeFrame(std::uint64_t& serial, std::vector<std::uint32_t>& pixels, std::uint32_t& width, std::uint32_t& height) -> bool;

}
