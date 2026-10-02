#include "PhobosHost.hpp"
#include "DesktopHost.hpp"

#include <SDL3/SDL.h>

#include "context.hpp"

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace phobos::host {

static std::vector<std::uint32_t> backBuffer;
static std::uint32_t backWidth = 0;
static std::uint32_t backHeight = 0;

static std::mutex presentedMutex;
static std::vector<std::uint32_t> presented;
static std::uint32_t presentedWidth = 0;
static std::uint32_t presentedHeight = 0;
static std::uint64_t presentedSerial = 0;

static SDL_AudioStream* audioStream = nullptr;
static std::int64_t underruns = 0;
// Like Android's AAudio buffer, writeAudio() waits while the device holds this much (64 ms).
static constexpr int audioQueueLimit = 3072 * 2 * (int)sizeof(float);

auto surfaceReady() -> bool { return true; }

auto lockFrame(std::uint32_t width, std::uint32_t height, Frame& frame) -> LockResult {
  backBuffer.resize((size_t)width * height);
  backWidth = width;
  backHeight = height;
  frame.pixels = backBuffer.data();
  frame.stride = width;
  return LockResult::Locked;
}

auto unlockFrame() -> void {
  std::lock_guard<std::mutex> lock(presentedMutex);
  presented.swap(backBuffer);
  presentedWidth = backWidth;
  presentedHeight = backHeight;
  presentedSerial++;
}

auto hintFrameRate(double) -> void {}

auto takeFrame(std::uint64_t& serial, std::vector<std::uint32_t>& pixels, std::uint32_t& width, std::uint32_t& height) -> bool {
  std::lock_guard<std::mutex> lock(presentedMutex);
  if (presentedSerial == serial || presented.empty()) return false;
  pixels = presented;
  width = presentedWidth;
  height = presentedHeight;
  serial = presentedSerial;
  return true;
}

auto openAudio() -> bool {
  if (audioStream) return true;
  if (!SDL_WasInit(SDL_INIT_AUDIO)) return false;
  SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
  audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!audioStream) {
    SDL_Log("Audio: %s", SDL_GetError());
    return false;
  }
  // Prime with silence so the first emulated frames have headroom.
  std::vector<float> silence(1024 * 2, 0.0f);
  SDL_PutAudioStreamData(audioStream, silence.data(), (int)(silence.size() * sizeof(float)));
  SDL_ResumeAudioStreamDevice(audioStream);
  return true;
}

auto audioOpen() -> bool { return audioStream != nullptr; }

auto writeAudio(const float* samples, int frames) -> int {
  if (!audioStream) return -1;
  Uint64 deadline = SDL_GetTicksNS() + 20'000'000;
  int queued = SDL_GetAudioStreamQueued(audioStream);
  while (queued > audioQueueLimit) {
    if (SDL_GetTicksNS() >= deadline) return 0;
    SDL_Delay(1);
    queued = SDL_GetAudioStreamQueued(audioStream);
  }
  if (queued == 0) underruns++;
  if (!SDL_PutAudioStreamData(audioStream, samples, frames * 2 * (int)sizeof(float))) return -1;
  return frames;
}

auto pauseAudio(bool paused) -> void {
  if (!audioStream) return;
  if (paused) {
    SDL_PauseAudioStreamDevice(audioStream);
    SDL_ClearAudioStream(audioStream);
  } else {
    SDL_ResumeAudioStreamDevice(audioStream);
  }
}

auto closeAudio() -> void {
  if (!audioStream) return;
  SDL_DestroyAudioStream(audioStream);
  audioStream = nullptr;
}

auto audioUnderruns() -> std::int64_t { return underruns; }

#if defined(__APPLE__)
// macOS has no Vulkan loader: Phobos.app carries MoltenVK in Contents/Frameworks.
static auto bundledMoltenVK() -> std::string {
  const char* base = SDL_GetBasePath();
  if (!base) return {};
  for (const char* relative : {"../Frameworks/libMoltenVK.dylib", "libMoltenVK.dylib"}) {
    std::string path = std::string(base) + relative;
    if (SDL_GetPathInfo(path.c_str(), nullptr)) return path;
  }
  return {};
}
#endif

auto loadVulkan(const char*, const char*, const char*) -> bool {
  if (!std::getenv("GRANITE_VULKAN_LIBRARY")) {
    #if defined(__APPLE__)
    auto moltenVK = bundledMoltenVK();
    if (!moltenVK.empty()) setenv("GRANITE_VULKAN_LIBRARY", moltenVK.c_str(), 1);
    #elif defined(__linux__)
    // parallel-RDP opens libvulkan.so, which only the loader's development package installs.
    setenv("GRANITE_VULKAN_LIBRARY", "libvulkan.so.1", 1);
    #endif
  }
  return ::Vulkan::Context::init_loader(nullptr, true);
}

}
