#include "PhobosRunner.hpp"
#include "vfs_android.hpp"
#include <mia/mia.hpp>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <aaudio/AAudio.h>
#include <fcntl.h>
#include <unistd.h>
#include <arm_neon.h>
#include <pthread.h>
#include <sched.h>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <vector>
#include <map>

#include <a26/a26.hpp>
#include <cv/cv.hpp>
#include <fc/fc.hpp>
#include <gb/gb.hpp>
#include <gba/gba.hpp>
#include <md/md.hpp>
#include <ms/ms.hpp>
#include <msx/msx.hpp>
#include <n64/n64.hpp>
#include <ng/ng.hpp>
#include <ngp/ngp.hpp>
#include <pce/pce.hpp>
#undef NCCS
#include <ps1/ps1.hpp>
#include <saturn/saturn.hpp>
#include <sfc/sfc.hpp>
#include <sg/sg.hpp>
#include <spec/spec.hpp>
#include <ws/ws.hpp>

#include <nall/encode/png.hpp>
#include <adrenotools/driver.h>

using namespace nall;
using namespace nall::primitives;

#define LOG_TAG "PhobosCore"

namespace ares {
  auto addLog(LogLevel level, string message) -> void;
}

static inline void log_internal(ares::LogLevel level, const char* format, ...) {
    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // Always print to Logcat for now so user can see it in terminal too
    s32 androidLevel = ANDROID_LOG_INFO;
    switch(level) {
        case ares::LogLevel::Trace: androidLevel = ANDROID_LOG_VERBOSE; break;
        case ares::LogLevel::Debug: androidLevel = ANDROID_LOG_DEBUG; break;
        case ares::LogLevel::Info:  androidLevel = ANDROID_LOG_INFO; break;
        case ares::LogLevel::Warn:  androidLevel = ANDROID_LOG_WARN; break;
        case ares::LogLevel::Error: androidLevel = ANDROID_LOG_ERROR; break;
        case ares::LogLevel::Fatal: androidLevel = ANDROID_LOG_FATAL; break;
        default: break;
    }
    __android_log_print(androidLevel, LOG_TAG, "%s", buf);

    ares::addLog(level, buf);
}

#define LOGI(...) log_internal(ares::LogLevel::Info, __VA_ARGS__)
#define LOGD(...) log_internal(ares::LogLevel::Debug, __VA_ARGS__)
#define LOGE(...) log_internal(ares::LogLevel::Error, __VA_ARGS__)
#define LOGW(...) log_internal(ares::LogLevel::Warn, __VA_ARGS__)

namespace ares {
  Node::System root;
  std::atomic<bool> isPausedAtomic{false};
  std::atomic<bool> emulationRunning{false};
  std::atomic<bool> fastForwardAtomic{false};
  std::atomic<f32>  ffSpeedLimitAtomic{2.0f};
  // Per-core refresh rate reported by ares via Screen::refreshRateHint().
  // Most cores are 60 Hz but WonderSwan/NGP run at ~75 Hz and PAL cores at
  // 50 Hz — the old hardcoded 60 FPS cap throttled those cores and caused
  // audio/video desync. Written from the ares video thread, read by the
  // emulation loop for the frame cap / fast-forward target.
  std::atomic<double> refreshRateAtomic{60.0};
  std::atomic<bool> resetRequestedAtomic{false};
  pthread_t emuThread = 0;
  std::atomic<bool> emuThreadRunning{false};

  static s32 romFd = -1;
  static s32 secondaryRomFd = -1;
  static std::shared_ptr<mia::Pak> currentMedium;
  static std::shared_ptr<mia::Pak> secondaryMedium;
  static std::atomic<bool> firstFrameRendered{false};
  static ANativeWindow* nativeWindow = nullptr;
  static AAudioStream* audioStream = nullptr;

  // ── Dedicated audio thread + ring buffer ────────────────────────────────
  // The emulation thread NEVER blocks on AAudioStream_write, and never does
  // O(n) work on a growing queue. Samples go into a FIXED-CAPACITY ring
  // buffer (O(1) push/pop, no memmove) capped at ~125ms; the dedicated audio
  // thread drains it into AAudio with a bounded blocking write. This removes
  // the synchronous-write churn (underrun→restart) that throttled run() to
  // 50-57 FPS, and the small cap keeps latency low (GBA UI "dings" were
  // delayed ~0.5s by an earlier 1s-cap vector queue). Oldest samples are
  // dropped (overwritten) when the emulator out-produces the DAC, so a
  // fast-forward burst can't leave a backlog that keeps playing after you
  // drop back to 60.
  static std::mutex audioMutex;
  static std::condition_variable audioCV;
  static constexpr size_t audioRingCapacity = 48000 * 2 / 8;  // ~125ms stereo floats
  static std::vector<f32> audioRing;        // fixed capacity, used as a ring
  static size_t audioRingHead = 0;          // oldest sample index
  static size_t audioRingSize = 0;          // samples currently buffered
  static std::thread audioThread;
  static std::atomic<bool> audioThreadRunning{false};
  static std::atomic<bool> audioThreadStop{false};

  static auto audioThreadMain() -> void {
    while (!audioThreadStop.load()) {
      std::vector<f32> chunk;
      {
        std::unique_lock<std::mutex> lock(audioMutex);
        audioCV.wait_for(lock, std::chrono::milliseconds(20),
            []{ return audioRingSize > 0 || audioThreadStop.load(); });
        if (audioThreadStop.load() && audioRingSize == 0) break;
        if (audioRingSize == 0) continue;
        // Drain up to ~2048 floats (1024 stereo frames) per iteration.
        size_t take = std::min<size_t>(audioRingSize, 2048);
        chunk.resize(take);
        for (size_t i = 0; i < take; i++)
          chunk[i] = audioRing[(audioRingHead + i) % audioRingCapacity];
        audioRingHead = (audioRingHead + take) % audioRingCapacity;
        audioRingSize -= take;
        // Wake the emulation thread's audio-pacing wait so it can resume
        // running frames as soon as the DAC has drained enough.
        audioCV.notify_one();
      }
      if (chunk.empty()) continue;

      AAudioStream* stream = nullptr;
      {
        std::lock_guard<std::mutex> lock(audioMutex);
        stream = audioStream;
      }
      // Paused/closed: drop queued audio so stale samples never pop on resume.
      if (!stream || isPausedAtomic.load()) continue;

      s32 total = (s32)chunk.size() / 2;
      s32 written = 0;
      // Blocking write is fine here (dedicated thread); 20ms cap per call so
      // a wedged stream can't hang the thread forever.
      while (written < total) {
        s32 result = AAudioStream_write(stream, chunk.data() + written * 2,
            total - written, 20'000'000);
        if (result > 0) written += result;
        else break; // stream stopped or error: drop the remainder
      }
    }
  }

  static std::mutex windowMutex;
  static bool windowChanged = false;
  static u32 currentWidth = 0;
  static u32 currentHeight = 0;
  static u32 bufferWidth = 0;
  static u32 bufferHeight = 0;
  static std::vector<u32> lastFrameBuffer;

  // Performance Monitoring
  static std::atomic<u64> frameCount{0};
  static std::atomic<u64> lastFrameTime{0};
  static std::atomic<f64> currentFps{0.0};
  static std::atomic<f64> avgFrameTime{0.0};
  static auto lastStatsUpdateTime = std::chrono::steady_clock::now();

  static std::deque<LogEntry> logBuffer;
  static std::mutex logMutex;
  static std::recursive_mutex systemMutex;
  // Guards ONLY core execution (root->run / root->power / serialize).
  // Raw pointer — when the emulation thread is abandoned while holding this
  // mutex, we release() the pointer (leaking the mutex) and allocate a
  // fresh one. Destroying a locked std::recursive_mutex is UB.
  static std::recursive_mutex* runMutex = new std::recursive_mutex();
  static Node::Object cachedPlayer1;

  struct InputState {
    std::atomic<f32> lx{0.0f}, ly{0.0f}, rx{0.0f}, ry{0.0f};
    std::atomic<s32> buttons{0};
  } inputState;

  static u64 nativeInputLogCounter = 0;

  struct VirtualGamepad {
    enum : u32 {
        Up       = 1 << 0,
        Down     = 1 << 1,
        Left     = 1 << 2,
        Right    = 1 << 3,
        A        = 1 << 4,
        B        = 1 << 5,
        X        = 1 << 6,
        Y        = 1 << 7,
        L1       = 1 << 8,
        R1       = 1 << 9,
        L2       = 1 << 10,
        R2       = 1 << 11,
        L3       = 1 << 12,
        R3       = 1 << 13,
        Select   = 1 << 14,
        Start    = 1 << 15,
        Home     = 1 << 16,
        LS_Up    = 1 << 17,
        LS_Down  = 1 << 18,
        LS_Left  = 1 << 19,
        LS_Right = 1 << 20,
        RS_Up    = 1 << 21,
        RS_Down  = 1 << 22,
        RS_Left  = 1 << 23,
        RS_Right = 1 << 24,
    };
  };

  // Bind-once input caches, mirroring ares desktop's InputMapping::bind(): node
  // names are resolved to VirtualGamepad bits / axis slots ONCE per node and
  // cached, so per-read cost is a map lookup instead of repeated string
  // matching on the emulation thread. Caches are cleared on system unload and
  // whenever the WonderSwan orientation mode changes (it remaps button names).
  static std::map<const void*, u32> inputButtonCache;
  static std::map<const void*, u32> inputAxisCache;  // axis slot + 1; 0 = unmapped
  static s32 inputCacheOrientation = -1;

  static auto resolveButtonBit(const string& nodeName, const string& systemName, bool vertical) -> u32 {
      u32 b = 0;

      // Standard D-Pad
      if (nodeName == "Up" || nodeName == "↑") b = VirtualGamepad::Up;
      else if (nodeName == "Down" || nodeName == "↓") b = VirtualGamepad::Down;
      else if (nodeName == "Left" || nodeName == "←") b = VirtualGamepad::Left;
      else if (nodeName == "Right" || nodeName == "→") b = VirtualGamepad::Right;

      // Face Buttons
      else if (nodeName == "A" || nodeName == "Cross" || nodeName == "I" || nodeName == "1" || nodeName == "○") b = VirtualGamepad::A;
      else if (nodeName == "B" || nodeName == "Circle" || nodeName == "II" || nodeName == "2" || nodeName == "×") b = VirtualGamepad::B;
      else if (nodeName == "Fire") b = VirtualGamepad::A;  // Atari 2600 single fire button
      else if (nodeName == "C") b = VirtualGamepad::R1; // Genesis 6-button / 3-button C -> R1
      else if (nodeName == "D") b = VirtualGamepad::R2; // Neo Geo D -> R2
      else if (nodeName == "X" || nodeName == "Square" || nodeName == "III" || nodeName == "□") b = VirtualGamepad::X;
      else if (nodeName == "Y" || nodeName == "Triangle" || nodeName == "IV" || nodeName == "△") b = VirtualGamepad::Y;
      else if (nodeName == "Z") b = VirtualGamepad::R2; // Genesis 6-button Z -> R2

      // Shoulders / Triggers
      else if (nodeName == "L" || nodeName == "L1" || nodeName == "L-Bumper") b = VirtualGamepad::L1;
      else if (nodeName == "R" || nodeName == "R1" || nodeName == "R-Bumper") b = VirtualGamepad::R1;
      else if (nodeName == "L2" || nodeName == "L-Trigger") b = VirtualGamepad::L2;
      else if (nodeName == "R2" || nodeName == "R-Trigger") b = VirtualGamepad::R2;

      // Stick Clicks
      else if (nodeName == "L3" || nodeName == "L-Stick-Click") b = VirtualGamepad::L3;
      else if (nodeName == "R3" || nodeName == "R-Stick-Click") b = VirtualGamepad::R3;

      // System Buttons
      else if (nodeName == "Select" || nodeName == "Mode") b = VirtualGamepad::Select;
      else if (nodeName == "Start" || nodeName == "Run") b = VirtualGamepad::Start;
      else if (nodeName == "Home") b = VirtualGamepad::Home;

      // WonderSwan Specific Names (Horizontal Layout as Default)
      else if (nodeName == "X1") b = vertical ? VirtualGamepad::X : VirtualGamepad::Up;      // Vertical: X, Horizontal: D-Up
      else if (nodeName == "X2") b = vertical ? VirtualGamepad::Y : VirtualGamepad::Right;   // Vertical: Y, Horizontal: D-Right
      else if (nodeName == "X3") b = vertical ? VirtualGamepad::B : VirtualGamepad::Down;    // Vertical: B, Horizontal: D-Down
      else if (nodeName == "X4") b = vertical ? VirtualGamepad::A : VirtualGamepad::Left;    // Vertical: A, Horizontal: D-Left
      else if (nodeName == "Y1") b = vertical ? VirtualGamepad::Left : VirtualGamepad::L1;   // Vertical: D-Left, Horizontal: L1
      else if (nodeName == "Y2") b = vertical ? VirtualGamepad::Up : VirtualGamepad::R1;     // Vertical: D-Up, Horizontal: R1
      else if (nodeName == "Y3") b = vertical ? VirtualGamepad::Right : VirtualGamepad::X;   // Vertical: D-Right, Horizontal: X
      else if (nodeName == "Y4") b = vertical ? VirtualGamepad::Down : VirtualGamepad::Y;    // Vertical: D-Down, Horizontal: Y

      else if (nodeName == "A")  b = vertical ? VirtualGamepad::L1 : VirtualGamepad::B;      // Vertical: L1, Horizontal: B
      else if (nodeName == "B")  b = vertical ? VirtualGamepad::R1 : VirtualGamepad::A;      // Vertical: R1, Horizontal: A

      // Stick-as-Buttons (for Digital mapping to Sticks)
      else if (nodeName == "L-Up") b = VirtualGamepad::LS_Up;
      else if (nodeName == "L-Down") b = VirtualGamepad::LS_Down;
      else if (nodeName == "L-Left") b = VirtualGamepad::LS_Left;
      else if (nodeName == "L-Right") b = VirtualGamepad::LS_Right;
      else if (nodeName == "R-Up") b = VirtualGamepad::RS_Up;
      else if (nodeName == "R-Down") b = VirtualGamepad::RS_Down;
      else if (nodeName == "R-Left") b = VirtualGamepad::RS_Left;
      else if (nodeName == "R-Right") b = VirtualGamepad::RS_Right;

      // Special System Overrides
      if (systemName == "Nintendo 64") {
          if (nodeName == "Z") b = VirtualGamepad::L2;
          else if (nodeName == "C-Up")    b = VirtualGamepad::RS_Up;
          else if (nodeName == "C-Down")  b = VirtualGamepad::RS_Down;
          else if (nodeName == "C-Left")  b = VirtualGamepad::RS_Left;
          else if (nodeName == "C-Right") b = VirtualGamepad::RS_Right;
      } else if (systemName == "PlayStation") {
          // DualShock uses L1, R1, L2, R2, L3, R3 explicitly
          if      (nodeName == "L1") b = VirtualGamepad::L1;
          else if (nodeName == "R1") b = VirtualGamepad::R1;
          else if (nodeName == "L2") b = VirtualGamepad::L2;
          else if (nodeName == "R2") b = VirtualGamepad::R2;
          else if (nodeName == "L3") b = VirtualGamepad::L3;
          else if (nodeName == "R3") b = VirtualGamepad::R3;
      }

      return b;
  }

  static auto resolveAxisSlot(const string& lowerName) -> s32 {
      if (lowerName == "lx" || lowerName == "l-stick x" || lowerName == "left x" || lowerName == "x-axis" || lowerName == "x" || lowerName == "player 1 x-axis") return 0;
      if (lowerName == "ly" || lowerName == "l-stick y" || lowerName == "left y" || lowerName == "y-axis" || lowerName == "y" || lowerName == "player 1 y-axis") return 1;
      if (lowerName == "rx" || lowerName == "r-stick x" || lowerName == "right x" || lowerName == "z-axis" || lowerName == "player 2 x-axis" || lowerName == "z") return 2;
      if (lowerName == "ry" || lowerName == "r-stick y" || lowerName == "right y" || lowerName == "rz-axis" || lowerName == "player 2 y-axis" || lowerName == "rz") return 3;
      return -1;
  }

  static bool muteAudioAtomic = false;
  static bool fastBootAtomic = false;
  static bool autoSaveMemoryAtomic = false;  // "Auto-Save Memory" — disabled by default
  static bool autoLoadMemoryAtomic = false;   // "Auto-Load Memory" — disabled by default
  static s32 regionPreference = 0;
  static std::atomic<s32>  n64UpscaleFactor{1};
  static std::atomic<bool> n64Recompiler{true};
  static std::atomic<bool> n64ExpansionPak{true};
  static std::atomic<bool> n64DisableVIProcessing{false};
  static std::atomic<bool> n64WeaveDeinterlacing{false};
  static std::atomic<bool> n64SupersampleScanout{false};
  static std::atomic<bool> skipBootRom{false};
  static bool ps1AnalogMode = true;
  static bool orientationVertical = false;
  static string customDriverPath;
  static string nativeLibraryDir;
  static string tempFilePath;
  static string homePath;
  static string savesPath;
  static std::map<string, string> firmwareMap;

  auto addLog(LogLevel level, string message) -> void {
    std::lock_guard<std::mutex> lock(logMutex);
    logBuffer.push_back({level, message});
    if (logBuffer.size() > 5000) logBuffer.pop_front();
  }

  static std::atomic<u32> emuThreadGeneration{0};

  auto emulationLoop(u32 generation) -> void {
    #if defined(ANDROID)
    setpriority(PRIO_PROCESS, 0, -10);
    #endif
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    s32 num_cores = sysconf(_SC_NPROCESSORS_CONF);
    for (s32 i = std::max(0, num_cores - 4); i < num_cores; i++) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    // Absolute frame deadline for pacing (see below). Persists across the
    // loop so sleep overshoot never compounds frame-to-frame.
    auto frameDeadline = std::chrono::steady_clock::now();

    while (emulationRunning && emuThreadGeneration == generation) {
      // Take a local shared_ptr copy so the zombie thread holds a
      // reference to the N64 System even after the main thread
      // replaces the global 'root'. Prevents use-after-free in the
      // abandon path.
      auto localRoot = root;

      if (resetRequestedAtomic.exchange(false)) {
        std::lock_guard<std::recursive_mutex> lock(*runMutex);
        if (localRoot) {
            // power(true) = soft reset. Node::System::power() defaults to
            // reset=false, which would take the N64 cold-boot path and
            // destroy/recreate the Vulkan device (poisoning it on Turnip).
            localRoot->power(true);
            addLog(LogLevel::Info, "System reset (async)");
            LOGI("System reset complete (async)");
        }
      }

      if (!isPausedAtomic) {
        auto start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::recursive_mutex> lock(*runMutex);
            if (localRoot) {
                localRoot->run();
            }
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto end = std::chrono::steady_clock::now();

        // Per-core pacing: the cap target is derived from the core's native
        // refresh rate (60/75/50 Hz) instead of a hardcoded 60 FPS, so
        // WonderSwan (~75 Hz) and PAL cores are no longer throttled.
        double refreshRate = refreshRateAtomic.load();

        if (fastForwardAtomic) {
            f64 speed = (f64)ffSpeedLimitAtomic;
            if (speed > 0.0) {
                f64 targetFrameTime = (1000000.0 / refreshRate) / speed;
                auto actualFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                if (actualFrameTime < targetFrameTime) {
                    std::this_thread::sleep_for(std::chrono::microseconds((s64)(targetFrameTime - (f64)actualFrameTime)));
                }
            }
        }

        lastFrameTime = (u64)std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        avgFrameTime = avgFrameTime * 0.9 + (f64)lastFrameTime * 0.1;
        frameCount++;

        // Pacing (non-fast-forward): sync to audio with a video-rate
        // fallback.
        //
        // The old approach slept for the exact remaining budget each frame
        // (sleep_for). On Android that overshoots 1-5ms per call and the
        // overshoot compounds, dragging a 60fps target to ~52. Worse, the
        // sleep drains the CPU/GPU pipeline every frame, so the next run()
        // exposes the full GPU fence latency of the previous frame: a scene
        // that fast-forward (no sleep, pipeline full) runs at 12ms/frame
        // takes 19ms here.
        //
        // Instead: hold an ABSOLUTE frame deadline (sleep_until) so
        // overshoot never compounds, and let the audio ring be the
        // pacemaker when audio is present: wait_until either the ring
        // drains below the target latency (the DAC's real-time clock -
        // hardware-accurate) or the video deadline (for silent moments).
        // Waiting on the AUDIO buffer instead of the GPU keeps the GPU
        // pipeline full, hiding fence latency and preventing underruns.
        if (!fastForwardAtomic) {
          double frameTarget = 1000000.0 / refreshRate;
          frameDeadline += std::chrono::microseconds((s64)frameTarget);
          // ~50ms of stereo floats: the latency we keep buffered to ride
          // out short GPU hitches without underrunning.
          constexpr size_t audioTargetFloats = 48000 * 2 * 50 / 1000;
          std::unique_lock<std::mutex> lock(audioMutex);
          audioCV.wait_until(lock, frameDeadline, [&] {
            return audioRingSize <= audioTargetFloats || audioThreadStop.load();
          });
          // If we fell behind (heavy frames), snap the deadline forward in
          // whole frame periods so we never burst to catch up.
          while (std::chrono::steady_clock::now() > frameDeadline) {
            frameDeadline += std::chrono::microseconds((s64)frameTarget);
          }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatsUpdateTime).count();
        if (elapsed >= 1000) {
            currentFps = (f64)frameCount * 1000.0 / (f64)elapsed;
            LOGI("Emulation Stats: FPS=%.1f, AvgFrameTime=%.2fms", (f64)currentFps, (f64)avgFrameTime / 1000.0);
            frameCount = 0;
            lastStatsUpdateTime = now;
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    LOGI("Emulation thread generation %u exiting", generation);
  }

  static auto ensureThread() -> void {
    if (emuThread && emuThreadRunning) return;
    // Reset abandoned thread state so a fresh emulation thread is created.
    if (emuThread) {
      LOGW("ensureThread: replacing abandoned emulation thread");
      emuThread = 0;
      emuThreadRunning = false;
      runMutex = new std::recursive_mutex();
    }
    emuThreadRunning = true;
    u32 gen = ++emuThreadGeneration;
    pthread_create(&emuThread, nullptr, [](void* arg) -> void* {
        u32 myGen = (u32)(uintptr_t)arg;
        emulationLoop(myGen);
        emuThreadRunning = false;
        return nullptr;
    }, (void*)(uintptr_t)gen);
  }

  auto setEmulationRunning(bool running) -> void {
    if (emulationRunning == running) return;
    emulationRunning = running;
    if (running) ensureThread();
  }

  struct AndroidPlatform : Platform {
    auto attach(Node::Object node) -> void override {
      string name = node->name();
      LOGD("Attach: %s", (const char*)name);
    }

    auto detach(Node::Object node) -> void override {
      string name = node->name();
      LOGD("Detach: %s", (const char*)name);
    }

    auto log(Node::Debugger::Tracer::Tracer tracer, string_view message) -> void override {
      string msg = message;
      addLog(LogLevel::Trace, string{"[Ares Log] ", msg});
    }

    auto event(Event event) -> void override {
      if (event == Event::Power) LOGI("Ares Event: Power");
      if (event == Event::Shutdown) LOGI("Ares Event: Shutdown");
    }

    auto status(string_view message) -> void override {
      string msg = message;
      addLog(LogLevel::Info, string{"[Ares Status] ", msg});
    }

    auto time() -> s64 override {
      return (s64)std::time(nullptr);
    }

    auto refreshRateHint(double refreshRate) -> void override {
      // Called by ares Screen nodes (some cores call it every frame). Ignore
      // garbage values and only log when the rate actually changes so we
      // don't spam logcat for dynamic-rate cores (WonderSwan, Atari 2600).
      if (refreshRate < 20.0 || refreshRate > 240.0) return;
      double prev = refreshRateAtomic.exchange(refreshRate);
      if (std::abs(prev - refreshRate) > 0.5) {
        LOGI("Refresh rate hint: %.2f Hz", refreshRate);
      }
    }

    auto input(Node::Input::Input input) -> void override {
      if (!root) return;
      string systemName = root->name();

      u32 buttons = (u32)inputState.buttons.load();
      f32 lx = inputState.lx.load();
      f32 ly = inputState.ly.load();
      f32 rx = inputState.rx.load();
      f32 ry = inputState.ry.load();

      // Invalidate bind-once caches if the WonderSwan orientation mode changed
      // (it remaps several button names). Cheap bool compare per call.
      if (inputCacheOrientation != (s32)orientationVertical) {
          inputButtonCache.clear();
          inputAxisCache.clear();
          inputCacheOrientation = (s32)orientationVertical;
      }

      if (auto button = input->cast<Node::Input::Button>()) {
          u32 b = 0;
          auto it = inputButtonCache.find(button.get());
          if (it == inputButtonCache.end()) {
              // Bind once, then cache (mirrors ares InputMapping::bind()): names
              // resolve on the first read; per-read is a map lookup + bit test.
              b = resolveButtonBit(button->name(), systemName, orientationVertical);
              inputButtonCache[button.get()] = b;
          } else {
              b = it->second;
          }

          // Always set the value (resetting if not mapped) to ensure state consistency
          button->setValue(b != 0 && (buttons & b) != 0);
      } else if (auto axis = input->cast<Node::Input::Axis>()) {
          s32 slot = -1;
          auto it = inputAxisCache.find(axis.get());
          if (it == inputAxisCache.end()) {
              string nodeName = axis->name();
              string lowerName = nodeName.downcase();
              slot = resolveAxisSlot(lowerName);
              inputAxisCache[axis.get()] = (u32)(slot + 1);  // 0 = unmapped
          } else {
              slot = (s32)it->second - 1;
          }

          s16 value = 0;
          if (slot >= 0) {
              switch (slot) {
                  case 0: value = (s16)(lx * 32767.0f); break;
                  case 1: value = (s16)(ly * 32767.0f); break;
                  case 2: value = (s16)(rx * 32767.0f); break;
                  case 3: value = (s16)(ry * 32767.0f); break;
              }
          }

          // Diagnostic heartbeat only (ares logs nothing per read): every 600th
          // axis read ≈ every 5-10 s at typical PS1 pad poll rates. The old
          // "|| abs(value) > 1000" clause logged EVERY deflected-stick read on
          // the emulation thread — thousands of synchronous logd writes/sec
          // while the stick moved, which stalled emulation and caused lag.
          static u64 axisLogCount = 0;
          if (axisLogCount++ % 600 == 0) {
              LOGI("PhobosNativeInput: System='%s' Axis='%s' Matched=%d Value=%d (lx=%.2f, ly=%.2f)",
                   (const char*)systemName, (const char*)axis->name(), slot >= 0, (int)value, (double)lx, (double)ly);
          }

          if (slot >= 0) {
              axis->setValue(value);
          }
      }
    }

    auto video(Node::Video::Screen screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
      if (width == 0 || height == 0 || isPausedAtomic) return;

      static u64 frameLogCount = 0;
      if (frameLogCount++ % 600 == 0) {
          LOGD("Video: %s, %ux%u, data[0]=%08x", (const char*)screen->name(), width, height, data ? data[0] : 0);
      }

      lock_guard<std::mutex> lock(windowMutex);
      if (!nativeWindow) return;

      if (!firstFrameRendered) firstFrameRendered = true;

      // Special handling for WonderSwan Rotation
      bool rotate = false;
      if (root && root->name().contains("WonderSwan") && orientationVertical) {
          rotate = true;
      }

      // Special handling for N64 Vulkan Direct Scanout
      bool isN64Vulkan = false;
      #if defined(CORE_N64)
      if (root && root->name() == "Nintendo 64" && ::ares::Nintendo64::vulkan.enable) {
          isN64Vulkan = true;
      }
      #endif

      // Determine if we should use 2x scaling for sharpness (disable for N64 Vulkan which handles its own res)
      bool scale2x = (width <= 320) && !rotate && !isN64Vulkan;
      u32 targetW = rotate ? height : (scale2x ? width * 2 : width);
      u32 targetH = rotate ? width : (scale2x ? height * 2 : height);

      if (windowChanged || targetW != bufferWidth || targetH != bufferHeight) {
          ANativeWindow_setBuffersGeometry(nativeWindow, (s32)targetW, (s32)targetH, WINDOW_FORMAT_RGBA_8888);
          bufferWidth = targetW;
          bufferHeight = targetH;
          currentWidth = width;
          currentHeight = height;
          windowChanged = false;
      }

      ANativeWindow_Buffer buffer;
      if (ANativeWindow_lock(nativeWindow, &buffer, nullptr) == 0) {
        auto* dest = (u32*)buffer.bits;
        s32 dst_stride = buffer.stride;

        if (lastFrameBuffer.size() < (u64)width * height) lastFrameBuffer.resize(width * height);

        const u8* vData = nullptr;
        u32 vW = 0, vH = 0;
        #if defined(CORE_N64)
        if (isN64Vulkan) {
            ::ares::Nintendo64::vulkan.mapScanoutRead(vData, vW, vH);
        }
        #endif

        auto colorMap = [&](u32 p) -> u32 {
            u32 colors = screen->colors();
            if (colors > 0 && p < colors) {
                return screen->lookupPalette(p);
            }
            return p;
        };

        if (rotate) {
            if (!data) { ANativeWindow_unlockAndPost(nativeWindow); return; }
            s32 src_stride = pitch / 4;
            for (s32 y = 0; y < (s32)height; y++) {
                const u32* srcLine = data + y * src_stride;
                for (s32 x = 0; x < (s32)width; x++) {
                    u32 p = colorMap(srcLine[x]);
                    u32 ap = 0xFF000000 | ((p << 16) & 0x00FF0000) | (p & 0x0000FF00) | ((p >> 16) & 0x000000FF);
                    // 90 degree clockwise rotation: (x, y) -> (h - 1 - y, x)
                    dest[x * dst_stride + (height - 1 - y)] = ap;
                    lastFrameBuffer[y * width + x] = ap;
                }
            }
        } else if (isN64Vulkan) {
            // Direct SIMD NEON vectorized copy from Vulkan RGBA to Android ABGR.
            // vData can be null when the scanout fence timed out in
            // mapScanoutRead() — but mapScanoutRead() still acquired
            // vulkan.mutex (scanoutLock). unmapScanoutRead() MUST run in ALL
            // cases, otherwise the screen thread leaks vulkan.mutex forever and
            // the emulation thread blocks in scanoutAsync → run() never
            // returns → black screen after reset. This was the N64 reset hang.
            if (vData) {
                u32 copyW = std::min(width, vW);
                u32 copyH = std::min(height, vH);
                for (s32 y = 0; y < (s32)copyH; y++) {
                    const u32* srcLine = (const u32*)(vData + y * vW * 4);
                    u32* destLine = dest + y * dst_stride;
                    s32 x = 0;
                    #if defined(__aarch64__) || defined(__arm__)
                    uint32x4_t alpha = vdupq_n_u32(0xFF000000);
                    for (; x <= (s32)copyW - 4; x += 4) {
                        uint32x4_t p = vld1q_u32(srcLine + x);
                        uint32x4_t result = vorrq_u32(alpha, p);
                        vst1q_u32(destLine + x, result);
                    }
                    #endif
                    for (; x < (s32)copyW; x++) {
                        destLine[x] = 0xFF000000 | srcLine[x];
                    }
                }
            }
            ::ares::Nintendo64::vulkan.unmapScanoutRead();
        } else if (scale2x) {
            if (!data) { ANativeWindow_unlockAndPost(nativeWindow); return; }
            s32 src_stride = pitch / 4;
            for (s32 y = 0; y < (s32)height; y++) {
                const u32* srcLine = data + y * src_stride;
                u32* destLine1 = dest + (y * 2) * dst_stride;
                u32* destLine2 = dest + (y * 2 + 1) * dst_stride;
                u32* saveLine = lastFrameBuffer.data() + y * width;

                for (s32 x = 0; x < (s32)width; x++) {
                    u32 p = colorMap(srcLine[x]);
                    u32 ap = 0xFF000000 | ((p << 16) & 0x00FF0000) | (p & 0x0000FF00) | ((p >> 16) & 0x000000FF);
                    destLine1[x * 2] = ap;
                    destLine1[x * 2 + 1] = ap;
                    destLine2[x * 2] = ap;
                    destLine2[x * 2 + 1] = ap;
                    saveLine[x] = ap;
                }
            }
        } else {
            if (!data) { ANativeWindow_unlockAndPost(nativeWindow); return; }
            s32 src_stride = pitch / 4;
            for (s32 y = 0; y < (s32)height; y++) {
                const u32* srcLine = data + y * src_stride;
                u32* destLine = dest + y * dst_stride;
                u32* saveLine = lastFrameBuffer.data() + y * width;
                for (s32 x = 0; x < (s32)width; x++) {
                    u32 p = colorMap(srcLine[x]);
                    u32 ap = 0xFF000000 | ((p << 16) & 0x00FF0000) | (p & 0x0000FF00) | ((p >> 16) & 0x000000FF);
                    destLine[x] = ap;
                    saveLine[x] = ap;
                }
            }
        }
        ANativeWindow_unlockAndPost(nativeWindow);
      }
    }

    auto audio(Node::Audio::Stream stream) -> void override {
      if (isPausedAtomic) return;

      {
        std::lock_guard<std::mutex> lock(audioMutex);
        if (!audioStream) {
          AAudioStreamBuilder* builder;
          AAudio_createStreamBuilder(&builder);
          AAudioStreamBuilder_setSampleRate(builder, 48000);
          AAudioStreamBuilder_setChannelCount(builder, 2);
          AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
          AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
          AAudioStreamBuilder_openStream(builder, &audioStream);
          AAudioStreamBuilder_delete(builder);
          if (audioStream) {
            // 32 bursts (~6144 frames at 48k = ~128ms): holds several frames
            // of output and rides out short stalls without underrunning.
            s32 burst = AAudioStream_getFramesPerBurst(audioStream);
            s32 bufferFrames = burst * 32;
            AAudioStream_setBufferSizeInFrames(audioStream, bufferFrames);
            // Prime with silence so the first emulated frames have headroom.
            std::vector<f32> silence((size_t)bufferFrames * 2, 0.0f);
            s64 written = 0;
            while (written < bufferFrames) {
                s32 n = AAudioStream_write(audioStream, silence.data() + written * 2,
                    (s32)(bufferFrames - written), 0);
                if (n <= 0) break;
                written += n;
            }
            AAudioStream_requestStart(audioStream);
          }
          if (!audioThreadRunning.load()) {
            audioThreadStop.store(false);
            audioThread = std::thread(audioThreadMain);
            audioThreadRunning.store(true);
          }
        }
      }

      // Push samples into the audio ring buffer (O(1), fixed ~125ms cap).
      // Never blocks; oldest samples are overwritten when the emulator
      // out-produces the DAC so a fast-forward burst can't leave a backlog.
      if (audioStream) {
        thread_local std::vector<f32> localBuffer;
        localBuffer.clear();

        f64 samples[2];
        while (stream->pending()) {
          stream->read(samples);
          localBuffer.push_back((f32)samples[0]);
          localBuffer.push_back((f32)samples[1]);
        }

        if (!localBuffer.empty()) {
            if (muteAudioAtomic) {
                std::fill(localBuffer.begin(), localBuffer.end(), 0.0f);
            }
            std::lock_guard<std::mutex> lock(audioMutex);
            if (audioRing.empty()) audioRing.resize(audioRingCapacity);
            for (f32 s : localBuffer) {
              audioRing[(audioRingHead + audioRingSize) % audioRingCapacity] = s;
              if (audioRingSize < audioRingCapacity) audioRingSize++;
              else audioRingHead = (audioRingHead + 1) % audioRingCapacity; // overwrite oldest
            }
            audioCV.notify_one();
        }
      }
    }

    auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
      if (!node) return std::make_shared<vfs::directory>();
      string nodeName = node->name();
      LOGI("VFS: pak() requested for node: %s", (const char*)nodeName);

      if (nodeName.endsWith("Cartridge") || nodeName.endsWith("Disc") || nodeName.endsWith("Card")) {
        if (currentMedium && currentMedium->pak) {
            LOGI("VFS: Returning currentMedium pak for %s", (const char*)nodeName);
            return currentMedium->pak;
        }
        LOGW("VFS: No currentMedium pak available for %s", (const char*)nodeName);
      }

      if (nodeName.endsWith("Disk") || nodeName.endsWith("Expansion")) {
        if (secondaryMedium && secondaryMedium->pak) {
            LOGI("VFS: Returning secondaryMedium pak for %s", (const char*)nodeName);
            return secondaryMedium->pak;
        }
        LOGW("VFS: No secondaryMedium pak available for %s", (const char*)nodeName);
      }

      auto dir = std::make_shared<vfs::directory>();
      dir->setAttribute("name", nodeName);
      dir->setAttribute("title", "Phobos Game");
      dir->setAttribute("region", "NTSC-U");

      string systemPath = string{homePath, "/System/", nodeName, "/"};

      auto attachFile = [&](string fileName, string vfsName = "") {
          if (!vfsName) vfsName = fileName;
          string filePath;
          if (fileName.find("/")) filePath = fileName;
          else {
              filePath = string{systemPath, fileName};
              // Robustness: handle potential double slashes or missing slashes
              if (systemPath.endsWith("/") && fileName.beginsWith("/")) {
                  filePath = string{systemPath.slice(0, -1), fileName};
              } else if (!systemPath.endsWith("/") && !fileName.beginsWith("/")) {
                  filePath = string{systemPath, "/", fileName};
              }
          }

          auto data = nall::file::read(filePath);
          if (data.size()) {
              if (auto fp = vfs::memory::open(data)) {
                  dir->append(vfsName, fp);
                  LOGI("VFS: Attached %s to %s system pak (Size: %zu)", (const char*)fileName, (const char*)nodeName, data.size());
                  return true;
              }
          }
          LOGW("VFS: Failed to attach %s to %s (Expected Path: %s)", (const char*)fileName, (const char*)nodeName, (const char*)filePath);
          return false;
      };

      if (nodeName == "Super Famicom") {
          attachFile("boards.bml");
          attachFile("ipl.rom");
      } else if (nodeName == "ColecoVision") {
          bool attached = false;
          auto it_cv = firmwareMap.find("fw_coleco");
          if (it_cv != firmwareMap.end()) attached = attachFile((const char*)it_cv->second, "bios.rom");
          if (!attached) attachFile("bios.rom");
      } else if (nodeName == "Famicom") {
          attachFile("boards.bml");
      } else if (nodeName == "PlayStation") {
          bool attached = false;
          auto it_us = firmwareMap.find("fw_psx_us");
          if (it_us != firmwareMap.end()) attached = attachFile((const char*)it_us->second, "bios.rom");
          if (!attached) {
              auto it_jp = firmwareMap.find("fw_psx_jp");
              if (it_jp != firmwareMap.end()) attached = attachFile((const char*)it_jp->second, "bios.rom");
          }
          if (!attached) {
              auto it_eu = firmwareMap.find("fw_psx_eu");
              if (it_eu != firmwareMap.end()) attached = attachFile((const char*)it_eu->second, "bios.rom");
          }
          if (!attached) attached = attachFile("bios.rom");
      } else if (nodeName == "Sega Saturn") {
          bool attached = false;
          auto it_us = firmwareMap.find("fw_saturn_us");
          if (it_us != firmwareMap.end()) attached = attachFile((const char*)it_us->second, "bios.rom");
          if (!attached) {
              auto it_jp = firmwareMap.find("fw_saturn_jp");
              if (it_jp != firmwareMap.end()) attached = attachFile((const char*)it_jp->second, "bios.rom");
          }
          if (!attached) {
              auto it_eu = firmwareMap.find("fw_saturn_eu");
              if (it_eu != firmwareMap.end()) attached = attachFile((const char*)it_eu->second, "bios.rom");
          }
          if (!attached) attached = attachFile("bios.rom");
      } else if (nodeName == "Mega Drive") {
          // Mega CD: ares uses "Mega Drive" as root node even for CD mode.
          // The MCD::load() sub-system reads "bios.rom" from this pak — if
          // omitted the sub-68000 runs from zeroed RAM (black screen, audio only).
          bool attached = false;
          auto it_us = firmwareMap.find("fw_mcd_us");
          if (it_us != firmwareMap.end()) attached = attachFile((const char*)it_us->second, "bios.rom");
          if (!attached) {
              auto it_jp = firmwareMap.find("fw_mcd_jp");
              if (it_jp != firmwareMap.end()) attached = attachFile((const char*)it_jp->second, "bios.rom");
          }
          if (!attached) {
              auto it_eu = firmwareMap.find("fw_mcd_eu");
              if (it_eu != firmwareMap.end()) attached = attachFile((const char*)it_eu->second, "bios.rom");
          }
          if (!attached) attachFile("bios.rom");
      } else if (nodeName == "Neo Geo AES" || nodeName == "Neo Geo MVS") {
          // neogeo.zip is copied to mia_temp. Extract BIOS + fix-layer ROM.
          string zipPath = string{tempFilePath, "/neogeo.zip"};
          bool haveBios = false, haveStatic = false;
          if (file::exists(zipPath)) {
            Decode::ZIP zip;
            if (zip.open(zipPath)) {
              for (auto& zf : zip.file) {
                string n = zf.name.downcase();
                // UniBIOS first (most forgiving, handles MVS/AES auto-detect),
                // then MVS BIOS variants (sp-e, sp-j2, sp-u2, sp1-u2),
                // then sp-s2.sp1 (universal AES) as last resort.
                if (!haveBios && n.beginsWith("uni-bios")) {
                  auto data = zip.extract(zf);
                  if (data.size() == 131072) {
                    if (auto fp = vfs::memory::open(data)) {
                      dir->append("bios.rom", fp); haveBios = true;
                      LOGI("VFS: Neo Geo BIOS: UniBIOS (%s)", (const char*)zf.name);
                    }
                  }
                }
                if (!haveBios && (n.equals("sp-e.sp1") || n.equals("sp-j2.sp1") || n.equals("sp-u2.sp1") || n.equals("sp1-u2") || n.equals("sp1-u3.bin") || n.equals("sp1-u4.bin"))) {
                  auto data = zip.extract(zf);
                  if (data.size() == 131072) {
                    if (auto fp = vfs::memory::open(data)) {
                      dir->append("bios.rom", fp); haveBios = true;
                      LOGI("VFS: Neo Geo BIOS: MVS (%s)", (const char*)zf.name);
                    }
                  }
                }
                if (!haveBios && n.equals("sp-s2.sp1")) {
                  auto data = zip.extract(zf);
                  if (data.size() == 131072) {
                    if (auto fp = vfs::memory::open(data)) {
                      dir->append("bios.rom", fp); haveBios = true;
                      LOGI("VFS: Neo Geo BIOS: AES universal (sp-s2.sp1)");
                    }
                  }
                }
                // sfix.sfix = 131KB BIOS fix-layer font. Only needed for
                // AES (home console). MVS arcade boards get fix ROM from
                // the cartridge itself — attaching a BIOS font can conflict.
                if (!haveStatic && n.iequals("sfix.sfix")) {
                  // Skip for now — MVS doesn't need system-pak static.rom.
                }
              }
            }
          }
          if (!haveBios) attachFile("bios.rom");
          if (!haveStatic) attachFile("static.rom");
      } else if (nodeName == "Neo Geo CD" || nodeName == "Neo Geo CDZ") {
          bool attached = false;
          auto it_ngcd = firmwareMap.find("fw_ng_cd");
          if (it_ngcd != firmwareMap.end()) attached = attachFile((const char*)it_ngcd->second, "bios.rom");
          if (!attached) attached = attachFile("bios.rom");
      } else if (nodeName == "Nintendo 64") {
          bool attached = false;
          auto it_ntsc = firmwareMap.find("fw_n64_pif_ntsc");
          if (it_ntsc != firmwareMap.end()) attached = attachFile((const char*)it_ntsc->second, "pif.ntsc.rom");
          if (!attached) {
              auto it_pal = firmwareMap.find("fw_n64_pif_pal");
              if (it_pal != firmwareMap.end()) attached = attachFile((const char*)it_pal->second, "pif.pal.rom");
          }
          if (!attached) attached = attachFile("pif.ntsc.rom");
          if (!attached) attached = attachFile("pif.pal.rom");
          if (!attached) LOGE("VFS: FAILED to attach PIF for Nintendo 64!");
      } else if (nodeName == "Neo Geo Pocket" || nodeName == "Neo Geo Pocket Color") {
          // NGP/NGPC needs bios.rom for TLCS900H CPU boot vector + KGE init.
          // Without it CPU reads 0x00 (NOP-loop) → white/black screen forever.
          bool attached = false;
          auto it = firmwareMap.find(nodeName == "Neo Geo Pocket Color" ? "fw_ngpc" : "fw_ngp");
          if (it != firmwareMap.end()) attached = attachFile((const char*)it->second, "bios.rom");
          if (!attached) attachFile("bios.rom");
      } else if (nodeName == "Game Boy Advance") {
          auto it_gba = firmwareMap.find("fw_gba");
          if (it_gba != firmwareMap.end()) attachFile((const char*)it_gba->second, "bios.rom");
          else attachFile("bios.rom");
      } else if (nodeName == "Game Boy") {
          bool attached = false;
          auto it_gb = firmwareMap.find("fw_gb_boot");
          if (it_gb != firmwareMap.end()) attached = attachFile((const char*)it_gb->second, "boot.rom");
          if (!attached) attached = attachFile("boot.dmg-0.rom", "boot.rom");
      } else if (nodeName == "Game Boy Color") {
          bool attached = false;
          auto it_gbc = firmwareMap.find("fw_gbc_boot");
          if (it_gbc != firmwareMap.end()) attached = attachFile((const char*)it_gbc->second, "boot.rom");
          if (!attached) attached = attachFile("boot.cgb-0.rom", "boot.rom");
      } else if (nodeName == "WonderSwan" || nodeName == "WonderSwan Color") {
          if (!skipBootRom) attachFile("boot.rom");
      } else if (nodeName == "MSX" || nodeName == "MSX2") {
          attachFile("bios.rom");
          if (nodeName == "MSX2") attachFile("sub.rom");
      } else if (nodeName == "PC Engine" || nodeName == "SuperGrafx" || nodeName == "PC Engine Duo" || nodeName == "PC Engine CD") {
          bool attached = false;
          auto it_pce = firmwareMap.find("fw_pce_cd_3_jp");
          if (it_pce != firmwareMap.end()) attached = attachFile((const char*)it_pce->second, "bios.rom");
          if (!attached) {
              auto it_ge = firmwareMap.find("fw_pce_cd_ge_jp");
              if (it_ge != firmwareMap.end()) attached = attachFile((const char*)it_ge->second, "bios.rom");
          }
          if (!attached) attached = attachFile("bios.rom");
      }

      return dir;
    }
  };

  static AndroidPlatform androidPlatform;
  Platform* platform = &androidPlatform;

  auto unloadSystem() -> void {
    isPausedAtomic = true;
    fastForwardAtomic = false;

    // Stop the audio thread FIRST (it may be mid-write to audioStream),
    // then stop/close the stream. Leaving the stream draining while the menu
    // shows causes continuous underruns → pops on every exit and load.
    if (audioThreadRunning.load()) {
      {
        std::lock_guard<std::mutex> lock(audioMutex);
        audioThreadStop.store(true);
      }
      audioCV.notify_one();
      if (audioThread.joinable()) audioThread.join();
      audioThreadRunning.store(false);
    }
    {
      std::lock_guard<std::mutex> lock(audioMutex);
      if (audioStream) {
        AAudioStream_requestStop(audioStream);
        AAudioStream_close(audioStream);
        audioStream = nullptr;
      }
    }

    // Tell the emulation thread to stop and wait for it to release runMutex.
    // We hold the lock briefly just to verify the thread has released it;
    // then we unlock and proceed with the full unload under systemMutex.
    setEmulationRunning(false);

    bool acquired = false;
    if (!emuThreadRunning) {
      // No thread running — safe to grab the mutex immediately.
      runMutex->lock();
      acquired = true;
    } else {
      // Wait up to 2 seconds for the thread to finish its current frame.
      for (int i = 0; i < 200; i++) {
        if (runMutex->try_lock()) { acquired = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    if (!acquired) {
      // Thread is stuck inside root->run(). The zombie holds a
      // localRoot reference.
      LOGI("unloadSystem: emulation thread stuck, abandoning system");

      // IMPORTANT: do NOT set Vulkan::discardPipelineCache here. The zombie
      // never destroys the system while it is stuck, so the flag never serves
      // its intended purpose (arming skip-idle teardown) — it only poisons
      // the NEXT N64 load by clearing the in-memory cache before the disk
      // cache is read, forcing a full shader-recompile storm (sync GPU
      // stalls of 10-500ms per pipeline, killing FPS and making fast-forward
      // useless until the cache re-warms). A stuck non-N64 core (e.g. PC
      // Engine) taking this path destroyed the user's warm N64 cache exactly
      // this way. Real wedge handling belongs in Vulkan::unload()'s bounded
      // scanout-fence check, which arms skip_idle_on_destroy only when the
      // GPU is actually unresponsive.
      // Similarly, skipCachePersist must not be set here: it would prevent
      // the next normal unload from persisting newly-compiled pipelines.

      std::lock_guard<std::recursive_mutex> lock(systemMutex);

      // Orphan the current system and its runner mutex. We leak the
      // mutex pointers because the zombie thread may still hold locks.
      // We clear 'root' so no new calls use the old system.
      // The shared_ptr ref in the zombie thread's 'localRoot' keeps it
      // alive until (if ever) it exits its loop iteration.
      root = {};
      runMutex = new std::recursive_mutex();
      emuThreadGeneration.fetch_add(1);

      cachedPlayer1 = {};
      inputButtonCache.clear();
      inputAxisCache.clear();
      inputCacheOrientation = -1;
      currentMedium.reset();
      secondaryMedium.reset();
      emuThread = 0;
      emuThreadRunning = false;
      LOGI("System abandoned (thread stuck)");
      return;
    }
    // Thread exited cleanly — we hold runMutex. Release it and unload normally.
    runMutex->unlock();

    std::lock_guard<std::recursive_mutex> lock(systemMutex);
    if (root) {
        if (savesPath && currentMedium && currentMedium->pak) {
          string sysName = root->name();
          string saveDir = {savesPath, "/", sysName, "/"};
          directory::create(saveDir);
          for (auto& saveNode : currentMedium->pak->files()) {
            string fileName = saveNode->name();
            if (!fileName.endsWith(".ram") && !fileName.endsWith(".srm") &&
                !fileName.endsWith(".eeprom") && !fileName.endsWith(".card") &&
                !fileName.endsWith(".sav") && !fileName.endsWith(".fla")) continue;
            auto fp = saveNode;
            fp->seek(0);
            auto size = fp->size();
            if (size == 0) continue;
            std::vector<u8> buf(size);
            fp->read({buf.data(), size});
            bool allZero = true;
            for (auto b : buf) { if (b != 0) { allZero = false; break; } }
            if (allZero) continue;
            string fullPath = {saveDir, fileName};
            file::write(fullPath, {buf.data(), size});
            LOGI("Saves: exported %s (%zu bytes) for %s", (const char*)fileName, size, (const char*)sysName);
          }
          // MIA-level sidecar saves (.sav, .flash for NGP/NGPC/WonderSwan):
          // stored on disk alongside the ROM, not in the VFS pak. Copy them
          // to the persistent saves directory so they survive mia_temp cleanup.
          for (auto& saveName : {"program.sav", "program.flash"}) {
            string sidecarPath = string{tempFilePath, "/", saveName};
            auto data = file::read(sidecarPath);
            if (data.size() == 0) continue;
            bool allZero = true;
            for (auto b : data) { if (b != 0) { allZero = false; break; } }
            if (allZero) continue;
            string fullPath = {saveDir, saveName};
            file::write(fullPath, data);
            LOGI("Saves: exported MIA sidecar %s (%zu bytes) for %s", saveName, data.size(), (const char*)sysName);
          }
        }
        // Export NGP/NGPC CPU RAM + BIOS (settings region)
        // root->save() flushes CPU::save() which updates the 12KB ram array.
        // Read it directly — the VFS roundtrip is unreliable.
        if (savesPath && (root->name() == "Neo Geo Pocket" || root->name() == "Neo Geo Pocket Color")) {
          root->save();
          string saveDir = {savesPath, "/", root->name(), "/"};
          directory::create(saveDir);
          // CPU RAM (12KB)
          auto& ram = ares::NeoGeoPocket::cpu.ram;
          if (ram.size() == 12_KiB) {
            std::vector<u8> buf(12_KiB);
            memcpy(buf.data(), ram.data(), 12_KiB);
            file::write({saveDir, "cpu.ram"}, buf);
            LOGI("Saves: exported cpu.ram (12KB) for %s", (const char*)root->name());
          }
          // BIOS (64KB) — language/date settings live in top 2KB EEPROM region
          auto& bios = ares::NeoGeoPocket::system.bios;
          if (bios.size() == 64_KiB) {
            std::vector<u8> buf(64_KiB);
            memcpy(buf.data(), bios.data(), 64_KiB);
            file::write({saveDir, "bios.rom"}, buf);
            LOGI("Saves: exported bios.rom (64KB) for %s", (const char*)root->name());
          }
        }
        root->unload();
        root.reset();
    }
    cachedPlayer1 = {};
    inputButtonCache.clear();
    inputAxisCache.clear();
    inputCacheOrientation = -1;
    currentMedium.reset();
    secondaryMedium.reset();
    LOGI("System unloaded");
  }

  static auto connectDevices(Node::Object node) -> void {
    if (!node) return;
    LOGI("VFS: connectDevices for system '%s'", (const char*)node->name());
    auto ports = node->find<Node::Port>();
    s32 portIndex = 1;

    for (auto& port : ports) {
      LOGI("VFS: connectDevices - name='%s', type='%s', family='%s'", (const char*)port->name(), (const char*)port->type(), (const char*)port->family());
      // MSX Tape/Tray: skip entirely (ares manages them, our touch crashes).
      if (port->type() == "Tape" || port->type() == "Tray" || port->type() == "Tape Deck") {
          continue;
      }
      // Disc Tray: connect for all disc-based systems. Only skip for
      // PC Engine / SuperGrafx HuCard games (name != "CD" / "Duo").
      if (port->name() == "Disc Tray") {
          string sysName = node ? node->name() : "";
          // HuCard-only PCE: skip tray connect to prevent feeding ROM as CD.
          bool isHuCard = (sysName == "PC Engine" || sysName == "SuperGrafx");
          if (isHuCard) { portIndex++; continue; }
          if (port->allocate()) {
              LOGI("VFS: Connecting Disc Tray for '%s'", (const char*)sysName);
              port->connect();
          }
          portIndex++;
          continue;
      }
      if (port->type() == "Cartridge" || port->type() == "Compact Disc" || port->type() == "Disk Drive") {
        if (port->allocate()) {
            LOGI("VFS: Allocated %s port", (const char*)port->type());
            port->connect();
            LOGI("VFS: Successfully called port->connect() for %s", (const char*)port->type());
        } else {
            LOGE("VFS: FAILED to allocate %s port", (const char*)port->type());
        }
      } else if (port->type() == "Memory Card") {
        // PS1 memory-card ports must get a Memory Card, NOT a controller. The
        // controller branch below matches ports by name contains("Port"), which
        // would otherwise hijack "Memory Card Port 1/2" and connect Digital
        // Gamepads there — corrupting the SIO bus routing (memcards only wake on
        // 0x81, gamepads wake on 0x01, so a stray gamepad can answer controller
        // polls) and silently breaking memcard saves.
        string defaultDevice = "Memory Card";
        auto currentConnected = port->connected();
        if (currentConnected && currentConnected->name() == defaultDevice) {
            LOGI("VFS: Port %s already connected to %s", (const char*)port->name(), (const char*)defaultDevice);
            portIndex++;
            continue;
        }
        if (port->connected()) port->disconnect();
        if (auto pNode = port->allocate(defaultDevice)) {
            LOGI("VFS: Allocated %s on %s", (const char*)defaultDevice, (const char*)port->name());
            port->connect();
            LOGI("VFS: Connected %s on %s", (const char*)defaultDevice, (const char*)port->name());
        } else {
            LOGE("VFS: FAILED to allocate %s on %s (Family: '%s', Sys: '%s')", (const char*)defaultDevice, (const char*)port->name(), (const char*)port->family(), (const char*)(node ? node->name() : ""));
        }
        portIndex++;
      } else if (port->type() == "Controller" || port->type() == "Control Pad" || port->name().contains("Controller") || port->name().contains("Port")) {
        string defaultDevice = "Gamepad";
        string family = port->family();
        string sysName = node ? node->name() : "";

        if (family.find("Nintendo 64") || sysName.find("Nintendo 64")) defaultDevice = (sysName == "Arcade") ? "Aleck64" : "Gamepad";
        else if (family.find("Super Famicom") || sysName.find("Super Famicom") || sysName.find("SNES")) defaultDevice = "Gamepad";
        else if (family.find("Mega Drive") || sysName.find("Mega Drive") || sysName.find("Genesis") || sysName.find("Mega CD") || sysName.find("Sega CD")) {
            if (port->name().find("Extension")) defaultDevice = ""; // Extension port doesn't take gamepad
            else defaultDevice = "Fighting Pad";
        }
        else if (family.find("MSX") || sysName.find("MSX")) defaultDevice = "Gamepad";
        else if (sysName.find("PlayStation")) {
            // Always allocate a DualShock on Port 1: the runtime analog toggle
            // Respect ps1AnalogMode: DualShock when analog is on, Digital
            // Gamepad when off. The hotkey toggle flips ps1AnalogMode and
            // calls connectDevices(root) which re-allocates the port.
            // Use port name (not portIndex) — Disc Tray bumps the counter.
            {
                bool isPort1 = (port->name() == "Controller Port 1");
                defaultDevice = isPort1 ? (ps1AnalogMode ? "DualShock" : "Digital Gamepad") : "Digital Gamepad";
            }
        }
        else if (family.find("Neo Geo") || sysName.find("Neo Geo")) defaultDevice = "Arcade Stick";
        else if (family.find("PC Engine") || sysName.find("PC Engine") || sysName.find("SuperGrafx")) defaultDevice = "Gamepad";
        else if (family.find("Atari 2600") || sysName.find("Atari 2600")) defaultDevice = "Gamepad";
        else if (family.find("ColecoVision") || sysName.find("ColecoVision")) defaultDevice = "Gamepad";

        if (!defaultDevice) { portIndex++; continue; }

        // Check if port is already connected to the desired device
        auto currentConnected = port->connected();
        if (currentConnected && currentConnected->name() == defaultDevice) {
            LOGI("VFS: Port %s already connected to %s", (const char*)port->name(), (const char*)defaultDevice);
            portIndex++;
            continue;
        }

        if (port->connected()) port->disconnect();

        if (auto pNode = port->allocate(defaultDevice)) {
            LOGI("VFS: Allocated %s controller on %s", (const char*)defaultDevice, (const char*)port->name());
            port->connect();
            LOGI("VFS: Connected %s on %s", (const char*)defaultDevice, (const char*)port->name());

            if (portIndex == 1) {
                cachedPlayer1 = pNode;
                LOGI("VFS: Cached Player 1 Peripheral: %s", (const char*)cachedPlayer1->name());
            }
        } else {
            LOGE("VFS: FAILED to allocate %s on %s (Family: '%s', Sys: '%s')", (const char*)defaultDevice, (const char*)port->name(), (const char*)family, (const char*)sysName);
        }
        portIndex++;
      }
else if (port->type() == "Keyboard") {
        string defaultLayout = "Japanese";
        if (port->allocate(defaultLayout)) {
            LOGI("VFS: Allocated %s keyboard on %s", (const char*)defaultLayout, (const char*)port->name());
        }
      }
    }
  }

  auto initialize(const char* systemNamePtr, const char* uriPtr, const char* romNamePtr) -> bool {
    string systemName = systemNamePtr;
    string uri = uriPtr;
    string romName = romNamePtr ? romNamePtr : "";
    unloadSystem();

    std::unique_lock<std::recursive_mutex> lock(systemMutex);

    isPausedAtomic = false;
    firstFrameRendered = false;

    scheduler.reset();

    LOGI("Initializing system: %s, uri: %s", (const char*)systemName, (const char*)uri);

    if (customDriverPath) {
        LOGI("adrenotools: Attempting load. NativeLibDir: %s, DriverPath: %s, RedirectDir: %s", (const char*)nativeLibraryDir, (const char*)customDriverPath, (const char*)tempFilePath);

        maybe<u32> lastSlash = customDriverPath.findPrevious(customDriverPath.size(), "/");
        string driverDir = lastSlash ? customDriverPath.slice(0, *lastSlash + 1) : "";
        string driverFile = lastSlash ? customDriverPath.slice(*lastSlash + 1) : customDriverPath;

        // HACK: Some drivers expect libvulkan.so.1, but Android only provides libvulkan.so
        string libVulkan1 = string{tempFilePath, "/libvulkan.so.1"};
        if (access((const char*)libVulkan1, F_OK) == -1) {
            symlink("/system/lib64/libvulkan.so", (const char*)libVulkan1);
            LOGI("adrenotools: Created symlink libvulkan.so.1 -> /system/lib64/libvulkan.so");
        }

        // Flags: CUSTOM (1) | FILE_REDIRECT (2) = 3
        void* vulkanModule = adrenotools_open_libvulkan(RTLD_NOW, 3, nullptr, (const char*)nativeLibraryDir, (const char*)driverDir, (const char*)driverFile, (const char*)tempFilePath, nullptr);

        if (vulkanModule) {
            auto gipa = (PFN_vkGetInstanceProcAddr)dlsym(vulkanModule, "vkGetInstanceProcAddr");
            if (gipa) {
                LOGI("adrenotools: Successfully resolved vkGetInstanceProcAddr");
                ::Vulkan::Context::init_loader(gipa, true);
            } else {
                LOGE("adrenotools: Failed to resolve vkGetInstanceProcAddr from custom driver! dlerror: %s", dlerror());
                ::Vulkan::Context::init_loader(nullptr, true);
            }
        } else {
            LOGE("adrenotools: Failed to load custom driver! dlerror: %s", dlerror());
            ::Vulkan::Context::init_loader(nullptr, true);
        }
    } else {
        LOGI("Environment: Using system default Vulkan driver");
        ::Vulkan::Context::init_loader(nullptr, true);
    }

    if (romFd == -1) return false;
    struct stat st;
    if(fstat(romFd, &st) != 0) return false;

    string extension = "bin";
    if(auto position = uri.findPrevious(uri.size(), ".")) {
        extension = uri.slice(*position + 1).downcase();
        if(auto paramStart = extension.find("?")) extension = extension.slice(0, *paramStart);
        if(auto paramStart = extension.find("&")) extension = extension.slice(0, *paramStart);
    }

    if (!tempFilePath) return false;
    // For MAME/arcade systems, MIA needs the ROM filename for database
    // lookup (manifestDatabaseArcade). Use the library's RomFile.name
    // which is a clean filename — the URI is encoded and unusable here.
    string tempFname = "phobos_rom_temp";
    if (systemName.contains("Neo Geo")) {
      if (romName.size() > 0) {
        tempFname = romName;
        if (auto dot = tempFname.find(".")) tempFname = tempFname.slice(0, *dot);
      }
    }
    string tempPath = string{tempFilePath, "/", tempFname, ".", extension};

    FILE* f = fopen((const char*)tempPath, "wb");
    if (!f) return false;
    std::vector<u8> copyBuf;
    copyBuf.resize(1024 * 1024);
    lseek(romFd, 0, SEEK_SET);
    while (true) {
        ssize_t r = read(romFd, copyBuf.data(), copyBuf.size());
        if (r <= 0) break;
        fwrite(copyBuf.data(), 1, r, f);
    }
    fclose(f);

    string loadPath = tempPath;
    string identifiedSystem = systemName;
    if (systemName == "Auto" || systemName == "Nintendo 64") {
      auto matches = mia::identify(loadPath);
      if (!matches.empty()) {
          LOGI("MIA: Identified system as %s", (const char*)matches[0]);
          identifiedSystem = matches[0];
      }
    }

    string lookup = identifiedSystem;
    LOGI("MIA: Identified system: '%s', lookup: '%s'", (const char*)identifiedSystem, (const char*)lookup);
    bool forceZipLoad = false;
    if (lookup.find("Nintendo 64")) {
        if (lookup != "Nintendo 64DD") identifiedSystem = "Nintendo 64";
    }
    else if (lookup.find("Atari 2600") || lookup.find("A26") || lookup.find("Atari2600") || lookup.find("Stella")) identifiedSystem = "Atari 2600";
    else if (lookup.find("ColecoVision") || lookup.find("Coleco") || lookup.find("CV")) identifiedSystem = "ColecoVision";
    else if (lookup.find("SG-1000") || lookup.find("SG1000") || lookup.find("SC-3000")) identifiedSystem = "SG-1000";
    else if (lookup.find("ZX Spectrum") || lookup.find("ZXSpectrum") || lookup.find("ZX") || lookup.find("Spectrum 128")) identifiedSystem = "ZX Spectrum";
    else if (lookup.find("Super Famicom") || lookup.find("SNES")) identifiedSystem = "Super Famicom";
    else if (lookup.find("Famicom") || lookup.find("NES")) identifiedSystem = "Famicom";
    else if (lookup.find("PlayStation") || lookup.find("PS1")) identifiedSystem = "PlayStation";
    else if (lookup.find("Neo Geo Pocket Color") || lookup.find("NGPC") || lookup.find("NGC")) identifiedSystem = "Neo Geo Pocket Color";
    else if (lookup.find("Neo Geo Pocket") || lookup.find("NGP") || lookup.find("NGP ")) identifiedSystem = "Neo Geo Pocket";
    else if (lookup.find("Neo Geo")) {
        identifiedSystem = "Neo Geo";
        forceZipLoad = true;
    }
    else if (lookup.find("Mega Drive") || lookup.find("Genesis")) identifiedSystem = "Mega Drive";
    else if (lookup.find("Master System")) identifiedSystem = "Master System";
    else if (lookup.find("Game Gear")) identifiedSystem = "Game Gear";
    else if (lookup.find("Game Boy Advance")) identifiedSystem = "Game Boy Advance";
    else if (lookup.find("Game Boy Color")) identifiedSystem = "Game Boy Color";
    else if (lookup.find("Game Boy")) identifiedSystem = "Game Boy";
    else if (lookup.find("WonderSwan Color") || lookup.find("WSC")) identifiedSystem = "WonderSwan Color";
    else if (lookup.find("WonderSwan") || lookup.find("WS")) identifiedSystem = "WonderSwan";
    else if (lookup.find("PC Engine CD") || lookup.find("PCE CD") || lookup.find("TG16 CD") || lookup.find("TurboGrafx CD") || lookup.find("turbografx-cd")) identifiedSystem = "PC Engine CD";
    else if (lookup.find("SuperGrafx") || lookup.find("Super Grafx") || lookup.find("supergrafx")) identifiedSystem = "SuperGrafx";
    else if (lookup.find("PC Engine") || lookup.find("PC-Engine") || lookup.find("TG16") || lookup.find("PCE") || lookup.find("TurboGrafx") || lookup.find("tg16")) identifiedSystem = "PC Engine";
    else if (lookup.find("MSX2")) identifiedSystem = "MSX2";
    else if (lookup.find("MSX")) identifiedSystem = "MSX";
    else if (lookup.find("Mega CD") || lookup.find("Sega CD")) identifiedSystem = "Mega CD";

    currentMedium = mia::Medium::create(identifiedSystem);
    if (!currentMedium && identifiedSystem == "Neo Geo") {
        currentMedium = mia::Medium::create("Neo Geo MVS");
        if(!currentMedium) currentMedium = mia::Medium::create("Neo Geo AES");
    }

    if (!currentMedium) {
        LOGE("MIA: Failed to create medium for %s", (const char*)identifiedSystem);
        return false;
    }
    LOGI("MIA: Created medium for %s", (const char*)identifiedSystem);

    bool isDisc = extension == "chd" || extension == "iso" || extension == "cue" || extension == "mdf" || extension == "img";
    // Neo Geo ROMs are multi-file zips (e.g. mslug.zip). Don't extract them
    // or we lose the internal file structure MIA needs for the database lookup.
    bool isNeoGeo = (string)identifiedSystem == "Neo Geo" || (string)identifiedSystem == "Neo Geo CD";
    if (!forceZipLoad && !isDisc && extension == "zip" && !isNeoGeo) {
        LOGI("MIA: Attempting ZIP extraction for %s", (const char*)loadPath);
        std::vector<u8> romBuffer = currentMedium->read(loadPath);
        if (!romBuffer.empty()) {
            LOGI("MIA: Extracted %zu bytes from ZIP", romBuffer.size());
            string aresExt = "bin";
            if (identifiedSystem == "Game Boy") aresExt = "gb";
            if (identifiedSystem == "Game Boy Color") aresExt = "gbc";
            if (identifiedSystem == "Game Boy Advance") aresExt = "gba";
            if (identifiedSystem == "Super Famicom") aresExt = "sfc";
            if (identifiedSystem == "Famicom") aresExt = "fc";
            if (identifiedSystem == "Nintendo 64") aresExt = "z64";

            string rawRomPath = string{tempFilePath, "/phobos_rom_raw.", aresExt};
            FILE* rf = fopen((const char*)rawRomPath, "wb");
            if (rf) { fwrite(romBuffer.data(), 1, romBuffer.size(), rf); fclose(rf); }
            loadPath = rawRomPath;
        } else {
            LOGW("MIA: ZIP extraction returned empty buffer");
        }
    }

    auto loadResult = currentMedium->load(loadPath);
    if (loadResult != successful) {
        LOGE("MIA: Failed to load medium for %s at %s (Result: %d)", (const char*)identifiedSystem, (const char*)loadPath, (s32)loadResult.result);
        return false;
    }
    LOGI("MIA: Successfully loaded medium %s", (const char*)loadPath);

    bool success = false;
    root = {};

    auto getRegion = [&](const char* ntscU, const char* ntscJ, const char* pal) -> const char* {
        switch(regionPreference) {
            case 2: case 3: return ntscJ;
            case 4: case 5: return pal;
            default: return ntscU;
        }
    };

    LOGI("Ares: Loading core for %s", (const char*)identifiedSystem);
    if (identifiedSystem == "Nintendo 64" || identifiedSystem == "Nintendo 64DD") {
      ::ares::Nintendo64::vulkan.enable = true; // DEFAULT TO VULKAN
      // Set pipeline cache path for Vulkan shader persistence
      if (savesPath && strlen(savesPath) > 0) {
        ::ares::Nintendo64::vulkan.pipelineCachePath = string{savesPath, "/n64_vulkan_pipeline_cache.bin"};
        LOGI("N64: Pipeline cache path: %s", (const char*)::ares::Nintendo64::vulkan.pipelineCachePath);
      }
      if (n64UpscaleFactor < 1) n64UpscaleFactor = 1;
    if (n64UpscaleFactor > 4) n64UpscaleFactor = 4; // memory safety: see setN64Upscale()
      ::ares::Nintendo64::vulkan.internalUpscale = (u32)n64UpscaleFactor.load();
      ::ares::Nintendo64::vulkan.outputUpscale = n64SupersampleScanout.load() ? 1 : (u32)n64UpscaleFactor.load();
      ::ares::Nintendo64::vulkan.disableVideoInterfaceProcessing = n64DisableVIProcessing.load();
      ::ares::Nintendo64::vulkan.weaveDeinterlacing = n64WeaveDeinterlacing.load();
      ::ares::Nintendo64::vulkan.supersampleScanout = n64SupersampleScanout.load();
      ::ares::Nintendo64::cpu.recompiler.enabled = n64Recompiler.load();
      ::ares::Nintendo64::rsp.recompiler.enabled = n64Recompiler.load();
      bool is64DD = (identifiedSystem == "Nintendo 64DD" || extension == "ndd" || extension == "d64" || secondaryMedium != nullptr);
      ::ares::Nintendo64::system.expansionPak = n64ExpansionPak.load();

      const char* regionString = getRegion(
          is64DD ? "[Nintendo] Nintendo 64DD (NTSC-U)" : "[Nintendo] Nintendo 64 (NTSC)",
          is64DD ? "[Nintendo] Nintendo 64DD (NTSC-J)" : "[Nintendo] Nintendo 64 (NTSC-J)",
          "[Nintendo] Nintendo 64 (PAL)"
      );

      success = ::ares::Nintendo64::load(root, regionString);
    } else if (identifiedSystem == "Super Famicom") {
      ::ares::SuperFamicom::ppu.implementation = &::ares::SuperFamicom::ppuPerformanceImpl;
      ::ares::SuperFamicom::ppu.accurate = false;
      success = ::ares::SuperFamicom::load(root, getRegion("[Nintendo] Super Famicom (NTSC)", "[Nintendo] Super Famicom (NTSC)", "[Nintendo] Super Famicom (PAL)"));
    } else if (identifiedSystem == "Famicom") {
      success = ::ares::Famicom::load(root, getRegion("[Nintendo] Famicom (NTSC-U)", "[Nintendo] Famicom (NTSC-J)", "[Nintendo] Famicom (PAL)"));
    } else if (identifiedSystem == "PlayStation") {
      success = ::ares::PlayStation::load(root, getRegion("[Sony] PlayStation (NTSC-U)", "[Sony] PlayStation (NTSC-J)", "[Sony] PlayStation (PAL)"));
    } else if (identifiedSystem == "Game Boy Advance") {
      success = ::ares::GameBoyAdvance::load(root, "[Nintendo] Game Boy Advance");
    } else if (identifiedSystem == "Game Boy") {
      success = ::ares::GameBoy::load(root, "[Nintendo] Game Boy");
    } else if (identifiedSystem == "Game Boy Color") {
      success = ::ares::GameBoy::load(root, "[Nintendo] Game Boy Color");
    } else if (identifiedSystem == "Mega Drive") {
      success = ::ares::MegaDrive::load(root, getRegion("[Sega] Mega Drive (NTSC-U)", "[Sega] Mega Drive (NTSC-J)", "[Sega] Mega Drive (PAL)"));
    } else if (identifiedSystem == "Neo Geo") {
       success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo MVS");
       if(!success) success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo AES");
    } else if (identifiedSystem == "Sega Saturn") {
       success = ::ares::Saturn::load(root, getRegion("[Sega] Saturn (NTSC-U)", "[Sega] Saturn (NTSC-J)", "[Sega] Saturn (PAL)"));
    } else if (identifiedSystem == "Master System") {
       success = ::ares::MasterSystem::load(root, getRegion("[Sega] Master System (NTSC-U)", "[Sega] Master System (NTSC-J)", "[Sega] Master System (PAL)"));
    } else if (identifiedSystem == "Game Gear") {
       success = ::ares::MasterSystem::load(root, getRegion("[Sega] Game Gear (NTSC-U)", "[Sega] Game Gear (NTSC-J)", "[Sega] Game Gear (PAL)"));
    } else if (identifiedSystem == "PC Engine CD") {
       success = ::ares::PCEngine::load(root, getRegion("[NEC] PC Engine Duo (NTSC-J)", "[NEC] PC Engine Duo (NTSC-J)", "[NEC] PC Engine Duo (NTSC-J)"));
    } else if (identifiedSystem == "SuperGrafx") {
       success = ::ares::PCEngine::load(root, "[NEC] SuperGrafx (NTSC-J)");
    } else if (identifiedSystem == "PC Engine") {
       success = ::ares::PCEngine::load(root, getRegion("[NEC] TurboGrafx 16 (NTSC-U)", "[NEC] PC Engine (NTSC-J)", "[NEC] PC Engine (NTSC-J)"));
    } else if (identifiedSystem == "MSX2") {
       success = ::ares::MSX::load(root, getRegion("[Microsoft] MSX2 (NTSC)", "[Microsoft] MSX2 (NTSC)", "[Microsoft] MSX2 (PAL)"));
    } else if (identifiedSystem == "MSX") {
       success = ::ares::MSX::load(root, getRegion("[Microsoft] MSX (NTSC)", "[Microsoft] MSX (NTSC)", "[Microsoft] MSX (PAL)"));
    } else if (identifiedSystem == "Mega CD") {
       success = ::ares::MegaDrive::load(root, getRegion("[Sega] Mega CD (NTSC-U)", "[Sega] Mega CD (NTSC-J)", "[Sega] Mega CD (PAL)"));
    } else if (identifiedSystem == "WonderSwan Color") {
       success = ::ares::WonderSwan::load(root, "[Bandai] WonderSwan Color");
    } else if (identifiedSystem == "WonderSwan") {
       success = ::ares::WonderSwan::load(root, "[Bandai] WonderSwan");
    } else if (identifiedSystem == "Neo Geo Pocket" || identifiedSystem == "Neo Geo Pocket Color") {
       // MIA always returns "Neo Geo Pocket" but the library may specify
       // "Neo Geo Pocket Color" — use the system name from the library
       // to disambiguate, since the TLCS900H CPU boots differently per model.
       string aresName = "[SNK] Neo Geo Pocket";
       if (systemName.downcase().find("color") || identifiedSystem == "Neo Geo Pocket Color") aresName = "[SNK] Neo Geo Pocket Color";
       success = ::ares::NeoGeoPocket::load(root, aresName);
    } else if (identifiedSystem == "Neo Geo") {
       success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo MVS");
       if(!success) success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo AES");
    } else if (identifiedSystem == "Atari 2600") {
       success = ::ares::Atari2600::load(root, getRegion("[Atari] Atari 2600 (NTSC)", "[Atari] Atari 2600 (NTSC)", "[Atari] Atari 2600 (PAL)"));
    } else if (identifiedSystem == "ColecoVision") {
       success = ::ares::ColecoVision::load(root, getRegion("[Coleco] ColecoVision (NTSC)", "[Coleco] ColecoVision (NTSC)", "[Coleco] ColecoVision (PAL)"));
    } else if (identifiedSystem == "SG-1000") {
       success = ::ares::SG1000::load(root, getRegion("[Sega] SG-1000 (NTSC)", "[Sega] SG-1000 (NTSC)", "[Sega] SG-1000 (PAL)"));
    } else if (identifiedSystem == "ZX Spectrum") {
       success = ::ares::ZXSpectrum::load(root, "[Sinclair] ZX Spectrum");
    } else {
        LOGE("Ares: Unidentified system (no load case)");
    }

    if (success && root) {
      for (auto& setting : root->find<Node::Setting::Boolean>()) {
          if (setting->name() == "Fast Boot") setting->setValue(fastBootAtomic);
          if (setting->name() == "Expansion Pak") setting->setValue(n64ExpansionPak);
          if (setting->name() == "Recompiler" && (identifiedSystem == "Nintendo 64" || identifiedSystem == "Nintendo 64DD")) {
              setting->setValue(n64Recompiler);
              LOGI("N64: CPU Recompiler set to %s", n64Recompiler ? "ON" : "OFF");
          }
      }
      for (auto& setting : root->find<Node::Setting::Setting>()) setting->setLatch();
      if (identifiedSystem == "Game Boy Advance") {
          for (auto& setting : root->find<Node::Setting::Boolean>()) {
              if (setting->name() == "Real Time Clock") {
                  setting->setValue(true);
                  LOGI("GBA: Real Time Clock enabled");
              }
          }
      }

      if (identifiedSystem == "Nintendo 64" || identifiedSystem == "Nintendo 64DD") {
          ::ares::Nintendo64::option("Recompiler", n64Recompiler ? "true" : "false");
          LOGI("N64: CPU Recompiler set to %s", n64Recompiler ? "ON" : "OFF");
      }

      connectDevices(root);

      // Import game save data (SRAM, EEPROM, memcards) — always restores.
      // The Auto-Load Memory toggle controls the "Auto" save-state slot only.
      if (savesPath && currentMedium && currentMedium->pak) {
        string saveDir = {savesPath, "/", identifiedSystem, "/"};
        directory::create(saveDir);
        for (auto& saveNode : currentMedium->pak->files()) {
          string fileName = saveNode->name();
          if (!fileName.endsWith(".ram") && !fileName.endsWith(".srm") &&
              !fileName.endsWith(".eeprom") && !fileName.endsWith(".card") &&
              !fileName.endsWith(".sav") && !fileName.endsWith(".fla")) continue;
          string fullPath = {saveDir, fileName};
          auto existing = file::read(fullPath);
          if (existing.size() == 0) continue;
          if (auto fp = currentMedium->pak->write(fileName)) {
            fp->write({existing.data(), (u32)existing.size()});
            LOGI("Saves: imported %s (%zu bytes) for %s", (const char*)fileName, existing.size(), (const char*)identifiedSystem);
          }
        }
        // MIA-level sidecar saves (.sav, .flash, cpu.ram): restore them to
        // mia_temp so MIA's Pak::load() / ares' CPU::load() pick them up.
        for (auto& saveName : {"program.sav", "program.flash"}) {
          string fullPath = {saveDir, saveName};
          auto existing = file::read(fullPath);
          if (existing.size() == 0) continue;
          string sidecarPath = string{tempFilePath, "/", saveName};
          file::write(sidecarPath, existing);
          LOGI("Saves: imported sidecar %s (%zu bytes) for %s", saveName, existing.size(), (const char*)identifiedSystem);
        }
      }

      // Inject saved cpu.ram + bios.rom BEFORE power-on so CPU::power()
      // sees ram[0x2c7a]!=0 → warm-boot path → skip language/date prompts.
      if (identifiedSystem == "Neo Geo Pocket" || identifiedSystem == "Neo Geo Pocket Color") {
        if (savesPath) {
          string d = string{savesPath, "/", identifiedSystem, "/"};
          auto r = nall::file::read({d, "cpu.ram"});
          if (r.size() == 12_KiB) { memcpy(ares::NeoGeoPocket::cpu.ram.data(), r.data(), 12_KiB); ares::NeoGeoPocket::cpu.ram.write(0x2c7a, 1); }
          r = nall::file::read({d, "bios.rom"});
          if (r.size() == 64_KiB) { memcpy((void*)ares::NeoGeoPocket::system.bios.data(), r.data(), 64_KiB); }
        }
      }

      root->power();

      if (skipBootRom) {
          if (identifiedSystem == "Game Boy" || identifiedSystem == "Game Boy Color") {
              LOGI("GB: Applying post-boot register state (Skip Boot ROM)");
              ::ares::GameBoy::cpu.r.pc.word = 0x0100;
              ::ares::GameBoy::cpu.r.af.word = 0x01b0;
              ::ares::GameBoy::cpu.r.bc.word = 0x0013;
              ::ares::GameBoy::cpu.r.de.word = 0x00d8;
              ::ares::GameBoy::cpu.r.hl.word = 0x014d;
              ::ares::GameBoy::cpu.r.sp.word = 0xfffe;

              ::ares::GameBoy::ppu.status.displayEnable = 1;
              ::ares::GameBoy::ppu.status.bgEnable = 1;
              ::ares::GameBoy::ppu.status.obEnable = 1;
              ::ares::GameBoy::ppu.status.bgTiledataSelect = 1;

              ::ares::GameBoy::ppu.bgp[0] = 0;
              ::ares::GameBoy::ppu.bgp[1] = 1;
              ::ares::GameBoy::ppu.bgp[2] = 2;
              ::ares::GameBoy::ppu.bgp[3] = 3;

              ::ares::GameBoy::ppu.latch.displayEnable = 1;

              // Force redraw
              ::ares::GameBoy::ppu.status.ly = 0;
              ::ares::GameBoy::ppu.status.lx = 0;

              ::ares::GameBoy::cartridge.bootromEnable = false;
          }
      }

      setEmulationRunning(true);
      LOGI("System loaded successfully: %s", (const char*)identifiedSystem);
      return true;
    }
    LOGE("Ares: Failed to load system %s", (const char*)identifiedSystem);
    return false;
  }

  auto setFastBoot(bool enabled) -> void { fastBootAtomic = enabled; LOGI("Fast boot %s", enabled ? "enabled" : "disabled"); }
  auto setAutoSaveMemory(bool enabled) -> void { autoSaveMemoryAtomic = enabled; LOGI("Auto-save memory %s", enabled ? "enabled" : "disabled"); }
  auto setAutoLoadMemory(bool enabled) -> void { autoLoadMemoryAtomic = enabled; LOGI("Auto-load memory %s", enabled ? "enabled" : "disabled"); }
  auto setPause(bool paused) -> void {
    isPausedAtomic = paused;
    // Stop the audio stream while paused so it doesn't keep draining with no
    // new samples (underrun pops in the pause menu); restart it on resume.
    // The audio thread checks isPausedAtomic and drops queued samples while
    // paused, so stale audio never plays on resume.
    // Only requestStart if not already starting/started — calling it on an
    // already-starting stream returns -895 and can desync the clock.
    std::lock_guard<std::mutex> lock(audioMutex);
    if (audioStream) {
      if (paused) {
        AAudioStream_requestStop(audioStream);
      } else {
        aaudio_stream_state_t state = AAudioStream_getState(audioStream);
        if (state != AAUDIO_STREAM_STATE_STARTING && state != AAUDIO_STREAM_STATE_STARTED) {
          AAudioStream_requestStart(audioStream);
        }
      }
    }
    LOGI("Emulation %s", paused ? "paused" : "resumed");
  }
  auto setFastForward(bool enabled) -> void { fastForwardAtomic = enabled; LOGI("Fast forward %s", enabled ? "enabled" : "disabled"); }
  auto setFastForwardSpeed(f32 speed) -> void { ffSpeedLimitAtomic = speed; LOGI("Fast forward speed set to %.1fx", (f64)speed); }
  auto resetSystem() -> void {
    resetRequestedAtomic.store(true);
    #if defined(CORE_N64)
    // Soft reset: keep the Vulkan device alive. Destroying the device and
    // creating a fresh one in the same process is fundamentally broken on
    // Turnip/Mesa — even when teardown "succeeds" (bounded fence waits),
    // the newly created device's fences can fail to signal, so the first
    // frame after reset hangs forever with no output. Upstream ares soft
    // resets N64 with the device alive (System::power(true) keeps it when
    // discardPipelineCache is false), which is both correct and fast — the
    // pipeline cache survives, so no post-reset shader-recompile storm.
    // The VI/deinterlace/supersample atomics below are read live by
    // scanoutAsync every frame, so the new values simply take effect
    // immediately.
    //
    // Defensive: clear any stale teardown flags so a reset never inherits
    // a device-teardown decision from an earlier path. (The abandon path
    // no longer sets these — it destroyed the warm pipeline cache — but
    // clearing them here guarantees reset always takes the keep-device
    // branch.)
    ::ares::Nintendo64::Vulkan::discardPipelineCache = false;
    ::ares::Nintendo64::Vulkan::skipCachePersist = false;
    ::ares::Nintendo64::vulkan.disableVideoInterfaceProcessing = n64DisableVIProcessing.load();
    ::ares::Nintendo64::vulkan.weaveDeinterlacing = n64WeaveDeinterlacing.load();
    ::ares::Nintendo64::vulkan.supersampleScanout = n64SupersampleScanout.load();
    ::ares::Nintendo64::vulkan.outputUpscale = n64SupersampleScanout.load() ? 1 : (u32)n64UpscaleFactor.load();
    ::ares::Nintendo64::cpu.recompiler.enabled = n64Recompiler.load();
    ::ares::Nintendo64::rsp.recompiler.enabled = n64Recompiler.load();
    #endif
    LOGI("System reset requested");
  }
  auto frameAdvance() -> void { lock_guard<recursive_mutex> lock(*runMutex); if (root) root->run(); }
  auto setMuteAudio(bool muted) -> void { muteAudioAtomic = muted; }
  auto setShader(const char* path) -> bool { return true; }
  auto saveState(const char* path) -> bool {
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> lock(*runMutex);
    if (!root) { isPausedAtomic.store(wasPaused); return false; }
    auto s = root->serialize(true);
    bool result = nall::file::write(path, {s.data(), s.size()});
    LOGI("Save state to %s: %s", path, result ? "success" : "failed");
    isPausedAtomic.store(wasPaused);
    return result;
  }
  auto loadState(const char* path) -> bool {
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> lock(*runMutex);
    if (!root) { isPausedAtomic.store(wasPaused); return false; }

    auto totalStart = std::chrono::steady_clock::now();
    FILE* f = fopen(path, "rb");
    if (!f) { LOGE("loadState: File not found or unreadable: %s", path); isPausedAtomic.store(wasPaused); return false; }
    fclose(f);

    auto readStart = std::chrono::steady_clock::now();
    auto data = nall::file::read(path);
    auto readEnd = std::chrono::steady_clock::now();

    if (data.size() == 0) { LOGE("loadState: nall::file::read returned empty data for %s", (const char*)path); isPausedAtomic.store(wasPaused); return false; }

    auto unserializeStart = std::chrono::steady_clock::now();
    nall::serializer s(data.data(), data.size());
    bool result = root->unserialize(s);
    auto unserializeEnd = std::chrono::steady_clock::now();

    auto totalEnd = std::chrono::steady_clock::now();
    LOGI("LoadState Timing: Total=%lldms, FileRead=%lldms, Unserialize=%lldms",
        (s64)std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count(),
        (s64)std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - readStart).count(),
        (s64)std::chrono::duration_cast<std::chrono::milliseconds>(unserializeEnd - unserializeStart).count());

    LOGI("Load state from %s: %s", (const char*)path, result ? "success" : "failed");
    isPausedAtomic.store(wasPaused);
    return result;
  }
  auto setLogLevel(s32 level) -> void { /* retained for JNI API compatibility; log verbosity no longer filters frontend logs */ }
  auto setRegion(s32 regionIndex) -> void { regionPreference = regionIndex; }
  auto setN64Upscale(s32 factor) -> void {
    if (factor < 1) factor = 1;
    // Hard cap at 4x: 8x on a 640x240 framebuffer creates ~5120x3840
    // internal targets (~150MB per buffer, multiple in flight) which
    // exhausts device memory — kswapd thrashes, dequeueBuffer fails,
    // ANR (observed in the field). Clamping here (the JNI entry point)
    // protects both the pause menu and the settings menu.
    if (factor > 4) factor = 4;
    n64UpscaleFactor = factor;
    LOGI("N64 upscale factor set to %dx (applies on next reset)", factor);
  }
  auto setN64Recompiler(bool enabled) -> void {
    n64Recompiler = enabled;
    LOGI("N64 recompiler set to %s (applies on next reset)", enabled ? "enabled" : "disabled");
  }
  auto setSkipBootRom(bool enabled) -> void { skipBootRom = enabled; LOGI("Skip Boot ROM set to %s", enabled ? "enabled" : "disabled"); }
  // VI/deinterlace/supersample settings cannot be applied live — they
  // alter the RDP scanout pipeline which is actively rendering frames.
  // Mutating them mid-frame causes GPU fence deadlocks (the emulation
  // thread blocks indefinitely in scanoutAsync waiting for a fence that
  // the GPU can no longer signal with the changed VI config).
  // Instead, just persist the preference; it takes effect on the next
  // System Reset or fresh load.
  auto setN64DisableVIProcessing(bool enabled) -> void {
    n64DisableVIProcessing = enabled;
    LOGI("N64 disable VI processing set to %d (applies on next reset)", enabled);
  }
  auto setN64WeaveDeinterlacing(bool enabled) -> void {
    n64WeaveDeinterlacing = enabled;
    LOGI("N64 weave deinterlacing set to %d (applies on next reset)", enabled);
  }
  auto setN64SupersampleScanout(bool enabled) -> void {
    n64SupersampleScanout = enabled;
    LOGI("N64 supersample scanout set to %d (applies on next reset)", enabled);
  }
  auto setN64ExpansionPak(bool enabled) -> void {
    if (n64ExpansionPak == enabled) return;
    n64ExpansionPak = enabled;
    LOGI("N64 expansion pak set to %d", enabled);
    lock_guard<std::recursive_mutex> lock(systemMutex);
    if (root && root->name() == "Nintendo 64") {
        for (auto& setting : root->find<Node::Setting::Boolean>()) {
            if (setting->name() == "Expansion Pak") setting->setValue(enabled);
        }
    }
  }
  auto setPs1AnalogMode(bool enabled) -> void {
    if (ps1AnalogMode == enabled) return;
    ps1AnalogMode = enabled;
    LOGI("PS1 analog mode set to %d", enabled);
    lock_guard<std::recursive_mutex> lock(systemMutex);
    if (root && root->name() == "PlayStation") {
        connectDevices(root);
    }
  }
  // Runtime analog toggle: flips ps1AnalogMode and re-allocates controller
  // port 1 to swap between DualShock and Digital Gamepad.
  auto togglePs1AnalogMode() -> bool {
    lock_guard<std::recursive_mutex> lock(systemMutex);
    if (!root || root->name() != "PlayStation") return false;
    ps1AnalogMode = !ps1AnalogMode;
    LOGI("PS1 analog toggle -> %d", (int)ps1AnalogMode);
    connectDevices(root);
    return true;
  }
  auto setStickToDpad(bool enabled) -> void {
    // Deprecated
  }
  auto setCustomDriverPath(const char* path) -> void { customDriverPath = path ? (string)path : ""; LOGI("Custom driver path set: %s", (const char*)customDriverPath); }
  auto setOrientationMode(bool vertical) -> void { orientationVertical = vertical; LOGI("Orientation mode set to %s", vertical ? "Vertical" : "Horizontal"); }
  auto setRomFd(s32 fd) -> void { lock_guard<recursive_mutex> lock(systemMutex); if (romFd != -1) ::close(romFd); romFd = fd; }
  auto setSecondaryRomFd(s32 fd) -> void { lock_guard<recursive_mutex> lock(systemMutex); if (secondaryRomFd != -1) ::close(secondaryRomFd); secondaryRomFd = fd; }
  auto setTempFilePath(const char* path) -> void { tempFilePath = path ? (string)path : ""; }
  auto setLoadDiskImageToRam(bool enabled) -> void { /* Deprecated */ }

  auto setInput(f32 lx, f32 ly, f32 rx, f32 ry, s32 buttons) -> void {
      static u64 inputLogCount = 0;
      if (inputLogCount++ % 30 == 0 && (abs(lx) > 0.05f || abs(ly) > 0.05f || abs(rx) > 0.05f || abs(ry) > 0.05f)) {
          LOGI("PhobosRunner: setInput LS(%.2f, %.2f) RS(%.2f, %.2f) BTNS=%08x", (double)lx, (double)ly, (double)rx, (double)ry, buttons);
      }
      inputState.lx = lx;
      inputState.ly = ly;
      inputState.rx = rx;
      inputState.ry = ry;
      inputState.buttons = buttons;
  }

  auto setNativeLibraryDir(const char* path) -> void { nativeLibraryDir = path ? (string)path : ""; LOGI("Native library dir set: %s", (const char*)nativeLibraryDir); }
  auto setFirmwarePath(const char* path) -> void { LOGI("Firmware path set: %s", path ? path : ""); }
  auto mapFirmwareFile(const char* name, const char* path) -> void { firmwareMap[name] = path ? (string)path : ""; LOGI("Firmware mapped: %s -> %s", name, (const char*)path); }
  auto setHomePath(const char* path) -> void {
    homePath = path ? (string)path : "";
    LOGI("Home path set: %s", (const char*)homePath);
    mia::setHomeLocation([] {
      string p = homePath;
      if (!p.endsWith("/")) p.append("/");
      return p;
    });
  }
  auto setSavesPath(const char* path) -> void {
    savesPath = path ? (string)path : "";
    LOGI("Saves path set: %s", (const char*)savesPath);
  }

  auto loadSecondaryRom(const char* systemNamePtr, const char* uriPtr) -> bool {
    string systemName = systemNamePtr;
    string uri = uriPtr;
    lock_guard<std::recursive_mutex> lock(systemMutex);
    LOGI("Loading secondary medium: %s, uri: %s", (const char*)systemName, (const char*)uri);

    if (secondaryRomFd == -1) return false;

    string extension = "bin";
    if(auto position = uri.findPrevious(uri.size(), ".")) {
        extension = uri.slice(*position + 1).downcase();
    }

    if (!tempFilePath) return false;
    string tempPath = string{tempFilePath, "/phobos_secondary.", extension};

    FILE* f = fopen((const char*)tempPath, "wb");
    if (!f) return false;
    std::vector<u8> copyBuf;
    copyBuf.resize(1024 * 1024);
    lseek(secondaryRomFd, 0, SEEK_SET);
    while (true) {
        ssize_t r = read(secondaryRomFd, copyBuf.data(), copyBuf.size());
        if (r <= 0) break;
        fwrite(copyBuf.data(), 1, r, f);
    }
    fclose(f);

    secondaryMedium = mia::Medium::create(systemName);
    if (!secondaryMedium) {
        LOGE("MIA: Failed to create secondary medium for %s", (const char*)systemName);
        return false;
    }

    auto loadResult = secondaryMedium->load(tempPath);
    if (loadResult != successful) {
        LOGE("MIA: Failed to load secondary medium for %s (Result: %d)", (const char*)systemName, (s32)loadResult.result);
        return false;
    }

    if (root) {
        connectDevices(root); // Refresh ports to attach new medium
        LOGI("Secondary medium loaded successfully");
        return true;
    }
    return false;
  }

  auto setSurface(JNIEnv* env, jobject surface) -> void {
    lock_guard<std::mutex> lock(windowMutex);
    LOGI("PhobosSurface: setSurface called. Old=%p, New=%p", nativeWindow, surface);
    if (nativeWindow) ANativeWindow_release(nativeWindow);
    nativeWindow = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (nativeWindow) {
        LOGI("PhobosSurface: New ANativeWindow acquired: %p", nativeWindow);
    }
    windowChanged = true;
  }
  auto getNewLogs() -> std::vector<LogEntry> {
    lock_guard<mutex> lock(logMutex);
    std::vector<LogEntry> logs;
    logs.reserve(logBuffer.size());
    for (auto& entry : logBuffer) logs.push_back(std::move(entry));
    logBuffer.clear();
    return logs;
  }
  auto isFirstFrameRendered() -> bool { return firstFrameRendered.load(); }
  auto getPerformanceStats() -> PerformanceStats {
    PerformanceStats stats;
    stats.fps = currentFps.load();
    stats.frameTime = avgFrameTime.load() / 1000.0;
    stats.activeCore = (s32)sched_getcpu();
    #if defined(CORE_N64)
    stats.pipelineFailures = ::ares::Nintendo64::Vulkan::pipelineFailureCount.load(std::memory_order_relaxed);
    stats.isAdrenoDriver = (bool)::ares::Nintendo64::Vulkan::gpuDeviceName.find("Adreno");
    #else
    stats.pipelineFailures = 0;
    stats.isAdrenoDriver = false;
    #endif
    return stats;
  }
  auto takeScreenshot(const char* path) -> bool {
    lock_guard<mutex> lock(windowMutex);
    if (lastFrameBuffer.empty() || currentWidth == 0 || currentHeight == 0) return false;
    std::vector<u32> converted;
    converted.resize(lastFrameBuffer.size());
    for(u32 i = 0; i < lastFrameBuffer.size(); i++) {
        u32 p = lastFrameBuffer[i];
        converted[i] = (p & 0xFF00FF00) | ((p >> 16) & 0x000000FF) | ((p << 16) & 0x00FF0000);
    }
    return nall::Encode::PNG::RGBA8(path, converted.data(), (s32)currentWidth * 4, (s32)currentWidth, (s32)currentHeight);
  }
}
