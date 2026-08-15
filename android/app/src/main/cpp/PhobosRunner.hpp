#pragma once
#include <ares/ares.hpp>
#include <jni.h>

namespace ares {
  using namespace nall;
  using namespace nall::primitives;

  extern Node::System root;
  extern bool isPaused;

  enum class LogLevel : s32 {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
    None  = 6
  };

  struct LogEntry {
    LogLevel level;
    string message;
  };

  struct PerformanceStats {
    f64 fps;
    f64 frameTime; // ms
    s32 activeCore;
    s32 pipelineFailures;
    bool isAdrenoDriver;
  };

  auto initialize(const char* systemName, const char* uri, const char* romName) -> bool;
  auto unloadSystem() -> void;
  auto setPause(bool paused) -> void;
  auto setEmulationRunning(bool running) -> void;
  auto runFrame() -> void;
  auto setFastForward(bool enabled) -> void;
  auto setFastForwardSpeed(f32 speed) -> void;
  auto setN64DebugLogging(bool enabled) -> void;
  auto saveState(const char* path) -> bool;
  auto loadState(const char* path) -> bool;
  auto takeScreenshot(const char* path) -> bool;
  auto setFastBoot(bool enabled) -> void;
  auto setAutoSaveMemory(bool enabled) -> void;
  auto setAutoLoadMemory(bool enabled) -> void;
  auto setSkipBootRom(bool enabled) -> void;
  auto resetSystem() -> void;
  auto frameAdvance() -> void;
  auto setMuteAudio(bool muted) -> void;
  auto setShader(const char* path) -> bool;
  auto setRegion(s32 regionIndex) -> void;
  auto setN64Renderer(s32 mode) -> void;
  auto setN64Upscale(s32 factor) -> void;
  auto setN64Recompiler(bool enabled) -> void;
  auto setN64ExpansionPak(bool enabled) -> void;
  auto setN64DisableVIProcessing(bool enabled) -> void;
  auto setN64WeaveDeinterlacing(bool enabled) -> void;
  auto setN64SupersampleScanout(bool enabled) -> void;
  auto setN64ViOverclock(s32 percent) -> void;
  auto setN64CountPerOp(s32 value) -> void;
  auto setN64CpuOverclock(s32 factor) -> void;
  auto setN64Pak(const char* pakName) -> void;
  auto getRumbleState() -> bool;
  auto setPs1AnalogMode(bool enabled) -> void;
  auto togglePs1AnalogMode() -> bool;
  auto setStickToDpad(bool enabled) -> void;
  auto setLogLevel(s32 level) -> void;
  auto setRomFd(s32 fd) -> void;
  auto setSecondaryRomFd(s32 fd) -> void;
  auto setTempFilePath(const char* path) -> void;
  auto setLoadDiskImageToRam(bool enabled) -> void;
  auto setOrientationMode(bool vertical) -> void;
  auto setHomePath(const char* path) -> void;
  auto setSavesPath(const char* path) -> void;
  auto setVulkanCachePath(const char* path) -> void;
  auto setNativeLibraryDir(const char* path) -> void;
  auto setFirmwarePath(const char* path) -> void;
  auto mapFirmwareFile(const char* name, const char* path) -> void;
  auto setCustomDriverPath(const char* path) -> void;
  auto loadSecondaryRom(const char* systemName, const char* uri) -> bool;
  auto setSurface(JNIEnv* env, jobject surface) -> void;
  auto getNewLogs() -> std::vector<LogEntry>;
  auto isFirstFrameRendered() -> bool;
  auto setInput(f32 lx, f32 ly, f32 rx, f32 ry, s32 buttons) -> void;
  auto setKeyboardKey(const char* label, bool pressed) -> void;
  auto playTape() -> bool;
  auto setTapeSpeed(s32 speed) -> void;
  auto setZxControlScheme(s32 scheme) -> void;
  auto setZxStickToKeys(bool enabled) -> void;
  auto setZxReversePitch(bool enabled) -> void;
  auto setZxKeyBinding(const char* label, s32 bit) -> void;
  auto setZxTapeMuted(bool muted) -> void;
  auto getZxTapeProgress() -> s32;
  auto getPerformanceStats() -> PerformanceStats;
}
