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
  //Presenting (docs/psp-gpu-renderers.md, "Presenting"): the host's window (an ANativeWindow on Android; none to
  //take it back) that the Vulkan renderer shows the screen on itself, straight from the GPU with nothing read back.
  //Any thread, any time: the core keeps a reference of its own while it has it. presenting(): whether the core shows
  //the frames on the window now, so the host mustn't (nor lock the window, which would take it from the core); any
  //thread. shot(): the picture presented last, at the PSP's size (8888, red in the low byte), for screenshots.
  auto window(void* window) -> void;
  auto presenting() -> bool;
  auto shot(std::vector<u32>& pixels, u32& width, u32& height) -> bool;
  //The GE on a thread of its own (option "GE Thread"), stuck: an unload found it still busy after two seconds (a
  //driver call that never returned), and left it be with the machine under it. wedged(): whether one is; unwedge():
  //waits up to milliseconds for it to come back, then finishes the unload (true: none left). Loads are refused while
  //one is (load() is false): a front end with threads of its own stuck the same way asks for a restart.
  auto wedged() -> bool;
  auto unwedge(u32 milliseconds) -> bool;
}

#include <psp/cpu/allegrex.hpp>
#include <psp/memory/memory.hpp>
#include <psp/ge/ge.hpp>
#include <psp/ge/gpu/gpu.hpp>
#include <psp/kernel/kernel.hpp>

namespace ares::PlayStationPortable {
  #include <psp/system/system.hpp>

  //The system's dialogs shown by the host (docs/psp-core.md, part 73), where it set the option "Dialogs" before
  //loading. dialogID(): the number of the request outstanding (0: none), cheap enough to ask every frame; dialog():
  //the request (Kernel::DialogRequest: what to show); answer(): the player's answer to it, false if it answers no
  //request outstanding (taken down, made afresh, or answered already) or isn't one of the request's choices. The
  //dialog runs, the game with it, until it's answered. dialogBytes() and answer(bytes): the same in the byte form
  //Kernel::dialogBytes() and dialogAnswerFrom() describe (no bytes: none outstanding), for a host in another language.
  //Any thread.
  auto dialogID() -> u32;
  auto dialog() -> Kernel::DialogRequest;
  auto answer(const Kernel::DialogAnswer& answer) -> bool;
  auto dialogBytes() -> std::vector<u8>;
  auto answer(const std::vector<u8>& bytes) -> bool;
}
