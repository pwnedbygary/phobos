//What the cores get from ares.hpp that the Allegrex uses: nall, ares's integer types (u32, n32 and friends), and
//sljit with nall's recompiler on top of it. The tests include this instead of all of ares, so they build in seconds
//without ares's node system.
#pragma once

#include <nall/nall.hpp>
#include <nall/bump-allocator.hpp>
using namespace nall;
#include <ares/types.hpp>
#include <sljit.h>
#include <nall/recompiler/generic/generic.hpp>
