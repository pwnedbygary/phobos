#pragma once
//started: 2026-10-03

#include <ares/ares.hpp>
#include <nall/bump-allocator.hpp>
#include <sljit.h>
#include <nall/recompiler/generic/generic.hpp>  //the CPU's recompiler is built on it

//A disc image in MAME's CHD form is read with nall's reader of them (through libchdr), in builds that have it.
//libchdr's MIN and MAX are macros, and the Allegrex has instructions by those names.
#if defined(ARES_ENABLE_CHD)
  #include <nall/decode/chd.hpp>
#endif
#undef MIN
#undef MAX

//The PlayStation Portable (Sony, 2004), as an ares core: what Phobos's front ends load, list and run like any other
//system. The parts underneath (the Allegrex CPU, the memory map, the GE, the HLE kernel) are in their own folders and
//don't know about ares; system/ puts them together under ares's node tree. docs/psp-core.md describes it all.
namespace ares::PlayStationPortable {
  auto enumerate() -> std::vector<string>;
  auto load(Node::System& node, string name) -> bool;
  auto option(string name, string value) -> bool;
  //The host's vkGetInstanceProcAddr, for the Vulkan renderer: the loader or the driver the host has loaded (a custom
  //one included), none for the system's own. Set before loading.
  auto vulkanLoader(void* getInstanceProcAddr) -> void;
  //What the owner should be told, once (the hardware renderer couldn't start, or stopped, and the software renderer
  //draws): taken, so the next call has nothing until there's more. Any thread may ask.
  auto notice() -> string;
}

#include <psp/cpu/allegrex.hpp>
#include <psp/memory/memory.hpp>
#include <psp/ge/ge.hpp>
#include <psp/ge/gpu/gpu.hpp>
#include <psp/kernel/kernel.hpp>

namespace ares::PlayStationPortable {
  #include <psp/system/system.hpp>
}
