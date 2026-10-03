//What the cores get from ares.hpp that the Allegrex uses: nall, and ares's integer types (u32, n32 and friends).
//The tests include this instead of all of ares, so they build in seconds without ares's node system.
#pragma once

#include <nall/nall.hpp>
using namespace nall;
#include <ares/types.hpp>
