#include "PhobosHost.hpp"

#include <aaudio/AAudio.h>
#include <adrenotools/driver.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "context.hpp"

#define LOG_TAG "PhobosCore"

namespace phobos::host {

static ANativeWindow* window = nullptr;
// Geometry last requested from the window; 0 x 0 forces the next lockFrame() to set it again.
static std::uint32_t geometryWidth = 0;
static std::uint32_t geometryHeight = 0;
static bool locked = false;

static AAudioStream* audioStream = nullptr;
static std::int64_t lastUnderruns = 0;

auto setWindow(ANativeWindow* next) -> void {
  if (window) ANativeWindow_release(window);
  window = next;
  geometryWidth = 0;
  geometryHeight = 0;
}

auto surfaceReady() -> bool { return window != nullptr; }

auto lockFrame(std::uint32_t width, std::uint32_t height, Frame& frame) -> LockResult {
  if (!window) return LockResult::NoSurface;
  if (width != geometryWidth || height != geometryHeight) {
    ANativeWindow_setBuffersGeometry(window, (int32_t)width, (int32_t)height, WINDOW_FORMAT_RGBA_8888);
    geometryWidth = width;
    geometryHeight = height;
  }
  ANativeWindow_Buffer buffer;
  if (ANativeWindow_lock(window, &buffer, nullptr) != 0) return LockResult::NoSurface;
  if ((std::uint32_t)buffer.width < width || (std::uint32_t)buffer.height < height) {
    // The geometry request did not take effect; writing would overrun the buffer.
    ANativeWindow_unlockAndPost(window);
    geometryWidth = 0;
    geometryHeight = 0;
    return LockResult::Rejected;
  }
  frame.pixels = (std::uint32_t*)buffer.bits;
  frame.stride = (std::uint32_t)buffer.stride;
  locked = true;
  return LockResult::Locked;
}

auto unlockFrame() -> void {
  if (locked && window) ANativeWindow_unlockAndPost(window);
  locked = false;
}

// Ask the compositor for a display mode near the game's rate. Looked up at run time
// since minSdk is 26. On the RP6, FIXED_SOURCE + the two-arg (seamless-only) call
// registered a 60 Hz override but left the panel at 120 Hz; Mupen's Parallel profile
// votes DEFAULT and the panel does switch. Prefer WithChangeStrategy(ALWAYS) so a
// non-seamless mode change is still allowed, and snap near-60 NTSC rates to 60 Hz so
// the vote matches a supported mode exactly. Java also sets preferredDisplayModeId.
auto hintFrameRate(double rate) -> void {
  using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);
  using SetFrameRateWithStrategy = int32_t (*)(ANativeWindow*, float, int8_t, int8_t);
  static auto symbols = [] {
    struct {
      SetFrameRateWithStrategy withStrategy;
      SetFrameRate setFrameRate;
    } s{};
    void* android = dlopen("libandroid.so", RTLD_NOW);
    if (android) {
      s.withStrategy = (SetFrameRateWithStrategy)dlsym(android, "ANativeWindow_setFrameRateWithChangeStrategy");
      s.setFrameRate = (SetFrameRate)dlsym(android, "ANativeWindow_setFrameRate");
    }
    return s;
  }();
  if (!window || rate <= 0.0) return;
  float request = (float)rate;
  // Progressive N64 (~59.826) and 59.94 NTSC: match the panel's 60 Hz mode.
  if (std::abs(rate - 60.0) < 1.5 || std::abs(rate - 59.94) < 1.5) request = 60.0f;
  // DEFAULT (0) + ALWAYS (1): see ANATIVEWINDOW_FRAME_RATE_* / CHANGE_FRAME_RATE_*.
  int32_t result = -1;
  if (symbols.withStrategy) {
    result = symbols.withStrategy(window, request, /*DEFAULT*/ 0, /*ALWAYS*/ 1);
  } else if (symbols.setFrameRate) {
    result = symbols.setFrameRate(window, request, /*DEFAULT*/ 0);
  } else {
    return;
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Window frame rate %.3f Hz requested as %.3f (%d)", rate, request, result);
}

auto openAudio() -> bool {
  if (audioStream) return true;
  AAudioStreamBuilder* builder;
  AAudio_createStreamBuilder(&builder);
  AAudioStreamBuilder_setSampleRate(builder, 48000);
  AAudioStreamBuilder_setChannelCount(builder, 2);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_openStream(builder, &audioStream);
  AAudioStreamBuilder_delete(builder);
  if (!audioStream) return false;
  // 32 bursts (~6144 frames at 48k = ~128ms): holds several frames
  // of output and rides out short stalls without underrunning.
  int32_t burst = AAudioStream_getFramesPerBurst(audioStream);
  int32_t bufferFrames = burst * 32;
  AAudioStream_setBufferSizeInFrames(audioStream, bufferFrames);
  // Prime with silence so the first emulated frames have headroom.
  std::vector<float> silence((size_t)bufferFrames * 2, 0.0f);
  int64_t written = 0;
  while (written < bufferFrames) {
    int32_t n = AAudioStream_write(audioStream, silence.data() + written * 2,
        (int32_t)(bufferFrames - written), 0);
    if (n <= 0) break;
    written += n;
  }
  AAudioStream_requestStart(audioStream);
  return true;
}

auto audioOpen() -> bool { return audioStream != nullptr; }

auto writeAudio(const float* samples, int frames) -> int {
  if (!audioStream) return -1;
  return AAudioStream_write(audioStream, samples, frames, 20'000'000);
}

// Only requestStart if not already starting/started — calling it on an
// already-starting stream returns -895 and can desync the clock.
auto pauseAudio(bool paused) -> void {
  if (!audioStream) return;
  if (paused) {
    AAudioStream_requestStop(audioStream);
  } else {
    aaudio_stream_state_t state = AAudioStream_getState(audioStream);
    if (state != AAUDIO_STREAM_STATE_STARTING && state != AAUDIO_STREAM_STATE_STARTED) {
      AAudioStream_requestStart(audioStream);
    }
  }
}

// After close(), AAudio's internal DefaultDispatch thread is still winding
// down asynchronously; wait for its state to reach a terminal state so
// the next load's open() can't race a live dispatch thread (the SIGSEGV
// 0x80 on a fresh load after a quit).
auto closeAudio() -> void {
  if (!audioStream) return;
  AAudioStream* oldStream = audioStream;
  AAudioStream_requestStop(oldStream);
  AAudioStream_close(oldStream);
  audioStream = nullptr;
  constexpr int kMaxWaitMs = 300;
  for (int i = 0; i < kMaxWaitMs; i += 10) {
    aaudio_stream_state_t st = AAudioStream_getState(oldStream);
    if (st == AAUDIO_STREAM_STATE_UNINITIALIZED || st == AAUDIO_STREAM_STATE_CLOSED) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

auto audioUnderruns() -> std::int64_t {
  if (audioStream) {
    int64_t count = AAudioStream_getXRunCount(audioStream);
    if (count >= 0) lastUnderruns = count;
  }
  return lastUnderruns;
}

auto loadVulkan(const char* customDriverPath, const char* nativeLibraryDir, const char* tempPath) -> bool {
  if (!customDriverPath || !customDriverPath[0]) return ::Vulkan::Context::init_loader(nullptr, true);

  std::string path = customDriverPath;
  auto lastSlash = path.find_last_of('/');
  std::string driverDir = lastSlash == std::string::npos ? "" : path.substr(0, lastSlash + 1);
  std::string driverFile = lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);

  // HACK: Some drivers expect libvulkan.so.1, but Android only provides libvulkan.so
  std::string libVulkan1 = std::string(tempPath ? tempPath : "") + "/libvulkan.so.1";
  if (access(libVulkan1.c_str(), F_OK) == -1) {
    symlink("/system/lib64/libvulkan.so", libVulkan1.c_str());
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "adrenotools: Created symlink libvulkan.so.1 -> /system/lib64/libvulkan.so");
  }

  // Flags: CUSTOM (1) | FILE_REDIRECT (2) = 3
  void* vulkanModule = adrenotools_open_libvulkan(RTLD_NOW, 3, nullptr, nativeLibraryDir, driverDir.c_str(), driverFile.c_str(), tempPath, nullptr);
  if (!vulkanModule) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "adrenotools: Failed to load custom driver! dlerror: %s", dlerror());
    ::Vulkan::Context::init_loader(nullptr, true);
    return false;
  }
  auto gipa = (PFN_vkGetInstanceProcAddr)dlsym(vulkanModule, "vkGetInstanceProcAddr");
  if (!gipa) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "adrenotools: Failed to resolve vkGetInstanceProcAddr from custom driver! dlerror: %s", dlerror());
    ::Vulkan::Context::init_loader(nullptr, true);
    return false;
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "adrenotools: Successfully resolved vkGetInstanceProcAddr");
  ::Vulkan::Context::init_loader(gipa, true);
  return true;
}

auto vulkanLoader() -> void* {
  return (void*)::Vulkan::Context::get_instance_proc_addr();
}

}
