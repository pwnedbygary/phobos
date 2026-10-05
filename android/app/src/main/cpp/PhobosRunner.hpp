#pragma once
#include <ares/ares.hpp>
#include <array>
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
    s32 emuTid;      // emulation thread id (for per-thread CPU sampling)
    f64 targetFps;   // core's native refresh rate
    s64 playTimeMs;  // unpaused run time since the game's emulation thread started
  };

  // Logical size of the last presented frame after the core's pixel-aspect
  // correction (0 x 0 before the first frame of a game).
  struct VideoGeometry {
    f32 width;
    f32 height;
  };

  auto initialize(const char* systemName, const char* uri, const char* romName) -> bool;
  auto unloadSystem() -> void;
  auto setPause(bool paused) -> void;
  auto setEmulationRunning(bool running) -> void;
  auto runFrame() -> void;
  auto setFastForward(bool enabled) -> void;
  auto setFastForwardSpeed(f32 speed) -> void;
  auto setNgcdLoadSpeed(s32 speed) -> void;
  auto setZxLoadSpeed(s32 speed) -> void;
  auto setMsxLoadSpeed(s32 speed) -> void;
  auto setZxTapeAuto(bool enabled) -> void;
  auto setN64DebugLogging(bool enabled) -> void;
  auto saveState(const char* path) -> bool;
  auto loadState(const char* path) -> bool;
  auto getSaveFiles() -> std::vector<string>;
  auto getButtonNames() -> std::vector<string>;
  auto flushSaves() -> void;
  auto takeScreenshot(const char* path) -> bool;
  auto setFastBoot(bool enabled) -> void;
  auto setAutoSaveMemory(bool enabled) -> void;
  auto setAutoLoadMemory(bool enabled) -> void;
  auto setSkipBootRom(bool enabled) -> void;
  auto resetSystem() -> void;
  auto frameAdvance() -> void;
  auto dumpNgGfx(const char* dir) -> void;
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
  auto setN64AsyncRdp(bool enabled) -> void;
  auto setN64FasterSync(bool enabled) -> void;
  auto setN64SkipCaches(bool enabled) -> void;
  auto setN64RspTaskMode(bool enabled) -> void;
  auto setPinFastestCore(bool enabled) -> void;
  auto setBusyWaitPacing(bool enabled) -> void;
  auto setVideoSettings(bool overscan, bool colorEmulation, bool interframeBlending) -> void;
  auto setN64Pak(const char* pakName) -> void;
  auto getRumbleState() -> bool;
  auto setPs1AnalogMode(bool enabled) -> void;
  auto togglePs1AnalogMode() -> bool;
  auto setStickToDpad(bool enabled) -> void;
  auto setLogLevel(s32 level) -> void;
  auto setRomFd(s32 fd) -> void;
  auto setSecondaryRomFd(s32 fd) -> void;
  // A file the next load (or disc change) opens by path where it is, instead of copying the descriptor's.
  auto setRomPath(const char* path) -> void;
  auto setSecondaryRomPath(const char* path) -> void;
  auto setTempFilePath(const char* path) -> void;
  auto setLoadDiskImageToRam(bool enabled) -> void;
  auto setOrientationMode(bool vertical) -> void;
  auto setHomePath(const char* path) -> void;
  auto setSavesPath(const char* path) -> void;
  // The folder name for the next game's PS1 memory cards (its name without the disc number).
  auto setMemoryCardKey(const char* key) -> void;
  // The PSP's memory stick: a folder the user picked, or empty for the shared one in the saves folder.
  auto setPspMemoryStickPath(const char* path) -> void;
  auto setVulkanCachePath(const char* path) -> void;
  auto setNativeLibraryDir(const char* path) -> void;
  auto setFirmwarePath(const char* path) -> void;
  auto mapFirmwareFile(const char* name, const char* path) -> void;
  // Firmware keys [system] can't start without that aren't set ("fw_mcd" for any Mega CD BIOS);
  // empty when nothing is missing or the system has no such check.
  auto missingFirmware(const char* system) -> std::vector<string>;
  auto setCustomDriverPath(const char* path) -> void;
  auto loadSecondaryRom(const char* systemName, const char* uri) -> bool;
  // The sides of the LaserActive disc being played (its .mmi's media, in order); empty for other systems.
  auto laserdiscSides() -> std::vector<string>;
  // Puts [side] of the LaserActive disc in the tray, or takes the disc out for ""; false without one.
  auto setLaserdiscSide(const char* side) -> bool;
  auto setSurface(JNIEnv* env, jobject surface) -> void;
  auto getNewLogs() -> std::vector<LogEntry>;
  auto isFirstFrameRendered() -> bool;
  auto setInput(f32 lx, f32 ly, f32 rx, f32 ry, s32 buttons) -> void;
  auto setKeyboardKey(const char* label, bool pressed) -> void;
  auto playTape() -> bool;
  // The ZX Spectrum tape: {in, playing, position ms, length ms}.
  auto getZxTapeState() -> std::array<s32, 4>;
  auto setZxTapePlaying(bool play) -> void;
  auto rewindZxTape() -> void;
  // The MSX tape: [in, playing, position ms, length ms]; the motor relay plays it, this winds it back.
  auto getMsxTapeState() -> std::array<s32, 7>;
  auto rewindMsxTape() -> void;
  auto setMsxTape(bool data) -> void;
  auto setMsxTapeRecord(bool armed) -> void;
  auto setZxControlScheme(s32 scheme) -> void;
  auto setZxStickToKeys(bool enabled) -> void;
  auto setZxReversePitch(bool enabled) -> void;
  auto setZxKeyBinding(const char* label, s32 bit) -> void;
  auto setZxTapeMuted(bool muted) -> void;
  auto getPerformanceStats() -> PerformanceStats;
  // Most recent frame-to-frame intervals in ms, oldest first; returns how many were written.
  auto getFrameTimes(f32* out, u32 capacity) -> u32;
  auto getVideoGeometry() -> VideoGeometry;
  /** The core's refresh-rate hint (live atomic; not the UI's polled stats copy). */
  auto getRefreshRateHint() -> f64;
}
