#include <n64/n64.hpp>
#include <chrono>

// Gated by the Phobos "N64 Debug Logging" toggle (defined in PhobosRunner.cpp).
namespace ares {
auto n64DebugLoggingEnabled() -> bool;
}

namespace ares::Nintendo64 {

Vulkan vulkan;
std::vector<uint8_t> Vulkan::pipelineCacheData;
string Vulkan::pipelineCachePath;
std::atomic<int> Vulkan::pipelineFailureCount{0};
string Vulkan::gpuDeviceName;
std::atomic<bool> Vulkan::skipCachePersist{false};
std::atomic<bool> Vulkan::discardPipelineCache{false};

struct LoggingInterface : Util::LoggingInterface {
  auto log(const char* tag, const char* fmt, va_list va) -> bool {
    char buffer[8192];
    vsnprintf(buffer, sizeof(buffer), fmt, va);

    // Count unique pipeline creation failures for the Turnip suggestion
    // dialog in the Android UI.
    if (strstr(buffer, "Failed to create") && strstr(buffer, "blacklisted")) {
      Vulkan::pipelineFailureCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Rate-limit the repetitive "flush render state / dispatch will be dropped"
    // spam that floods logcat when the GPU driver can't compile compute shaders.
    // These fire at ~120/sec and cause measurable performance overhead.
    // TEMPORARILY DISABLED (2026-08-15) for the Rogue Squadron menu debug — we
    // need to see if the menu's render is being silently dropped. Restore after.
    if (false && (strstr(buffer, "flush render state") || strstr(buffer, "dispatch will be dropped"))) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushError).count();
      if (elapsed < 5000) return true; // suppress for 5 seconds
      lastFlushError = now;
    }

    // Suppress "Thread does not exist in thread manager or is not the main
    // thread." entirely. parallel-RDP's get_current_thread_index() logs this
    // whenever ANY thread not registered with its internal thread manager
    // touches the RDP — here that is the ares emulation thread and the ares
    // screen thread, every frame. It falls back to index 0 safely, so the
    // message is expected noise with no diagnostic value, and at ~120/sec it
    // is pure logcat flooding + wasted logd calls. (We must NOT "fix" it by
    // registering those threads as index 0: that would make parallel-RDP
    // treat distinct threads as one and skip required synchronization.)
    if (strstr(buffer, "Thread does not exist in thread manager")) return true;

    __android_log_print(ANDROID_LOG_ERROR, "Granite", "%s", buffer);
    return true;
  }

  std::chrono::steady_clock::time_point lastFlushError;
} loggingInterface;

struct Vulkan::Implementation {
  Implementation(u8* data, u32 size, const uint8_t* cacheData = nullptr, u32 cacheSize = 0);
  ~Implementation();

  ::Vulkan::Context context;
  ::Vulkan::Device device;
  ::RDP::CommandProcessor* processor = nullptr;
  atomic<const char*> crash_error = nullptr;

  struct Validation : public ::RDP::ValidationInterface {
    Implementation& self;
    Validation(Implementation& i) : self(i) {}
    void report_rdp_crash(::RDP::ValidationError err, const char *msg) override {
      // NON-FATAL: log and continue. parallel-RDP already skips the offending
      // operation (e.g. a 4bpp tile upload) and returns. If we set crash_error
      // here, ares' RDP::render() sees it and calls RDP::crash(), which sets
      // command.crashed/pipeBusy/bufferBusy and permanently kills the RDP — the
      // game then waits forever on the dead RDP → frozen screen at 60fps with
      // identical frames (Conker's BFD pub menu, save-state restores, any 4bpp
      // texture). On real hardware these are rare/fatal, but for a general
      // emulator skip-and-continue is far better than a hard freeze.
      __android_log_print(ANDROID_LOG_WARN, "PhobosVulkan",
                          "RDP validation (%d) non-fatal: %s", (int)err, msg);
    }
  } validator{*this};

  //commands are u64 words, but the backend uses u32 swapped words.
  //size and offset are in u64 words.
  u32 buffer[0x10000] = {};
  u32 queueSize = 0;
  u32 queueOffset = 0;

  ::RDP::VIScanoutBuffer scanout;
  std::mutex lock;
  std::condition_variable condition;
  u32 scanoutCount = 0;
  u32 endCount = 0;
};

auto Vulkan::load(Node::Object object) -> bool {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Vulkan::load called, enable = %d", vulkan.enable);
  if (vulkan.enable) {
    Util::set_thread_logging_interface(&loggingInterface);

    // If we're discarding the cache, the GPU is likely in a bad state.
    // Skip idle wait on the old implementation to avoid hanging the main thread.
    if (discardPipelineCache && implementation && implementation->processor) {
      implementation->processor->set_skip_idle_on_destroy(true);
    }
    delete implementation;
    implementation = nullptr;

    // During reset (System::power), discard both in-memory and on-disk
    // pipeline cache. Reusing stale cache across VkDevice instances
    // causes Turnip to produce broken GPU fences.
    if (discardPipelineCache) {
      pipelineCacheData.clear();
      discardPipelineCache = false;
    } else if (pipelineCacheData.empty() && pipelineCachePath.size() > 0) {
      auto disk = nall::file::read(pipelineCachePath);
      if (disk.size() > 0) {
        pipelineCacheData.assign(disk.data(), disk.data() + disk.size());
      }
    }
    // Inject saved pipeline cache BEFORE Implementation ctor so
    // device.init_frame_contexts() sees it during pipeline creation.
    std::vector<uint8_t> cacheCopy;
    cacheCopy.swap(pipelineCacheData);
    implementation = new Vulkan::Implementation(rdram.ram.data, rdram.ram.size, cacheCopy.data(), (u32)cacheCopy.size());
    if(!implementation->processor) {
      delete implementation;
      implementation = nullptr;
    }

    if (!implementation) {
      platform->status("Vulkan init failed: No RDP rendering support");
      vulkan.enable = false;
      rdram.hidden.data = (u8*)malloc(4_MiB); // Fallback allocation to prevent crash
      memset(rdram.hidden.data, 0, 4_MiB);
    } else {
      platform->status("Vulkan Enabled: using paraLLEl-RDP");
      rdram.hidden.data = (u8*)implementation->processor->begin_read_hidden_rdram();
    }
  } else {
    platform->status("Vulkan Disabled: No RDP rendering support");
    rdram.hidden.data = (u8*)malloc(4_MiB); // Fallback allocation to prevent crash
    memset(rdram.hidden.data, 0, 4_MiB);
  }

  return true;
}

auto Vulkan::unload() -> void {
  std::lock_guard<std::recursive_mutex> lock(mutex);

  // Before tearing down the VkDevice, try to drain any in-flight GPU work
  // with a BOUNDED wait. Destroying a device that still has pending
  // submissions (the skip_idle path) leaves orphaned timelines behind, and
  // the next device created in this process can then fail to signal its
  // fences — this is the "N64 reset never comes back" / "sometimes works,
  // sometimes doesn't" failure on Turnip. The scanout fence is the last
  // submission and is ordered after all RDP command-ring work, so its
  // signal means the GPU has caught up: the CommandProcessor destructor's
  // normal idle() teardown then completes cleanly and the next device
  // starts fresh. Only if it times out (GPU genuinely wedged) do we arm
  // skip_idle_on_destroy so teardown never blocks on a fence that will
  // not signal, and we skip the pipeline-cache readback (which would
  // implicitly wait for that same stuck work).
  bool gpuHealthy = true;
  if (implementation && implementation->processor && implementation->scanout.fence) {
    if (!implementation->scanout.fence->wait_timeout(2'000'000'000ull)) {
      gpuHealthy = false;
      __android_log_print(ANDROID_LOG_WARN, "PhobosVulkan",
          "Vulkan: scanout fence timed out during unload (GPU stall). Forcing skip-idle teardown.");
    }
  }
  if (!gpuHealthy && implementation && implementation->processor) {
    implementation->processor->set_skip_idle_on_destroy(true);
  }

  if (!discardPipelineCache && gpuHealthy && implementation && implementation->device.get_device() != VK_NULL_HANDLE) {
    size_t cacheSize = implementation->device.get_pipeline_cache_size();
    if (cacheSize > 0) {
      pipelineCacheData.resize(cacheSize);
      if (implementation->device.get_pipeline_cache_data(pipelineCacheData.data(), cacheSize)) {
        __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Pipeline cache saved (%zu bytes)", cacheSize);
        // Persist to disk for next app launch (alongside driver UUID
        // so we can detect driver changes and invalidate stale caches).
        if (pipelineCachePath.size() > 0 && !skipCachePersist) {
          nall::file::write(pipelineCachePath, pipelineCacheData);
          // Write UUID to sidecar so next load detects driver changes
          string uuidPath = {pipelineCachePath, ".uuid"};
          VkPhysicalDeviceProperties props;
          vkGetPhysicalDeviceProperties(implementation->device.get_physical_device(), &props);
          nall::file::write(uuidPath, {(u8*)props.pipelineCacheUUID, sizeof(props.pipelineCacheUUID)});
        }
      } else {
        pipelineCacheData.clear();
      }
    }
  } else {
    pipelineCacheData.clear();
  }
  if (rdram.hidden.data && (!implementation || rdram.hidden.data != (u8*)implementation->processor->begin_read_hidden_rdram())) {
    free(rdram.hidden.data);
  }
  rdram.hidden.data = nullptr;
  if (implementation) {
    delete implementation;
    implementation = nullptr;
  }
}

auto Vulkan::render() -> bool {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!implementation) return false;

  static constexpr u32 commandLength[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 4, 6,12,14,12,14,20,22,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  };

  auto& command = rdp.command;

  u32 current = command.current & ~7;
  u32 end = command.end & ~7;
  u32 length = (end - current) / 8;
  if(current >= end) return true;

  u32* buffer = implementation->buffer;
  u32& queueSize = implementation->queueSize;
  u32& queueOffset = implementation->queueOffset;
  if(queueSize + length >= 0x8000) return true;

  if(!command.source) {
    do {
      buffer[queueSize * 2 + 0] = rdram.ram.read<Word>(current, RBusDevice::DP_DMA); current += 4;
      buffer[queueSize * 2 + 1] = rdram.ram.read<Word>(current, RBusDevice::DP_DMA); current += 4;
      queueSize++;
    } while(--length);
  } else {
    do {
      buffer[queueSize * 2 + 0] = rsp.dmem.read<Word>(current); current += 4;
      buffer[queueSize * 2 + 1] = rsp.dmem.read<Word>(current); current += 4;
      if(system.homebrewMode) {
        rsp.debugger.dmemReadWord(current - 8, 8, "RDP XBUS");
      }
      queueSize++;
    } while(--length);
  }

  while(queueOffset < queueSize) {
    u32 op = buffer[queueOffset * 2];
    u32 code = op >> 24 & 63;
    u32 length = commandLength[code];

    if(queueOffset + length > queueSize) {
      //partial command, keep data around for next processing call
      command.start = command.current = command.end;
      return true;
    }

    if(code >= 8) {
      implementation->processor->enqueue_command(length * 2, buffer + queueOffset * 2);
    }

    if(::RDP::Op(code) == ::RDP::Op::SyncFull) {
      implementation->processor->wait_for_timeline(implementation->processor->signal_timeline());
      rdp.syncFull();
    }

    queueOffset += length;
  }

  queueOffset = 0;
  queueSize = 0;
  command.current = command.end;
  return true;
}

auto Vulkan::frame() -> void {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!implementation) return;
  implementation->processor->begin_frame_context();
}

auto Vulkan::writeWord(u32 address, u32 data) -> void {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!implementation) return;
  implementation->processor->set_vi_register(::RDP::VIRegister(address), data);
}

auto Vulkan::scanoutAsync(bool field) -> bool {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!implementation) return false;

  implementation->processor->set_vi_register(::RDP::VIRegister::VCurrentLine, field);

  //0 steps if scanning out at upscaled resolution.
  //each downscale step reduces output resolution to [width, height] * max(1, upscale >> downscale_steps)
  ::RDP::ScanoutOptions options;
  options.downscale_steps = supersampleScanout ? 16 : 0;
  options.persist_frame_on_invalid_input = true;  //this is a compatibility hack, but I'm not sure what for ...
  if(disableVideoInterfaceProcessing) {
    options.vi = {false, false, true, false, false, false};
  }
  if(!supersampleScanout){
    options.blend_previous_frame = weaveDeinterlacing;
    options.upscale_deinterlacing = !weaveDeinterlacing;
  }
  else {
    options.blend_previous_frame = false;
    options.upscale_deinterlacing = true;
  }


  if(implementation->scanout.fence) {
    // Bounded wait: after a reset, the fence may be from the frame that was
    // in flight when the reset fired, and Turnip can fail to signal it. A
    // hard wait() here would hang the emulation thread forever (black
    // screen, ~500ms fence timeout loop = ~2 fps). If it doesn't signal in
    // 100ms, drop the stale frame and let scanout_async_buffer submit a
    // fresh one on the current device.
    if (!implementation->scanout.fence->wait_timeout(100'000'000ull)) {
      __android_log_print(ANDROID_LOG_WARN, "PhobosVulkan",
          "Vulkan::scanoutAsync: previous scanout fence timed out; dropping stale frame");
    }
  }
  // Gated behind the N64 Debug Logging toggle (consistent with all other
  // Phobos diag) — 1 log per 300 scanouts when enabled.
  static int scanoutCountLog = 0;
  if (::ares::n64DebugLoggingEnabled() && ++scanoutCountLog % 300 == 0)
    __android_log_print(ANDROID_LOG_DEBUG, "PhobosVulkan", "Vulkan::scanoutAsync triggered");
  implementation->processor->scanout_async_buffer(implementation->scanout, options);
  implementation->scanoutCount++;
  return true;
}

auto Vulkan::mapScanoutRead(const u8*& rgba, u32& width, u32& height) -> void {
  // Acquire and hold the lock until unmapScanoutRead() so the implementation
  // (and its scanout buffer) cannot be destroyed while we are reading it.
  if(!scanoutLock.owns_lock()) scanoutLock = std::unique_lock<std::recursive_mutex>(mutex);
  if(!implementation || !implementation->scanout.fence || !implementation->scanout.width || !implementation->scanout.height) {
    rgba = nullptr;
    width = 0;
    height = 0;
  } else {
    // Bounded wait for the same reason as scanoutAsync: after a reset the
    // fence may never signal on Turnip. If it times out, present a blank
    // frame instead of wedging the screen thread (which holds vulkan.mutex
    // via scanoutLock and would block every subsequent Vulkan load).
    if (implementation->scanout.fence->wait_timeout(100'000'000ull)) {
      rgba = (const u8*)implementation->device.map_host_buffer(*implementation->scanout.buffer, ::Vulkan::MEMORY_ACCESS_READ_BIT);
      width = implementation->scanout.width;
      height = implementation->scanout.height;
    }
  }
}

auto Vulkan::unmapScanoutRead() -> void {
  if(implementation && implementation->scanout.buffer) {
    implementation->device.unmap_host_buffer(*implementation->scanout.buffer, ::Vulkan::MEMORY_ACCESS_READ_BIT);
  }
  // Release the lock acquired by mapScanoutRead().
  if(scanoutLock.owns_lock()) scanoutLock.unlock();
}

auto Vulkan::endScanout() -> void {
  if(implementation) {
    //notify main thread that we're done reading
    std::lock_guard<std::mutex> lock{implementation->lock};
    implementation->endCount++;
    implementation->condition.notify_one();
  }
}

auto Vulkan::crashed() -> const char* {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(implementation) return implementation->crash_error;
  return nullptr;
}

auto Vulkan::setSkipIdleOnDestroy(bool skip) -> void {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (implementation && implementation->processor) {
    implementation->processor->set_skip_idle_on_destroy(skip);
  }
}

auto Vulkan::resetScanoutFence() -> void {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (implementation) {
    // Reset the intrusive fence pointer WITHOUT waiting. The old fence
    // (from the pre-reset in-flight frame) may never signal on Turnip;
    // scanout_async_buffer will replace it with a fresh submission next
    // frame.
    implementation->scanout.fence.reset();
  }
}

Vulkan::Implementation::Implementation(u8* data, u32 size, const uint8_t* cacheData, u32 cacheSize) {
  __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Vulkan::Implementation constructor");
  if(!::Vulkan::Context::init_loader(nullptr, false)) {
    __android_log_print(ANDROID_LOG_ERROR, "PhobosVulkan", "Vulkan loader init failed");
    return;
  }
  __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Vulkan loader init success, initializing instance");
  if(!context.init_instance_and_device(nullptr, 0, nullptr, 0, 0)) {
    __android_log_print(ANDROID_LOG_ERROR, "PhobosVulkan", "Vulkan instance init failed");
    return;
  }
  device.set_context(context);

  // Validate saved cache against the GPU's pipelineCacheUUID.
  // If the driver or device changed (custom GPU driver swap, OS update),
  // reject the stale cache so it doesn't poison pipeline creation.
  if (cacheData && cacheSize > 0 && pipelineCachePath.size() > 0) {
    string uuidPath = {pipelineCachePath, ".uuid"};
    auto savedUUID = nall::file::read(uuidPath);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device.get_physical_device(), &props);
    if (savedUUID.size() != sizeof(props.pipelineCacheUUID) ||
        memcmp(savedUUID.data(), props.pipelineCacheUUID, sizeof(props.pipelineCacheUUID)) != 0) {
      __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Pipeline cache: driver UUID changed, discarding stale cache");
      nall::file::remove(pipelineCachePath);
      nall::file::remove(uuidPath);
      cacheData = nullptr; cacheSize = 0;
      pipelineCacheData.clear();
    }
  }

  // Store GPU device name for the UI (used to suppress Turnip suggestion
  // when a community driver is already active).
  {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device.get_physical_device(), &props);
    Vulkan::gpuDeviceName = props.deviceName;
    __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "GPU: %s", props.deviceName);
  }

  // Always initialize the pipeline cache. On first load, pass null to
  // create an empty cache that compiled shaders write into. On subsequent
  // loads, pass saved data so the driver reuses cached binaries.
  device.init_pipeline_cache(cacheData, cacheSize);
  if (cacheData && cacheSize > 0) {
    __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Pipeline cache loaded (%u bytes)", cacheSize);
  } else {
    __android_log_print(ANDROID_LOG_INFO, "PhobosVulkan", "Pipeline cache initialized (empty)");
  }
  // 4 frame contexts: 3 in-flight + 1 spare. Reduces GPU pipeline stalls
  // on slower hardware (e.g. stock Adreno) when frames complete out-of-order
  // due to driver-internal async compute scheduling.
  device.init_frame_contexts(4);

  ::RDP::CommandProcessorFlags flags = ::RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT;
  switch(vulkan.internalUpscale) {
  case 2: flags |= ::RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT; break;
  case 4: flags |= ::RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT; break;
  case 8: flags |= ::RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT; break;
  }

  if(vulkan.internalUpscale > 1) {
    flags |= ::RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;
    //rasky: this is explicitly disabled because we want to make sure we don't
    // read back the super sampled version, as it can cause artifacts. We want
    // parallelRDP to also produce a 1x render to use for readbacks.
    //flags |= ::RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT;
  }

  processor = new ::RDP::CommandProcessor(device, data, 0, size, size / 2, flags);
  if(!processor->device_is_supported()) {
    delete processor;
    processor = nullptr;
    return;
  }

  processor->set_validation_interface(&validator);
}

Vulkan::Implementation::~Implementation() {
  if(processor) delete processor;
}

}
