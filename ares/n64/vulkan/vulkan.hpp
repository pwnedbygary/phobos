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
  auto setSkipIdleOnDestroy(bool skip) -> void;
  // Called from VI::power() on reset: drops the stale scanout fence from
  // the frame that was in flight when the reset fired, so the first
  // scanoutAsync/mapScanoutRead after reset does not wait forever on a
  // fence Turnip may never signal.
  auto resetScanoutFence() -> void;

  struct Implementation;
  Implementation* implementation = nullptr;

  // Pipeline cache persists across Vulkan unload/load cycles. On unload(),
  // device.get_pipeline_cache_data() captures all compiled shader variants.
  // On next load(), the cache is injected via device.init_pipeline_cache()
  // so the driver can reuse cached binaries — eliminating compilation stutter.
  static std::vector<uint8_t> pipelineCacheData;
  static string pipelineCachePath;

  // Incremented each time the GPU driver fails to create a compute/render
  // pipeline. The UI polls this to show a "try a Turnip driver" suggestion
  // when the system Adreno driver can't handle paraLLEl-RDP shaders.
  static std::atomic<int> pipelineFailureCount;

  // GPU device name from VkPhysicalDeviceProperties::deviceName.
  // e.g. "Adreno (TM) 740" for stock, "Turnip" for community driver.
  // Used by the UI to suppress the Turnip suggestion when a
  // community driver is already in use.
  static string gpuDeviceName;

  // Set to true by System::power() / PhobosRunner::resetSystem() to skip
  // persisting the pipeline cache during reset. Turnip Mesa handles
  // VkPipelineCache reuse across successive VkDevice instances poorly.
  // Atomic so the JNI thread can set it safely while the emulation thread
  // reads it inside System::power() -> vulkan.unload().
  static std::atomic<bool> skipCachePersist;

  // Set to true by System::power() / PhobosRunner::resetSystem() to discard
  // the in-memory pipeline cache before vulkan.load(). Reusing cache data
  // across VkDevice instances causes Turnip to silently produce broken GPU
  // fences that never signal, deadlocking the emulation thread.
  static std::atomic<bool> discardPipelineCache;

  // Serializes access to `implementation` (and its scanout buffer) between the
  // emulation thread (load/unload/render/scanoutAsync/...) and the video screen
  // thread (VI::refresh, platform->video).
  //
  // mapScanoutRead() acquires this lock and holds it until unmapScanoutRead(),
  // so the implementation (and its mapped scanout buffer) cannot be destroyed
  // by a concurrent reset (System::power -> vulkan.unload/load) while the
  // screen thread is copying pixels out of it.
  std::recursive_mutex mutex;
  std::unique_lock<std::recursive_mutex> scanoutLock;

  bool enable = true;
  // Written by the JNI thread (PhobosRunner::resetSystem) and read by the
  // emulation thread (scanoutAsync) every frame, so these must be atomic.
  std::atomic<bool> disableVideoInterfaceProcessing = false;
  std::atomic<bool> weaveDeinterlacing = false;
  std::atomic<u32>  internalUpscale = 1;  //1, 2, 4, 8
  std::atomic<bool> supersampleScanout = false;
  std::atomic<u32>  outputUpscale = 1;
};

extern Vulkan vulkan;

}
