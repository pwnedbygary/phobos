#pragma once
#include <cstdint>

#if defined(__ANDROID__)
struct ANativeWindow;
#endif

// What the runner needs from the platform it runs on: a surface to present frames to, an audio
// device, and a Vulkan loader. PhobosHostAndroid.cpp implements them with ANativeWindow, AAudio and
// libadrenotools; desktop/PhobosHostDesktop.cpp with SDL and the system Vulkan loader.
namespace phobos::host {

// One frame of window pixels: 0xAABBGGRR words (RGBA bytes), `stride` words per line.
struct Frame {
  std::uint32_t* pixels = nullptr;
  std::uint32_t stride = 0;
};

enum class LockResult {
  NoSurface,
  Locked,
  // The surface would not take the requested size; the next lockFrame() asks again.
  Rejected,
};

auto surfaceReady() -> bool;
// Called on the video thread with the runner's window mutex held; unlockFrame() presents.
auto lockFrame(std::uint32_t width, std::uint32_t height, Frame& frame) -> LockResult;
auto unlockFrame() -> void;
auto hintFrameRate(double hz) -> void;

// 48 kHz interleaved stereo float. writeAudio() blocks while the device buffer is full, for up to
// 20 ms, and returns the frames it took (0 or less: the caller drops the rest of the chunk).
auto openAudio() -> bool;
auto audioOpen() -> bool;
auto writeAudio(const float* samples, int frames) -> int;
auto pauseAudio(bool paused) -> void;
auto closeAudio() -> void;
auto audioUnderruns() -> std::int64_t;

// Points parallel-RDP's loader at the Vulkan driver for the next N64 session.
auto loadVulkan(const char* customDriverPath, const char* nativeLibraryDir, const char* tempPath) -> bool;
// The vkGetInstanceProcAddr the last loadVulkan() loaded (the custom driver's, or the system loader's), for the
// PSP's Vulkan renderer; null if none loaded.
auto vulkanLoader() -> void*;

#if defined(__ANDROID__)
// Takes ownership of `window` (released when replaced).
auto setWindow(ANativeWindow* window) -> void;
#endif

}
