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
  std::atomic<bool> resetRequestedAtomic{false};
  std::thread emulationThread;
  bool isPaused = false;

  static s32 romFd = -1;
  static s32 secondaryRomFd = -1;
  static std::shared_ptr<mia::Pak> currentMedium;
  static std::shared_ptr<mia::Pak> secondaryMedium;
  static std::atomic<bool> firstFrameRendered{false};
  static ANativeWindow* nativeWindow = nullptr;
  static AAudioStream* audioStream = nullptr;
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
  static Node::Object cachedPlayer1;
  static LogLevel currentLogLevel = LogLevel::Info;

  struct InputState {
    std::atomic<f32> lx{0.0f}, ly{0.0f}, rx{0.0f}, ry{0.0f};
    std::atomic<s32> buttons{0};
  } inputState;

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

  static bool muteAudioAtomic = false;
  static bool fastBootAtomic = false;
  static s32 regionPreference = 0;
  static s32 n64RendererMode = 0;
  static bool n64Recompiler = true;
  static bool ps1AnalogMode = true;
  static bool n64ExpansionPak = true;
  static bool skipBootRom = false;
  static bool orientationVertical = false;
  static string customDriverPath;
  static string nativeLibraryDir;
  static string tempFilePath;
  static string homePath;
  static string firmwarePath;
  static string shaderPath;
  static std::map<string, string> firmwareMap;

  auto addLog(LogLevel level, string message) -> void {
    std::lock_guard<std::mutex> lock(logMutex);
    logBuffer.push_back({level, message});
    if (logBuffer.size() > 5000) logBuffer.pop_front();
  }

  auto emulationLoop() -> void {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    s32 num_cores = sysconf(_SC_NPROCESSORS_CONF);
    for (s32 i = std::max(0, num_cores - 4); i < num_cores; i++) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    while (emulationRunning) {
      if (resetRequestedAtomic.exchange(false)) {
        std::lock_guard<std::recursive_mutex> lock(systemMutex);
        if (root) {
            root->power();
            addLog(LogLevel::Info, "System reset (async)");
        }
      }

      if (!isPausedAtomic) {
        auto start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::recursive_mutex> lock(systemMutex);
            if (root) {
                root->run();
            }
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto end = std::chrono::steady_clock::now();

        // Speed limiting is now handled by blocking audio sync in platform::audio
        // unless fast-forwarding is enabled.
        if (fastForwardAtomic) {
            f64 speed = (f64)ffSpeedLimitAtomic;
            if (speed > 0.0) {
                f64 targetFrameTime = (1000000.0 / 60.0) / speed;
                auto actualFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                if (actualFrameTime < targetFrameTime) {
                    std::this_thread::sleep_for(std::chrono::microseconds((s64)(targetFrameTime - (f64)actualFrameTime)));
                }
            }
        }

        lastFrameTime = (u64)std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        avgFrameTime = avgFrameTime * 0.9 + (f64)lastFrameTime * 0.1;
        frameCount++;

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
  }

  auto setEmulationRunning(bool running) -> void {
    if (emulationRunning == running) return;

    if (running) {
      if (emulationThread.joinable()) {
          emulationRunning = false;
          emulationThread.join();
      }
      emulationRunning = true;
      emulationThread = std::thread(emulationLoop);
    } else {
      emulationRunning = false;
      if (emulationThread.joinable()) {
        emulationThread.join();
      }
    }
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

    auto input(Node::Input::Input input) -> void override {
      if (!root) return;
      string nodeName = input->name();
      string systemName = root->name();

      u32 buttons = (u32)inputState.buttons.load();
      f32 lx = inputState.lx.load();
      f32 ly = inputState.ly.load();
      f32 rx = inputState.rx.load();
      f32 ry = inputState.ry.load();

      if (auto button = input->cast<Node::Input::Button>()) {
          u32 b = 0;

          // Standard D-Pad
          if (nodeName == "Up" || nodeName == "↑") b = VirtualGamepad::Up;
          else if (nodeName == "Down" || nodeName == "↓") b = VirtualGamepad::Down;
          else if (nodeName == "Left" || nodeName == "←") b = VirtualGamepad::Left;
          else if (nodeName == "Right" || nodeName == "→") b = VirtualGamepad::Right;

          // Face Buttons
          else if (nodeName == "A" || nodeName == "Cross" || nodeName == "I" || nodeName == "1" || nodeName == "○") b = VirtualGamepad::A;
          else if (nodeName == "B" || nodeName == "Circle" || nodeName == "II" || nodeName == "2" || nodeName == "×") b = VirtualGamepad::B;
          else if (nodeName == "X" || nodeName == "Square" || nodeName == "III" || nodeName == "□") b = VirtualGamepad::X;
          else if (nodeName == "Y" || nodeName == "Triangle" || nodeName == "IV" || nodeName == "△") b = VirtualGamepad::Y;

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
          else if (nodeName == "X1") b = orientationVertical ? VirtualGamepad::X : VirtualGamepad::Up;      // Vertical: X, Horizontal: D-Up
          else if (nodeName == "X2") b = orientationVertical ? VirtualGamepad::Y : VirtualGamepad::Right;   // Vertical: Y, Horizontal: D-Right
          else if (nodeName == "X3") b = orientationVertical ? VirtualGamepad::B : VirtualGamepad::Down;    // Vertical: B, Horizontal: D-Down
          else if (nodeName == "X4") b = orientationVertical ? VirtualGamepad::A : VirtualGamepad::Left;    // Vertical: A, Horizontal: D-Left
          else if (nodeName == "Y1") b = orientationVertical ? VirtualGamepad::Left : VirtualGamepad::L1;   // Vertical: D-Left, Horizontal: L1
          else if (nodeName == "Y2") b = orientationVertical ? VirtualGamepad::Up : VirtualGamepad::R1;     // Vertical: D-Up, Horizontal: R1
          else if (nodeName == "Y3") b = orientationVertical ? VirtualGamepad::Right : VirtualGamepad::X;   // Vertical: D-Right, Horizontal: X
          else if (nodeName == "Y4") b = orientationVertical ? VirtualGamepad::Down : VirtualGamepad::Y;    // Vertical: D-Down, Horizontal: Y

          else if (nodeName == "A")  b = orientationVertical ? VirtualGamepad::L1 : VirtualGamepad::B;      // Vertical: L1, Horizontal: B
          else if (nodeName == "B")  b = orientationVertical ? VirtualGamepad::R1 : VirtualGamepad::A;      // Vertical: R1, Horizontal: A

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
              else if (nodeName == "C-Up")    { button->setValue((buttons & VirtualGamepad::RS_Up) != 0); return; }
              else if (nodeName == "C-Down")  { button->setValue((buttons & VirtualGamepad::RS_Down) != 0); return; }
              else if (nodeName == "C-Left")  { button->setValue((buttons & VirtualGamepad::RS_Left) != 0); return; }
              else if (nodeName == "C-Right") { button->setValue((buttons & VirtualGamepad::RS_Right) != 0); return; }
          } else if (systemName == "PlayStation") {
              // DualShock uses L1, R1, L2, R2, L3, R3 explicitly
              if      (nodeName == "L1") b = VirtualGamepad::L1;
              else if (nodeName == "R1") b = VirtualGamepad::R1;
              else if (nodeName == "L2") b = VirtualGamepad::L2;
              else if (nodeName == "R2") b = VirtualGamepad::R2;
              else if (nodeName == "L3") b = VirtualGamepad::L3;
              else if (nodeName == "R3") b = VirtualGamepad::R3;
          }

          // UNIVERSAL STICK BLOCK: If physical analog stick is displaced, block digital D-Pad to core
          if (b == VirtualGamepad::Up || b == VirtualGamepad::Down || b == VirtualGamepad::Left || b == VirtualGamepad::Right) {
              float threshold = 0.08f;
              if (abs(lx) > threshold || abs(ly) > threshold || abs(rx) > threshold || abs(ry) > threshold) {
                  button->setValue(false);
                  return;
              }
          }

          // Always set the value (resetting if not mapped) to ensure state consistency
          button->setValue(b != 0 && (buttons & b) != 0);
      } else if (auto axis = input->cast<Node::Input::Axis>()) {
          s16 value = 0;
          string nodeName = axis->name();

          if (nodeName == "LX" || nodeName == "L-Stick X" || nodeName == "Left X" || nodeName == "X-Axis") {
              value = (s16)(lx * 32767.0f);
          } else if (nodeName == "LY" || nodeName == "L-Stick Y" || nodeName == "Left Y" || nodeName == "Y-Axis") {
              value = (s16)(ly * 32767.0f);
          } else if (nodeName == "RX" || nodeName == "R-Stick X" || nodeName == "Right X" || nodeName == "Z-Axis") {
              value = (s16)(rx * 32767.0f);
          } else if (nodeName == "RY" || nodeName == "R-Stick Y" || nodeName == "Right Y" || nodeName == "RZ-Axis") {
              value = (s16)(ry * 32767.0f);
          }

          if (abs(value) > 1000) {
              LOGI("PhobosRunnerInput: Axis Set '%s' = %d", (const char*)nodeName, value);
          }
          axis->setValue(value);
      }
    }

    auto video(Node::Video::Screen screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
      if (width == 0 || height == 0) return;

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

      // Determine if we should use 2x scaling for sharpness
      bool scale2x = (width <= 320) && !rotate; // Don't scale2x if rotating for now
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
        } else if (isN64Vulkan && vData && vW == width && vH == height) {
            // Direct copy from Vulkan RGBA to Android ABGR
            for (s32 y = 0; y < (s32)height; y++) {
                const u32* srcLine = (const u32*)(vData + y * width * 4);
                u32* destLine = dest + y * dst_stride;
                u32* saveLine = lastFrameBuffer.data() + y * width;
                s32 x = 0;
                #if defined(__aarch64__) || defined(__arm__)
                uint32x4_t alpha = vdupq_n_u32(0xFF000000);
                for (; x <= (s32)width - 4; x += 4) {
                    uint32x4_t p = vld1q_u32(srcLine + x);
                    uint32x4_t result = vorrq_u32(alpha, p);
                    vst1q_u32(destLine + x, result);
                    vst1q_u32(saveLine + x, result);
                }
                #endif
                for (; x < (s32)width; x++) {
                    u32 ap = 0xFF000000 | srcLine[x];
                    destLine[x] = ap;
                    saveLine[x] = ap;
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
            s32 framesPerBurst = AAudioStream_getFramesPerBurst(audioStream);
            AAudioStream_setBufferSizeInFrames(audioStream, framesPerBurst * 4); // 4 bursts for stability
            AAudioStream_requestStart(audioStream);
        }
      }

      if (audioStream) {
        static std::vector<f32> localBuffer;
        localBuffer.reserve(2048);

        f64 samples[2];
        while (stream->pending()) {
          stream->read(samples);
          localBuffer.push_back((f32)samples[0]);
          localBuffer.push_back((f32)samples[1]);
        }

        // Blocking write to provide audio-sync for the emulation loop
        if (!localBuffer.empty()) {
            if (muteAudioAtomic) {
                // If muted, fill the buffer with silence but still write to maintain audio-sync timing
                std::fill(localBuffer.begin(), localBuffer.end(), 0.0f);
            }

            // Audio-Sync: Use a large timeout to force the emulation loop to wait for hardware
            s64 timeout = fastForwardAtomic ? 0 : 50000000; // 50ms timeout
            AAudioStream_write(audioStream, localBuffer.data(), (s32)localBuffer.size() / 2, timeout);
            localBuffer.clear();
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
      } else if (nodeName == "Mega CD") {
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
          if (!attached) attached = attachFile("bios.rom");
      } else if (nodeName == "Neo Geo AES" || nodeName == "Neo Geo MVS") {
          bool attached = false;
          auto it_aes = firmwareMap.find("fw_ng_aes");
          if (it_aes != firmwareMap.end()) attached = attachFile((const char*)it_aes->second, "bios.rom");
          if (!attached) {
              auto it_mvs = firmwareMap.find("fw_ng_mvs");
              if (it_mvs != firmwareMap.end()) attached = attachFile((const char*)it_mvs->second, "bios.rom");
          }
          if (!attached) attached = attachFile("bios.rom");
          attachFile("static.rom"); // Font ROM
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
      } else if (nodeName == "PC Engine" || nodeName == "SuperGrafx") {
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
    setEmulationRunning(false);
    std::lock_guard<std::recursive_mutex> lock(systemMutex);
    if (root) {
        root->unload();
        root.reset();
    }
    cachedPlayer1 = {}; // Reset Player 1 cache
    currentMedium.reset();
    secondaryMedium.reset();
    LOGI("System unloaded");
  }

  static auto connectDevices(Node::Object node) -> void {
    if (!node) return;
    auto ports = node->find<Node::Port>();
    s32 portIndex = 1;

    for (auto& port : ports) {
      LOGI("VFS: connectDevices - name='%s', type='%s', family='%s'", (const char*)port->name(), (const char*)port->type(), (const char*)port->family());
      if (port->type() == "Cartridge" || port->type() == "Compact Disc") {
        if (port->allocate()) {
            LOGI("VFS: Allocated %s port", (const char*)port->type());
            port->connect();
            LOGI("VFS: Successfully called port->connect() for %s", (const char*)port->type());
        } else {
            LOGE("VFS: FAILED to allocate %s port", (const char*)port->type());
        }
      } else if (port->type() == "Controller" || port->type() == "Control Pad") {
        string defaultDevice = "Gamepad";
        if (port->family() == "Nintendo 64") defaultDevice = (node && node->name() == "Arcade") ? "Aleck64" : "Gamepad";
        if (port->family() == "MSX") defaultDevice = "Controller";
        if (port->family() == "Mega Drive") defaultDevice = "Control Pad";
        if (node->name() == "PlayStation") {
            if (portIndex == 1) {
                defaultDevice = ps1AnalogMode ? "DualShock" : "Digital Gamepad";
            } else {
                defaultDevice = "Digital Gamepad";
            }
        }

        if (port->connected()) port->disconnect();

        if (port->allocate(defaultDevice)) {
            LOGI("VFS: Allocated %s controller on %s", (const char*)defaultDevice, (const char*)port->name());

            if (portIndex == 1) {
                auto peripherals = port->find<Node::Peripheral>();
                if (peripherals.size() > 0) {
                    cachedPlayer1 = peripherals[0];
                    LOGI("VFS: Cached Player 1 Peripheral: %s", (const char*)cachedPlayer1->name());
                }
            }
        }
        portIndex++;
      } else if (port->type() == "Keyboard") {
        string defaultLayout = "Japanese";
        if (port->allocate(defaultLayout)) {
            LOGI("VFS: Allocated %s keyboard on %s", (const char*)defaultLayout, (const char*)port->name());
        }
      }
    }
  }

  auto initialize(const char* systemNamePtr, const char* uriPtr) -> bool {
    string systemName = systemNamePtr;
    string uri = uriPtr;
    unloadSystem();
    std::unique_lock<std::recursive_mutex> lock(systemMutex);

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
    string tempPath = string{tempFilePath, "/phobos_rom_temp.", extension};

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
    if (systemName == "Arcade" || systemName == "Auto" || systemName.find("Nintendo 64")) {
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
    else if (lookup.find("Super Famicom") || lookup.find("SNES")) identifiedSystem = "Super Famicom";
    else if (lookup.find("Famicom") || lookup.find("NES")) identifiedSystem = "Famicom";
    else if (lookup.find("PlayStation") || lookup.find("PS1")) identifiedSystem = "PlayStation";
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
    if (!forceZipLoad && !isDisc && extension == "zip") {
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
      bool is64DD = (identifiedSystem == "Nintendo 64DD" || extension == "ndd" || extension == "d64");
      ::ares::Nintendo64::system.expansionPak = n64ExpansionPak;

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
    } else {
        LOGI("Ares: Falling back to Aleck 64 for Arcade");
        success = ::ares::Nintendo64::load(root, "[SETA] Aleck 64");
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

  auto runFrame() -> void {
    lock_guard<std::recursive_mutex> lock(systemMutex);
    if (root && !isPaused) {
        root->run();
        if (::ares::Nintendo64::vulkan.implementation) {
            if (const char* err = ::ares::Nintendo64::vulkan.crashed()) {
                LOGE("N64 RDP CRASH: %s", err);
            }
        }
    }
  }
  auto setFastBoot(bool enabled) -> void { fastBootAtomic = enabled; LOGI("Fast boot %s", enabled ? "enabled" : "disabled"); }
  auto setPause(bool paused) -> void { isPausedAtomic = paused; isPaused = paused; LOGI("Emulation %s", paused ? "paused" : "resumed"); }
  auto setFastForward(bool enabled) -> void { fastForwardAtomic = enabled; LOGI("Fast forward %s", enabled ? "enabled" : "disabled"); }
  auto setFastForwardSpeed(f32 speed) -> void { ffSpeedLimitAtomic = speed; LOGI("Fast forward speed set to %.1fx", (f64)speed); }
  auto resetSystem() -> void {
    resetRequestedAtomic.store(true);
    LOGI("System reset requested");
  }
  auto frameAdvance() -> void { lock_guard<recursive_mutex> lock(systemMutex); if (root) root->run(); }
  auto setMuteAudio(bool muted) -> void { muteAudioAtomic = muted; }
  auto setShader(const char* path) -> bool { return true; }
  auto saveState(const char* path) -> bool {
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> lock(systemMutex);
    if (!root) { isPausedAtomic.store(wasPaused); return false; }
    auto s = root->serialize(true);
    bool result = nall::file::write(path, {s.data(), s.size()});
    LOGI("Save state to %s: %s", path, result ? "success" : "failed");
    isPausedAtomic.store(wasPaused);
    return result;
  }
  auto loadState(const char* path) -> bool {
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> lock(systemMutex);
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
  auto setLogLevel(s32 level) -> void { currentLogLevel = (LogLevel)level; }
  auto setRegion(s32 regionIndex) -> void { regionPreference = regionIndex; }
  auto setN64Renderer(s32 mode) -> void { n64RendererMode = mode; LOGI("N64 renderer set to %s", mode == 0 ? "Vulkan" : "Software"); }
  auto setN64Recompiler(bool enabled) -> void { n64Recompiler = enabled; LOGI("N64 recompiler set to %s", enabled ? "enabled" : "disabled"); }
  auto setSkipBootRom(bool enabled) -> void { skipBootRom = enabled; LOGI("Skip Boot ROM set to %s", enabled ? "enabled" : "disabled"); }
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
  auto setFirmwarePath(const char* path) -> void { firmwarePath = path ? (string)path : ""; }
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
    if (nativeWindow) ANativeWindow_release(nativeWindow);
    nativeWindow = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
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
