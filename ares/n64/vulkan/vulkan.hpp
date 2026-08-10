#include "rdp_device.hpp"
#include <mutex>

namespace ares::Nintendo64 {

struct Vulkan {
  auto load(Node::Object) -> bool;
  auto unload() -> void;

  auto render() -> bool;
  auto frame() -> void;
  auto writeWord(u32 address, u32 data) -> void;
  auto scanoutAsync(bool field) -> bool;
  auto mapScanoutRead(const u8*& rgba, u32& width, u32& height) -> void;
  auto unmapScanoutRead() -> void;
  auto endScanout() -> void;
  auto crashed() -> const char*;

  struct Implementation;
  Implementation* implementation = nullptr;

  // Serializes access to `implementation` (and its scanout buffer) between the
  // emulation thread (load/unload/render/scanoutAsync/...) and the video screen
  // thread (VI::refresh, platform->video).
  //
  // mapScanoutRead() acquires this lock and holds it until unmapScanoutRead(),
  // so the implementation (and its mapped scanout buffer) cannot be destroyed
  // by a concurrent reset (System::power -> vulkan.unload/load) while the
  // screen thread is copying pixels out of it.
  std::mutex mutex;
  std::unique_lock<std::mutex> scanoutLock;

  bool enable = true;
  bool disableVideoInterfaceProcessing = false;
  bool weaveDeinterlacing = false;
  u32  internalUpscale = 1;  //1, 2, 4, 8
  bool supersampleScanout = false;
  u32  outputUpscale = supersampleScanout ? 1 : internalUpscale;
};

extern Vulkan vulkan;

}
