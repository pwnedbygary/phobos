#include "PhobosRunner.hpp"
#include "PhobosHost.hpp"
#include <mia/mia.hpp>
#include <android/log.h>
#if defined(__ANDROID__)
#include "vfs_android.hpp"
#include <android/native_window_jni.h>
#include <sys/auxv.h>
#if defined(__aarch64__)
#include <asm/hwcap.h>
#endif
#endif
#include <fcntl.h>
#include <unistd.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include <pthread.h>
#include <sched.h>
#if defined(_WIN32)
#include <io.h>
#include <filesystem>
#endif
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <array>

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
#include <psp/psp.hpp>
#include <sfc/sfc.hpp>
#include <sg/sg.hpp>
#include <spec/spec.hpp>
#include <ws/ws.hpp>

#include <nall/encode/png.hpp>

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
  // N64 debug instrumentation gate: when OFF (default) the per-second
  // "N64 PC:" / "N64 STALL/HANG" diagnostics are skipped entirely so logs stay
  // clean and no per-second core-state reads cost CPU. Toggle ON from Settings
  // (N64 Experimental) or the pause menu when debugging freezes.
  std::atomic<bool> n64DebugLoggingAtomic{false};
  // Accessor for the ares core files (vi.cpp, rdp_device.cpp) so the
  // PhobosVI/PhobosRDP diagnostics are gated behind the same toggle.
  auto n64DebugLoggingEnabled() -> bool { return n64DebugLoggingAtomic.load(); }
  pthread_t emuThread = 0;
  std::atomic<bool> emuThreadRunning{false};
  // Set while unloadSystem() / the N64DD reload is tearing down a system
  // (freeing the ares singleton hardware: rdram.ram, cartridge.rom, dd.disk).
  // setEmulationRunning(true) refuses to spawn a fresh emu thread during this
  // window: the new thread copies `localRoot = root` (the OLD, half-torn-down
  // system) and runs CPU::LW against the freed buffers -> SIGSEGV on unload
  // (the 64DD quit -> reload crash). The thread is spawned by initialize() /
  // the N64DD reload itself AFTER the teardown completes.
  static std::atomic<bool> systemUnloading{false};
  // Identity of the CURRENT emulation thread. platform callbacks (audio) must
  // reject calls from an ABANDONED zombie thread: unloadSystem()'s abandon
  // path deliberately leaks a stuck emulation thread, and if it later unsticks
  // it keeps emulating the OLD system — it must never touch the NEW system's
  // shared audio pipeline (stream registry / ring buffer / AAudio stream).
  static std::atomic<pthread_t> currentEmuThread{0};

  static s32 romFd = -1;
  static s32 secondaryRomFd = -1;
  // A disc image the app can read by path, which the next load (or disc change) opens where it is
  // instead of copying the descriptor's file into mia_temp. Cleared once used.
  static string romPath;
  static string secondaryRomPath;
  // mia_temp copies of the running game's files (the game, a changed disc, an unzipped ROM), removed
  // when it unloads.
  static std::vector<string> tempCopies;
  static auto rememberTempCopy(const string& path) -> void {
    if (std::find(tempCopies.begin(), tempCopies.end(), path) == tempCopies.end()) tempCopies.push_back(path);
  }
  static auto removeTempCopies() -> void {
    for (auto& path : tempCopies) {
      if (::unlink((const char*)path) == 0) LOGI("Removed temp copy %s", (const char*)path);
    }
    tempCopies.clear();
  }
  static std::shared_ptr<mia::Pak> currentMedium;
  static std::shared_ptr<mia::Pak> secondaryMedium;
  // The running MSX game's data tape: a .wav kept with its saves, which the tape controls put in the
  // deck in place of the game's own tape (if it has one) and which records what the MSX saves to tape.
  static std::shared_ptr<mia::Pak> msxDataTape;
  static string msxDataTapePath;
  static bool msxDataTapeIn = false;
  static u64 msxGameTapePosition = 0;
  static std::atomic<bool> firstFrameRendered{false};
  // The host's audio device is open; readable without audioMutex (audio() runs per sample).
  static std::atomic<bool> audioStreamOpen{false};
  // Base name (no extension) of the currently loaded ROM — used to key the
  // per-game save files on disk (saves/<System>/<RomName>.save.ram etc.) so
  // different games never overwrite each other's saves.
  static string currentRomBase;

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
  static std::atomic<size_t> audioRingFill{0};  // audioRingSize, readable without audioMutex
  static std::atomic<f64> audioRateTrim{1.0};   // last dynamic rate control trim, for AudioDiag
  static std::thread audioThread;
  static std::atomic<bool> audioThreadRunning{false};
  static std::atomic<bool> audioThreadStop{false};

  // ── Audio stream registry (lockstep mixing) ────────────────────────────
  // Ares exposes one Node::Audio::Stream per sound source (Mega Drive:
  // YM2612 + PSG + CD-DA + PCM; Master System: PSG + YM2413; MSX: PSG +
  // SCC + tape; etc.). The emulation thread invokes platform->audio(stream)
  // for whichever stream has pending output, so the host must MIX all
  // streams sample-aligned — exactly what upstream desktop ares
  // Program::audio does. Streams register lazily on first sight and are
  // cleared in unloadSystem() (both the clean and abandon paths). The
  // registry holds strong refs; audio() copies it under the mutex so a
  // concurrent clear can't invalidate an in-flight mix.
  static std::mutex audioStreamsMutex;
  static std::vector<Node::Audio::Stream> audioStreams;
  // Bumped whenever the registry changes (stream added / cleared). The
  // emulation thread caches a snapshot keyed on this, so the hot audio()
  // path (the ZX ULA fires it millions of times/sec) avoids a mutex lock +
  // linear scan + heap copy on EVERY call. The emulation thread is the only
  // audio() caller (zombie gate) and is recreated per load, so the cache is
  // naturally scoped to one system's stream set.
  static std::atomic<u64> audioStreamsVersion{0};

  static auto audioThreadMain() -> void {
    // [Phobos] Audio diagnostics (Task 46 investigation, 2026-08-18): log ring
    // fill + AAudio xrun count once/sec while playing, so we can distinguish
    // CLOCK-DRIFT UNDERRUNS (ring → 0, xrun increments) from CD-DA GAP POPS
    // (ring healthy, xrun flat, pops coincide with CD track boundaries).
    auto diagStart = std::chrono::steady_clock::now();
    s64 lastXruns = 0;
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
        size_t first = std::min(take, audioRingCapacity - audioRingHead);
        memcpy(chunk.data(), audioRing.data() + audioRingHead, first * sizeof(f32));
        memcpy(chunk.data() + first, audioRing.data(), (take - first) * sizeof(f32));
        audioRingHead = (audioRingHead + take) % audioRingCapacity;
        audioRingSize -= take;
        audioRingFill.store(audioRingSize, std::memory_order_relaxed);
        // Wake the emulation thread's audio-pacing wait so it can resume
        // running frames as soon as the DAC has drained enough.
        audioCV.notify_one();
      }
      if (chunk.empty()) continue;

      bool open;
      {
        std::lock_guard<std::mutex> lock(audioMutex);
        open = phobos::host::audioOpen();
      }
      // Paused/closed: drop queued audio so stale samples never pop on resume.
      if (!open || isPausedAtomic.load()) continue;

      s32 total = (s32)chunk.size() / 2;
      s32 written = 0;
      // Blocking write is fine here (dedicated thread); the host caps each call
      // at 20ms so a wedged stream can't hang the thread forever.
      while (written < total) {
        s32 result = phobos::host::writeAudio(chunk.data() + written * 2, total - written);
        if (result > 0) written += result;
        else break; // stream stopped or error: drop the remainder
      }

      // [Phobos] Audio diagnostics (Task 46): 1x/sec log ring fill + xruns.
      auto nowDiag = std::chrono::steady_clock::now();
      auto diagElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(nowDiag - diagStart).count();
      if (diagElapsed >= 1000) {
        diagStart = nowDiag;
        s64 xruns = phobos::host::audioUnderruns();
        s64 newXruns = xruns - lastXruns;
        lastXruns = xruns;
        LOGI("AudioDiag: ring=%zu/%zu (%.0f%%) xruns+%lld (total %lld) trim=%+.3f%%",
             audioRingSize, (size_t)audioRingCapacity,
             (f64)audioRingSize * 100.0 / (f64)audioRingCapacity,
             (long long)newXruns, (long long)xruns,
             (audioRateTrim.load(std::memory_order_relaxed) - 1.0) * 100.0);
      }
    }
  }

  // Logical display size of the last presented frame (getVideoGeometry()),
  // written by the video thread and read by the UI for aspect-correct sizing.
  static std::atomic<f32> videoDisplayWidth{0.0f};
  static std::atomic<f32> videoDisplayHeight{0.0f};

  // Display size as ares desktop computes it: the core's scale and pixel aspect
  // describe the unrotated image, so undo ares' own 90/270 rotation first; the
  // frontend WonderSwan rotation swaps the axes once more.
  // tvPicture: the frame size follows the video mode (N64 scans out 240 or 480
  // lines at 640 dots, PS1 uses 256-640 dots) while the picture always fills a
  // 4:3 TV, and neither core's pixel aspect says so. Keep 4:3 at the frame's
  // line count so integer scaling still sees 240 or 480 lines.
  static auto recordVideoGeometry(const Node::Video::Screen& screen, u32 width, u32 height, bool frontendRotate, bool tvPicture) -> void {
    bool coreRotated = screen->rotation() == 90 || screen->rotation() == 270;
    f64 sourceWidth = coreRotated ? height : width;
    f64 sourceHeight = coreRotated ? width : height;
    f64 scaleX = screen->scaleX() > 0 ? screen->scaleX() : 1.0;
    f64 scaleY = screen->scaleY() > 0 ? screen->scaleY() : 1.0;
    f64 aspectX = screen->aspectX() > 0 ? screen->aspectX() : 1.0;
    f64 aspectY = screen->aspectY() > 0 ? screen->aspectY() : 1.0;
    f64 displayHeight = sourceHeight * scaleY;
    f64 displayWidth = tvPicture ? displayHeight * 4.0 / 3.0 : sourceWidth * scaleX * aspectX / aspectY;
    if (coreRotated != frontendRotate) std::swap(displayWidth, displayHeight);
    videoDisplayWidth.store((f32)displayWidth, std::memory_order_relaxed);
    videoDisplayHeight.store((f32)displayHeight, std::memory_order_relaxed);
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
  // Frame-to-frame intervals (ms) for the performance HUD's frame-time graph. Written by
  // the emulation thread; getFrameTimes() tolerates reading an element mid-update.
  static constexpr u32 FrameIntervalHistory = 240;
  static std::array<std::atomic<f32>, FrameIntervalHistory> frameIntervals{};
  static std::atomic<u32> frameIntervalCursor{0};
  static std::atomic<s32> emuThreadTid{0};
  static std::atomic<s32> emuThreadCore{-1};
  // Unpaused run time of the current emulation thread (one per loaded game), in µs.
  static std::atomic<u64> playedMicros{0};

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
  // matching on the emulation thread. Caches are keyed by RAW Node::Input*
  // (button.get()/axis.get()): they MUST be invalidated whenever the node tree
  // is rebuilt (connectDevices() re-allocates controller ports — PS1 analog
  // toggle, N64DD disk mount, reload). Otherwise a freed address recycled by
  // the allocator for a NEW node collides with a stale entry → the new control
  // binds to the OLD control's bit/slot (e.g. left stick reads R-stick or
  // nothing) — the "PS1 analog toggle degrades after 3rd toggle" bug.
  // inputCacheMutex guards these maps: input() runs on the emulation thread
  // while connectDevices() can clear them from the UI/JNI thread, so a clear
  // racing a lookup on std::map is UB (corruption) without the lock.
  struct ButtonBinding {
    u32 bits = 0;               // VirtualGamepad bits that press this button
    bool zxKeyboardKey = false; // ZX keyboard-matrix key (sourced from the key sets)
    bool playerOne = true;      // controller port 1, or not under a port (keyboards)
    u32 seenPresses = 0;        // pressGeneration(bits) at the last read (quick-tap delivery)
  };
  static std::map<const void*, ButtonBinding> inputButtonCache;
  static std::map<const void*, u32> inputAxisCache;  // axis slot + 1; 0 = unmapped
  static s32 inputCacheOrientation = -1;
  static std::mutex inputCacheMutex;

  // Quick taps: every rising edge of a VirtualGamepad bit bumps its counter
  // (setInput()). A button node whose bits were pressed since its last read
  // reads as pressed once, so a tap that starts and ends between two core polls
  // (touch taps can be shorter than a frame) still reaches the game — for every
  // node that maps the bit, including Neo Geo combo bits shared by two buttons.
  // Only recent presses replay: a press made while paused or while the game was
  // not polling (loading) must not surface later as a phantom press.
  static std::atomic<u32> bitPressCount[32];
  static std::atomic<s64> bitPressTimeMs[32];
  static constexpr s64 TAP_REPLAY_WINDOW_MS = 100;
  static auto steadyMs() -> s64 {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static auto pressGeneration(u32 bits) -> u32 {
    u32 generation = 0;
    for (u32 remaining = bits; remaining; remaining &= remaining - 1) {
      generation += bitPressCount[__builtin_ctz(remaining)].load(std::memory_order_acquire);
    }
    return generation;
  }
  static auto latestPressMs(u32 bits) -> s64 {
    s64 latest = 0;
    for (u32 remaining = bits; remaining; remaining &= remaining - 1) {
      latest = std::max(latest, bitPressTimeMs[__builtin_ctz(remaining)].load(std::memory_order_relaxed));
    }
    return latest;
  }

  static auto invalidateInputCaches() -> void {
    std::lock_guard<std::mutex> lock(inputCacheMutex);
    inputButtonCache.clear();
    inputAxisCache.clear();
    inputCacheOrientation = -1;
  }

  // On-screen keyboard state (ZX Spectrum / 128 keyboard, MSX keyboard,
  // ColecoVision keypad). setKeyboardKey() toggles membership;
  // AndroidPlatform::input() sources those buttons from this set so the core's
  // keyboard/keypad poll sees the press. keyboardKeyCount mirrors the set size
  // so input() can skip the lock while nothing is held.
  static std::mutex keyboardMutex;
  static std::set<string> keyboardKeysPressed;
  static std::atomic<u32> keyboardKeyCount{0};
  // Gamepad-scheme-derived keyboard keys (QAOP/ZXZX), set each frame from
  // setInput(); the keyboard input path checks both sets.
  static std::set<string> zxSchemeKeysPressed;

  // ZX gamepad control scheme: maps the gamepad (VirtualGamepad bits) onto
  // keyboard keys for games that don't support Kempston.
  // 0 = none (Kempston only), 1 = Q/P + Space (Manic Miner), 2 = Z/X + Space,
  // 3 = ELITE, 4 = CUSTOM, 5 = Sinclair 1, 6 = Sinclair 2, 7 = Cursor,
  // 8 = QAOP + Space.
  static std::atomic<s32> zxControlScheme{0};
  // ZX scheme-translation toggles (Layer 2 — orthogonal to per-core rebinding
  // which lives at Layer 1: physical input → VirtualGamepad bits).
  // zxStickToKeys: left-stick cardinal → same D-pad bits the scheme maps, so
  // the stick drives the scheme keys for ANY ZX game.
  // zxReversePitch: swap Up↔Down key mapping (aircraft-style: pull back =
  // pitch up), applies to stick AND d-pad together.
  static std::atomic<bool> zxStickToKeys{false};
  static std::atomic<bool> zxReversePitch{false};
  // Per-key rebind overrides: ZX keyboard label -> gamepad bit. When a key
  // has an entry here, the ZX scheme path uses it INSTEAD of the built-in
  // scheme mapping (ELITE/QAOP/ZXZX). Layer 2.5 — sits above the scheme
  // defaults, below Layer-1 (physical->bit) rebinding. Guarded by
  // keyboardMutex like the pressed-key sets.
  static std::map<string, u32> zxKeyBindings;

  // Map a VirtualGamepad bit to the ZX keyboard keys for the active scheme.
  // Start -> ENTER is universal (all schemes) since ENTER is used everywhere
  // on the ZX (menus, prompts, LOAD confirmation).
  static auto zxSchemeKeys(u32 bit, s32 scheme) -> std::vector<string> {
    std::vector<string> keys;
    auto add = [&](const char* a, const char* b = nullptr) {
      keys.push_back(a);
      if (b) keys.push_back(b);
    };
    // Start button -> ENTER (all schemes).
    if (bit == VirtualGamepad::Start) add("ENTER");
    switch (scheme) {
      case 1: // Q/P + Space: Manic Miner's left, right and jump
        if (bit == VirtualGamepad::Left)  add("Q");
        if (bit == VirtualGamepad::Right) add("P");
        if (bit == VirtualGamepad::A)     add("SPACE BREAK");
        break;
      // The Sinclair Interface 2 and Cursor joysticks are wired to number keys, so games
      // read them as the keyboard.
      case 5: // Sinclair 1: Interface 2's left port, keys 6-0
        if (bit == VirtualGamepad::Left)  add("6");
        if (bit == VirtualGamepad::Right) add("7");
        if (bit == VirtualGamepad::Down)  add("8");
        if (bit == VirtualGamepad::Up)    add("9");
        if (bit == VirtualGamepad::A)     add("0");
        break;
      case 6: // Sinclair 2: Interface 2's right port, keys 1-5
        if (bit == VirtualGamepad::Left)  add("1");
        if (bit == VirtualGamepad::Right) add("2");
        if (bit == VirtualGamepad::Down)  add("3");
        if (bit == VirtualGamepad::Up)    add("4");
        if (bit == VirtualGamepad::A)     add("5");
        break;
      case 7: // Cursor (Protek, AGF): the arrow keys 5-8, fire on 0
        if (bit == VirtualGamepad::Left)  add("5");
        if (bit == VirtualGamepad::Down)  add("6");
        if (bit == VirtualGamepad::Up)    add("7");
        if (bit == VirtualGamepad::Right) add("8");
        if (bit == VirtualGamepad::A)     add("0");
        break;
      case 8: // QAOP + Space, with M on B: the usual keys of games without joystick support
        if (bit == VirtualGamepad::Up)    add("Q");
        if (bit == VirtualGamepad::Down)  add("A");
        if (bit == VirtualGamepad::Left)  add("O");
        if (bit == VirtualGamepad::Right) add("P");
        if (bit == VirtualGamepad::A)     add("SPACE BREAK");
        if (bit == VirtualGamepad::B)     add("M");
        break;
      case 2: // ZXZX + Space
        if (bit == VirtualGamepad::Left)  add("Z");
        if (bit == VirtualGamepad::Right) add("X");
        if (bit == VirtualGamepad::A)     add("SPACE BREAK");
        break;
      case 3: // ELITE (Flight + Combat) — definitive Firebird 1985 manual layout
        // Flying: S=Dive, X=Climb, N=Roll Left, M=Roll Right, SPACE=Increase
        //   speed, SYMBOL SHIFT=Decrease speed, 1-4=Views.
        // Combat: A=Fire laser, T=Target missile, F=Fire missile, U=Unarm
        //   missile, E=ECM, W=Energy bomb, Q=Escape capsule.
        // Nav: H=Hyperspace, J=Torus jump drive, G+H=Intergalactic jump,
        //   C=Docking computer on/off, D=Distance to system.
        // Aircraft convention (pull back = climb): Up=S (dive/push forward),
        //   Down=X (climb/pull back). Reverse Pitch swaps them.
        // Gamepad mapping (14 inputs, flight-first):
        if (bit == VirtualGamepad::Up)    add(zxReversePitch.load() ? "X" : "S");
        if (bit == VirtualGamepad::Down)  add(zxReversePitch.load() ? "S" : "X");
        if (bit == VirtualGamepad::Left)  add("N");
        if (bit == VirtualGamepad::Right) add("M");
        if (bit == VirtualGamepad::A)     add("A");
        if (bit == VirtualGamepad::B)     add("C");       // Docking computer on/off
        if (bit == VirtualGamepad::X)     add("SPACE BREAK"); // Increase speed
        if (bit == VirtualGamepad::Y)     add("SYMBOL SHIFT");
        if (bit == VirtualGamepad::L1)    add("T");
        if (bit == VirtualGamepad::R1)    add("U");
        if (bit == VirtualGamepad::L2)    add("H");
        if (bit == VirtualGamepad::R2)    add("J");
        if (bit == VirtualGamepad::Select) add("1");
        break;
    }
    return keys;
  }

  static auto isZxKeyboardSystem(const string& systemName) -> bool {
    return systemName == "ZX Spectrum" || systemName == "ZX Spectrum 128";
  }

  // Systems whose keyboard or keypad keys can be held from the on-screen
  // controls (setKeyboardKey). ZX keys are keyboard-only (see input()); MSX
  // keys and the ColecoVision keypad also keep their gamepad-bit mapping.
  static auto isOnScreenKeyboardSystem(const string& systemName) -> bool {
    return isZxKeyboardSystem(systemName) || systemName == "MSX" || systemName == "MSX2" ||
           systemName == "ColecoVision";
  }

  static auto clearOnScreenKeys() -> void {
    std::lock_guard<std::mutex> lock(keyboardMutex);
    keyboardKeysPressed.clear();
    zxSchemeKeysPressed.clear();
    keyboardKeyCount.store(0);
  }

  // True if the button name is one of the ZX keyboard matrix labels. The
  // matrix has no "Up"/"Down"/"Left"/"Right"/"Fire" — those belong to the
  // Kempston joystick (and other port devices), so they must read from the
  // gamepad bitmask, NOT the keyboard-source (otherwise the ZX branch would
  // zero them every frame → Kempston joystick dead).
  static auto isZxKeyboardKey(const string& name) -> bool {
    static const string keys[] = {
      "CAPS SHIFT", "Z", "X", "C", "V",
      "A", "S", "D", "F", "G",
      "Q", "W", "E", "R", "T",
      "1", "2", "3", "4", "5",
      "0", "9", "8", "7", "6",
      "P", "O", "I", "U", "Y",
      "ENTER", "L", "K", "J", "H",
      "SPACE BREAK", "SYMBOL SHIFT", "M", "N", "B",
    };
    for (auto& k : keys) if (name == k) return true;
    return false;
  }

  // Determine which controller port (0-based player index) a button belongs to.
  // Walks up from the button node to find a "Controller Port N" ancestor. Returns
  // 0 for player 1; >0 for additional players. Non-controller inputs (keyboard,
  // etc.) have no such ancestor and return 0. Without this, resolveButtonBit maps
  // purely by leaf name, so P2's "A" resolves to the SAME gamepad bit as P1's "A"
  // -> a single pad drives both players (observed on Neo Geo KOF2003).
  static auto controllerPlayerIndex(Node::Input::Input input) -> s32 {
    Node::Object node = input;
    for (int depth = 0; depth < 8 && node; depth++) {
      string name = node->name();
      if (name.beginsWith("Controller Port")) {
        if (name.size() >= 1) {
          char c = name[name.size() - 1];
          if (c >= '1' && c <= '9') return c - '1';
        }
        return 0;
      }
      node = ares::Node::parent(node);
    }
    // Arcade boards (Aleck64 / SG-1000A) name buttons "Player 1 …" / "Player 2 …".
    string leaf = input->name();
    if (leaf.beginsWith("Player 1")) return 0;
    if (leaf.beginsWith("Player 2")) return 1;
    return 0;
  }

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
          // N64 B sits up and to the left of A. BUTTON_X holds that spot relative to BUTTON_A in
          // both common layouts: the left face button when A is at the bottom (Xbox codes) and
          // the top one when A is on the right (Nintendo codes).
          else if (nodeName == "B") b = VirtualGamepad::X;
          else if (nodeName == "C-Up")    b = VirtualGamepad::RS_Up;
          else if (nodeName == "C-Down")  b = VirtualGamepad::RS_Down;
          else if (nodeName == "C-Left")  b = VirtualGamepad::RS_Left;
          else if (nodeName == "C-Right") b = VirtualGamepad::RS_Right;
      } else if (systemName == "Arcade") {
          // Aleck64 / SG-1000A: "Player N …" leaves, plus cabinet Service/Test.
          string leaf = nodeName;
          if (leaf.beginsWith("Player 1 ") || leaf.beginsWith("Player 2 ")) leaf = leaf.slice(9);
          if (leaf == "Up") b = VirtualGamepad::Up;
          else if (leaf == "Down") b = VirtualGamepad::Down;
          else if (leaf == "Left") b = VirtualGamepad::Left;
          else if (leaf == "Right") b = VirtualGamepad::Right;
          else if (leaf == "Start") b = VirtualGamepad::Start;
          else if (leaf == "Coin") b = VirtualGamepad::Select;
          else if (leaf == "Button 1") b = VirtualGamepad::A;
          else if (leaf == "Button 2") b = VirtualGamepad::B;
          else if (leaf == "Button 3") b = VirtualGamepad::X;
          else if (leaf == "Button 4") b = VirtualGamepad::Y;
          else if (leaf == "Button 5") b = VirtualGamepad::L1;
          else if (leaf == "Button 6") b = VirtualGamepad::R1;
          else if (leaf == "Button 7") b = VirtualGamepad::L2;
          else if (leaf == "Button 8") b = VirtualGamepad::R2;
          else if (leaf == "Button 9") b = VirtualGamepad::L3;
          else if (nodeName == "Service") b = VirtualGamepad::Home;
          else if (nodeName == "Test") b = VirtualGamepad::R3;
      } else if (systemName == "Pocket Challenge V2") {
          // As upstream ares's desktop app maps them: Circle at the bottom of the face buttons, Clear on the
          // right, Pass on the left, View as Start and Escape as Select.
          if      (nodeName == "Circle") b = VirtualGamepad::A;
          else if (nodeName == "Clear")  b = VirtualGamepad::B;
          else if (nodeName == "Pass")   b = VirtualGamepad::X;
          else if (nodeName == "View")   b = VirtualGamepad::Start;
          else if (nodeName == "Escape") b = VirtualGamepad::Select;
      } else if (systemName == "PlayStation") {
          // DualShock uses L1, R1, L2, R2, L3, R3 explicitly
          if      (nodeName == "L1") b = VirtualGamepad::L1;
          else if (nodeName == "R1") b = VirtualGamepad::R1;
          else if (nodeName == "L2") b = VirtualGamepad::L2;
          else if (nodeName == "R2") b = VirtualGamepad::R2;
          else if (nodeName == "L3") b = VirtualGamepad::L3;
          else if (nodeName == "R3") b = VirtualGamepad::R3;
      } else if (systemName.beginsWith("Neo Geo") && !systemName.beginsWith("Neo Geo Pocket")) {
          // Neo Geo / Neo Geo CD 4-button default (Xbox-layout reference):
          // X=A, Y=B, A=C, B=D — and the shoulders/stick-click carry the
          // classic button combos (setValue's (buttons & b) != 0 test makes a
          // bitmask per core button work unchanged). Supersedes the
          // Genesis-heritage C->R1 / D->R2 single-bit mapping.
          if      (nodeName == "A") b = VirtualGamepad::X | VirtualGamepad::R1 | VirtualGamepad::L2;
          else if (nodeName == "B") b = VirtualGamepad::Y | VirtualGamepad::R1 | VirtualGamepad::L1 | VirtualGamepad::L2 | VirtualGamepad::R3;
          else if (nodeName == "C") b = VirtualGamepad::A | VirtualGamepad::R2 | VirtualGamepad::L1 | VirtualGamepad::L2 | VirtualGamepad::R3;
          else if (nodeName == "D") b = VirtualGamepad::B | VirtualGamepad::R2 | VirtualGamepad::R3;
      } else if (systemName == "Atari 2600") {
          // Console switches. Game Reset/Select are momentary; ares flips the
          // difficulty and TV Type switches on each press (a26/riot/io.cpp), so
          // momentary bits drive all of them. Without these, games that need
          // Game Reset to start could not be started at all.
          if      (nodeName == "Reset")            b = VirtualGamepad::Start;
          else if (nodeName == "Left Difficulty")  b = VirtualGamepad::L1;
          else if (nodeName == "Right Difficulty") b = VirtualGamepad::R1;
          else if (nodeName == "TV Type")          b = VirtualGamepad::L2;
      } else if (systemName == "Master System") {
          // The console's Pause button raises the NMI (ms/system/controls.cpp).
          if (nodeName == "Pause") b = VirtualGamepad::Start;
      } else if (systemName.beginsWith("Neo Geo Pocket")) {
          if (nodeName == "Option") b = VirtualGamepad::Start;
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

  // Per-emulation-thread audio state, in one thread_local: with minSdk < 29
  // every thread_local access is an emulated-TLS call, and audio() runs once
  // per output sample.
  //   pending: mixed samples not yet handed to the audio thread. Each hand-off
  //   can wake that thread with a futex syscall, so samples go over in blocks
  //   of audioPushSamples and at the end of each emulated frame.
  struct AudioThreadState {
    u64 streamsVersion = 0;
    std::vector<Node::Audio::Stream> streams;
    std::vector<f32> pending;
    // Dynamic rate control (updateAudioRateControl).
    f64 smoothedFill = 0.0;
    f64 appliedTrim = 1.0;
    u64 trimmedVersion = 0;
  };
  static constexpr size_t audioPushSamples = 256 * 2;  // 256 stereo frames, ~5 ms
  static auto audioThreadState() -> AudioThreadState& {
    thread_local AudioThreadState state;
    return state;
  }

  static auto pushAudio(std::vector<f32>& samples) -> void {
    if (samples.empty()) return;
    if (muteAudioAtomic) std::fill(samples.begin(), samples.end(), 0.0f);
    bool wasEmpty;
    {
      std::lock_guard<std::mutex> lock(audioMutex);
      if (audioRing.empty()) audioRing.resize(audioRingCapacity);
      const f32* source = samples.data();
      size_t count = samples.size();
      if (count > audioRingCapacity) {
        source += count - audioRingCapacity;
        count = audioRingCapacity;
      }
      // Overwrite the oldest samples when the emulator out-produces the DAC.
      size_t overflow = audioRingSize + count > audioRingCapacity ? audioRingSize + count - audioRingCapacity : 0;
      audioRingHead = (audioRingHead + overflow) % audioRingCapacity;
      audioRingSize -= overflow;
      wasEmpty = audioRingSize == 0;
      size_t tail = (audioRingHead + audioRingSize) % audioRingCapacity;
      size_t first = std::min(count, audioRingCapacity - tail);
      memcpy(audioRing.data() + tail, source, first * sizeof(f32));
      memcpy(audioRing.data(), source + first, (count - first) * sizeof(f32));
      audioRingSize += count;
      audioRingFill.store(audioRingSize, std::memory_order_relaxed);
    }
    // The audio thread only waits while the ring is empty.
    if (wasEmpty) audioCV.notify_one();
    samples.clear();
  }

  // Dynamic rate control. Emulated audio and the DAC run on different clocks that frame pacing
  // can't match exactly, so the ring would slowly fill (then drop samples) or run dry (then
  // underrun). Once per frame, every stream's resampling rate is trimmed by up to ±0.5%, too
  // little to hear, toward a quarter-full ring, which also leaves a cushion for late frames.
  static constexpr f64 audioRateControlRange = 0.005;
  static constexpr f64 audioRingTargetFill = 0.25;
  static auto updateAudioRateControl(AudioThreadState& state) -> void {
    f64 fill = (f64)audioRingFill.load(std::memory_order_relaxed) / (f64)audioRingCapacity;
    state.smoothedFill += (fill - state.smoothedFill) * 0.05;
    f64 error = std::clamp((audioRingTargetFill - state.smoothedFill) / audioRingTargetFill, -1.0, 1.0);
    f64 trim = 1.0 + audioRateControlRange * error;
    if (std::abs(trim - state.appliedTrim) < 0.0001 && state.trimmedVersion == state.streamsVersion) return;
    for (auto& stream : state.streams) stream->setResamplerTrim(trim);
    state.appliedTrim = trim;
    state.trimmedVersion = state.streamsVersion;
    audioRateTrim.store(trim, std::memory_order_relaxed);
  }

  static auto flushAudio() -> void {
    if (pthread_self() != currentEmuThread.load()) return;
    auto& state = audioThreadState();
    pushAudio(state.pending);
    updateAudioRateControl(state);
  }

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
  // VI Overclock percent (100 = native). Written to ::ares::Nintendo64::vi.
  // overclockPercent at load/reset; makes the VI generate frames faster so
  // the game's logic runs at a genuinely higher FPS (Mupen64Plus-FZ style).
  static std::atomic<s32> n64ViOverclock{100};
  // Count Per Operation (1-3, default 2) + R4300 Overclock factor (0-5,
  // 2^f) — Mupen64Plus-FZ style CPU timing knobs, written to
  // ::ares::Nintendo64::cpu.countPerOp / overclockFactor at load/reset.
  static std::atomic<s32> n64CountPerOp{2};
  static std::atomic<s32> n64CpuOverclock{0};
  // Asynchronous RDP (N64 Experimental, default off): SyncFull does not wait
  // for the GPU. Applies live to ::ares::Nintendo64::vulkan.asynchronousRdp.
  static std::atomic<bool> n64AsyncRdp{false};
  // Opt-in speed hacks (N64 Experimental, default off). Apply live.
  static std::atomic<bool> n64FasterSync{false};
  static std::atomic<bool> n64SkipCaches{false};
  static std::atomic<bool> n64RspTaskMode{false};
  // Keep the emulation thread on the fastest CPU cores (Settings > Emulation, default on).
  static std::atomic<bool> pinFastestCore{true};
  // Busy-wait for the next N64 frame instead of sleeping (Settings > Emulation, default off).
  static std::atomic<bool> busyWaitPacing{false};
  // Settings > Video. Overscan shows the border around the picture; cores without one ignore it.
  static std::atomic<bool> videoOverscan{false};
  static std::atomic<bool> videoColorEmulation{true};
  static std::atomic<bool> videoInterframeBlending{true};
  // Settings > Emulation > Performance and the pause menu: how much faster than real time a Neo Geo
  // CD game runs while its drive reads data (1 = real time). The drive itself keeps its speed.
  static std::atomic<s32> ngcdLoadSpeed{1};
  // The same for a ZX Spectrum game while a loader reads its tape. The tape plays at its real speed,
  // so every loader, custom and protected ones included, sees the timing it expects.
  static std::atomic<s32> zxLoadSpeed{1};
  // The same for an MSX game while its cassette motor runs the tape.
  static std::atomic<s32> msxLoadSpeed{1};
  // Settings > Emulation and the pause menu: the ZX Spectrum tape plays while a loader reads it and
  // stops once the game moves on (TapeDeck::detectLoader()), so multi-load games find their next part.
  static std::atomic<bool> zxTapeAuto{true};

  // setValue() skips modify() when the value is unchanged, and the cores only turn interframe
  // blending on in modify(), so it is called either way.
  static auto applyVideoSettings() -> void {
    if (!root) return;
    for (auto& screen : root->find<Node::Video::Screen>()) screen->setOverscan(videoOverscan);
    for (auto& setting : root->find<Node::Setting::Boolean>()) {
      bool value;
      if (setting->name() == "Color Emulation") value = videoColorEmulation;
      else if (setting->name() == "Interframe Blending") value = videoInterframeBlending;
      else continue;
      setting->setValue(value);
      setting->modify(value);
    }
  }

  // A thread that sleeps part of every frame reads to Android as a medium load: the fastest
  // core gets paused or clocked down, and heavy frames then overrun until it reacts. Busy-waiting
  // keeps the core as busy as fast-forward does, at a battery and heat cost, so it is only
  // used for N64, whose frames take a large share of the frame period.
  static auto waitUntil(std::chrono::steady_clock::time_point deadline, bool spin) -> void {
    if (!spin) return std::this_thread::sleep_until(deadline);
    #if defined(__aarch64__) && defined(__ANDROID__)
    // WFE clock-gates the core while the thread keeps running, so Android still sees a busy core.
    // Only the kernel's timer event stream (every 100 us) guarantees a wake-up; the last 200 us spin.
    static const bool eventStream = (getauxval(AT_HWCAP) & HWCAP_EVTSTRM) != 0;
    if (eventStream) {
      auto coarse = deadline - std::chrono::microseconds(200);
      while (std::chrono::steady_clock::now() < coarse) asm volatile("wfe" ::: "memory");
    }
    #endif
    while (std::chrono::steady_clock::now() < deadline) {
      #if defined(__aarch64__)
      asm volatile("yield" ::: "memory");
      #endif
    }
  }

  // The CPUs with the highest capacity (the prime core or big cluster), from cpu_capacity
  // or, on kernels without it, cpuinfo_max_freq. Empty when neither is readable for every CPU.
  static auto fastestCpus(s32 count) -> std::vector<s32> {
    for (const char* format : {"/sys/devices/system/cpu/cpu%d/cpu_capacity",
                               "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq"}) {
      std::vector<s64> score((size_t)std::max(count, 0), 0);
      bool complete = count > 0;
      for (s32 cpu = 0; cpu < count && complete; cpu++) {
        char path[96];
        snprintf(path, sizeof(path), format, cpu);
        long long value = 0;
        if (FILE* file = fopen(path, "r")) {
          if (fscanf(file, "%lld", &value) != 1) value = 0;
          fclose(file);
        }
        score[cpu] = value;
        complete = value > 0;
      }
      if (!complete) continue;
      s64 best = *std::max_element(score.begin(), score.end());
      std::vector<s32> cpus;
      for (s32 cpu = 0; cpu < count; cpu++) {
        if (score[cpu] == best) cpus.push_back(cpu);
      }
      return cpus;
    }
    return {};
  }

  // SETA Aleck64 reports root name "Arcade" (same as SG-1000A). Distinguish by
  // the configuration string set in Nintendo64::System::load.
  static auto isAleck64Session() -> bool {
    return root && root->name() == "Arcade" &&
        root->attribute("configuration") == "[SETA] Aleck 64";
  }
  static auto isN64VulkanSession() -> bool {
    #if defined(CORE_N64)
    return root && ::ares::Nintendo64::vulkan.enable &&
        (root->name() == "Nintendo 64" || isAleck64Session());
    #else
    return false;
    #endif
  }

  // With asynchronous RDP the GPU can still be writing RDRAM when the emulation
  // thread stops; wait for it before snapshotting or replacing that memory
  // (state save/load, reset). Callers hold runMutex, so no new RDP work starts.
  static auto drainN64RdpIfAsync() -> void {
    #if defined(CORE_N64)
    auto& vulkan = ::ares::Nintendo64::vulkan;
    if (isN64VulkanSession() &&
        (vulkan.asynchronousRdp.load() || vulkan.rdpWorkPending.load())) {
      vulkan.drainRdp();
    }
    #endif
  }
  static std::atomic<bool> skipBootRom{false};
  static bool ps1AnalogMode = true;
  static bool orientationVertical = false;
  static string customDriverPath;
  static string nativeLibraryDir;
  static string tempFilePath;
  static string homePath;
  static string savesPath;
  static string vulkanCachePath;
  // The folder the user picked for the PSP's memory stick; empty for the shared one in the saves folder.
  static string pspMemoryStickPath;
  static std::map<string, string> firmwareMap;

  // The 32X's boot ROMs: Sega's 68000 vector table and the two SH-2 boot ROMs, from the Firmware
  // screen when set there, else the copies ares bundles. A set file of the wrong size is ignored.
  struct Mega32XBootFile { const char* key; const char* name; const mia::Resource::Blob& bundled; };
  static const Mega32XBootFile mega32XBootFiles[] = {
    {"fw_32x_g", "vector.rom", mia::Resource::Mega32X::Vector},
    {"fw_32x_m", "sh2.boot.mrom", mia::Resource::Mega32X::SH2BootM},
    {"fw_32x_s", "sh2.boot.srom", mia::Resource::Mega32X::SH2BootS},
  };

  static auto readMega32XBootFile(const Mega32XBootFile& file) -> std::vector<u8> {
    if (auto it = firmwareMap.find(file.key); it != firmwareMap.end()) {
      auto data = nall::file::read(it->second);
      if (data.size() == file.bundled.size) return data;
    }
    return {file.bundled.data, file.bundled.data + file.bundled.size};
  }

  // Whether pak() has a Super Game Boy cartridge ROM (the ~256–512 KiB boot cart,
  // not the 256-byte SM83 boot ROM bundled in mia).
  static auto hasSuperGameBoyCart() -> bool {
    auto present = [](const string& path) {
      return nall::file::exists(path) && nall::file::size(path) >= 0x10000;
    };
    for (auto key : {"fw_sgb2", "fw_sgb1", "fw_sgb"}) {
      if (auto it = firmwareMap.find(key); it != firmwareMap.end() && present(it->second)) return true;
    }
    return false;
  }

  static auto superGameBoyCartPath() -> string {
    auto present = [](const string& path) {
      return nall::file::exists(path) && nall::file::size(path) >= 0x10000;
    };
    // Prefer SGB2 when both are set (dedicated oscillator, slightly later cart).
    // Load still tries later candidates if this file fails MIA load.
    for (auto key : {"fw_sgb2", "fw_sgb1", "fw_sgb"}) {
      if (auto it = firmwareMap.find(key); it != firmwareMap.end() && present(it->second)) return it->second;
    }
    return {};
  }

  // Candidate SGB cart paths in preference order (SGB2 → SGB1 → generic).
  static auto superGameBoyCartCandidates() -> std::vector<string> {
    auto present = [](const string& path) {
      return nall::file::exists(path) && nall::file::size(path) >= 0x10000;
    };
    std::vector<string> out;
    for (auto key : {"fw_sgb2", "fw_sgb1", "fw_sgb"}) {
      if (auto it = firmwareMap.find(key); it != firmwareMap.end() && present(it->second)) {
        out.push_back(it->second);
      }
    }
    return out;
  }

  // Whether pak() has a Mega CD BIOS to give the Mega Drive system pak.
  static auto hasMegaCDBios() -> bool {
    auto present = [](const string& path) { return nall::file::exists(path) && nall::file::size(path) > 0; };
    for (auto key : {"fw_mcd_us", "fw_mcd_jp", "fw_mcd_eu"}) {
      if (auto it = firmwareMap.find(key); it != firmwareMap.end() && present(it->second)) return true;
    }
    return present(string{homePath, "/System/Mega Drive/bios.rom"});
  }

  // Whether pak() has a System Card for the PC Engine Duo's BIOS, which ares doesn't ship; without one the
  // CD unit runs from an empty ROM and the game stays on a black screen.
  static auto hasPCEngineCDBios() -> bool {
    auto present = [](const string& path) { return nall::file::exists(path) && nall::file::size(path) > 0; };
    for (auto key : {"fw_pce_cd_3_jp", "fw_pce_cd_ge_jp"}) {
      if (auto it = firmwareMap.find(key); it != firmwareMap.end() && present(it->second)) return true;
    }
    return present(string{homePath, "/System/PC Engine/bios.rom"});
  }

  static auto firmwareSet(const char* key) -> bool {
    auto it = firmwareMap.find(key);
    return it != firmwareMap.end() && nall::file::exists(it->second) && nall::file::size(it->second) > 0;
  }

  // The LaserActive's PAC BIOSes, which ares doesn't ship: the SEGA PAC's for the Mega LD, the NEC PAC's
  // (PAC-N10, PAC-N1 or PCE-LP1) for the PC Engine LD.
  static auto hasLaserActiveSegaBios() -> bool {
    return firmwareSet("fw_laseractive_sega_us") || firmwareSet("fw_laseractive_sega_jp");
  }

  static constexpr const char* laserActiveNecBiosKeys[] = {"fw_laseractive_nec_us", "fw_laseractive_nec_jp", "fw_laseractive_nec_lp"};

  static auto hasLaserActiveNecBios() -> bool {
    for (auto key : laserActiveNecBiosKeys) if (firmwareSet(key)) return true;
    return false;
  }

  // The Mega CD's backup RAM as mia's Mega CD, Mega CD 32X and Mega LD system paks leave it
  // (mia/system/mega-cd.cpp): 8 KiB of 0xFF ending in the format the BIOS looks for.
  static auto megaCDBackupRam() -> std::vector<u8> {
    static constexpr u8 format[64] = {
      0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x00,0x00,0x00,0x00,0x40,
      0x00,0x7d,0x00,0x7d,0x00,0x7d,0x00,0x7d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x53,0x45,0x47,0x41,0x5f,0x43,0x44,0x5f,0x52,0x4f,0x4d,0x00,0x01,0x00,0x00,0x00,
      0x52,0x41,0x4d,0x5f,0x43,0x41,0x52,0x54,0x52,0x49,0x44,0x47,0x45,0x5f,0x5f,0x5f,
    };
    std::vector<u8> ram(8_KiB, 0xff);
    std::copy(std::begin(format), std::end(format), ram.end() - sizeof(format));
    return ram;
  }

  // The PC Engine CD's 2 KiB backup RAM: the loaded game's copy in [system]'s saves folder (where
  // flushSavesToDisk() writes it), else blank, as mia's PC Engine system pak starts it. PCD::load() reads it
  // when the system loads, before importIntoPak runs, so the saved copy has to be in the pak from the start.
  static auto pcEngineCDBackupRam(const string& system) -> std::vector<u8> {
    std::vector<u8> ram(2_KiB);
    if (!savesPath) return ram;
    string romKey = currentRomBase;
    romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
    if (romKey.size() == 0) romKey = "rom";
    auto saved = nall::file::read(string{savesPath, "/", system, "/", romKey, "/backup.ram"});
    if (saved.size() != ram.size()) return ram;
    LOGI("Saves: restored backup.ram (%zu bytes) for %s [%s]", saved.size(), (const char*)system, (const char*)romKey);
    return saved;
  }

  // N64 Player 1 controller pak ("None" | "Rumble Pak" | "Controller Pak").
  // Rumble state is polled from Kotlin; player1PakDir backs the Controller
  // Pak's save.pak (created on demand in pak() when a Controller Pak attaches).
  static string n64Pak = "None";
  static std::atomic<bool> rumbleState{false};
  static std::chrono::steady_clock::time_point lastRumbleOnTime{};
  static std::shared_ptr<vfs::directory> player1PakDir;

  // PS1 memory cards. ares leaves a card's storage to the frontend, as desktop ares keeps a card file
  // per game; the disc's pak has no save.card, so the card's writes were dropped. Each port gets its own
  // pak, saved as saves/PlayStation/<game>/save.card and save2.card, where <game> is the name Kotlin
  // gives without the disc number so all of a game's discs share the card.
  struct Ps1MemoryCard {
    std::shared_ptr<vfs::directory> pak;
    std::vector<u8> seen;   // contents at the last check
    std::vector<u8> saved;  // contents in the save file
    std::chrono::steady_clock::time_point changed{};
  };
  static Ps1MemoryCard ps1MemoryCards[2];
  static string ps1MemoryCardKey;
  static string ps1MemoryCardDir;
  static std::chrono::steady_clock::time_point ps1MemoryCardsChecked{};
  // The emulation thread's check and the pause/unload flush both write the cards.
  static std::mutex ps1MemoryCardMutex;

  static auto ps1MemoryCardDevice(u32 port) -> ::ares::PlayStation::MemoryCard* {
    auto& slot = port == 0 ? ::ares::PlayStation::memoryCardPort1 : ::ares::PlayStation::memoryCardPort2;
    return dynamic_cast<::ares::PlayStation::MemoryCard*>(slot.device.get());
  }

  static auto ps1MemoryCardFile(u32 port) -> const char* { return port == 0 ? "save.card" : "save2.card"; }

  // Before the ports connect: each pak holds its saved card, marked loaded so MemoryCard reads it.
  static auto loadPs1MemoryCards() -> void {
    std::lock_guard<std::mutex> lock(ps1MemoryCardMutex);
    string key = ps1MemoryCardKey ? ps1MemoryCardKey : currentRomBase;
    key.replace("/", "_"); key.replace("\\", "_"); key.replace(":", "_");
    if (!key) key = "rom";
    ps1MemoryCardDir = savesPath ? string{savesPath, "/PlayStation/", key, "/"} : string{};
    for (u32 port : range(2)) {
      auto& card = ps1MemoryCards[port];
      card = {};
      card.pak = std::make_shared<vfs::directory>();
      card.pak->append("save.card", 128_KiB);
      if (!ps1MemoryCardDir) continue;
      auto data = nall::file::read({ps1MemoryCardDir, ps1MemoryCardFile(port)});
      if (data.size() != 128_KiB) continue;
      if (auto fp = card.pak->write("save.card")) {
        fp->write({data.data(), (u32)data.size()});
        fp->setAttribute("loaded", true);
        LOGI("Saves: loaded PS1 memory card %u for [%s]", port + 1, (const char*)key);
      }
    }
  }

  // After the ports connect: what each card holds now is what its file holds.
  static auto trackPs1MemoryCards() -> void {
    std::lock_guard<std::mutex> lock(ps1MemoryCardMutex);
    for (u32 port : range(2)) {
      auto& card = ps1MemoryCards[port];
      card.seen.clear();
      card.saved.clear();
      if (auto device = ps1MemoryCardDevice(port)) {
        card.seen.assign(device->memory.data, device->memory.data + device->memory.size);
        card.saved = card.seen;
      }
    }
    ps1MemoryCardsChecked = std::chrono::steady_clock::now();
  }

  // Written to a temporary file and renamed over the card, so a crash mid-write keeps the old card.
  static auto writePs1MemoryCard(u32 port, const std::vector<u8>& data) -> bool {
    if (!ps1MemoryCardDir) return false;
    directory::create(ps1MemoryCardDir);
    string path = {ps1MemoryCardDir, ps1MemoryCardFile(port)};
    string temp = {path, ".tmp"};
    FILE* file = fopen((const char*)temp, "wb");
    if (!file) return false;
    bool ok = fwrite(data.data(), 1, data.size(), file) == data.size();
    ok = fflush(file) == 0 && ok;
    #if defined(_WIN32)
    ok = _commit(_fileno(file)) == 0 && ok;
    fclose(file);
    // rename() refuses to replace an existing file on Windows; std::filesystem::rename replaces it.
    std::error_code error;
    if (ok) std::filesystem::rename((const char*)temp, (const char*)path, error);
    ok = ok && !error;
    #else
    ok = fsync(fileno(file)) == 0 && ok;
    fclose(file);
    ok = ok && ::rename((const char*)temp, (const char*)path) == 0;
    #endif
    if (!ok) {
      ::unlink((const char*)temp);
      LOGE("Saves: couldn't write PS1 memory card %u (%s)", port + 1, (const char*)path);
      return false;
    }
    LOGI("Saves: wrote PS1 memory card %u (%s)", port + 1, (const char*)path);
    return true;
  }

  // Emulation thread, after a frame: a card the game wrote and then left alone for a second is saved,
  // so an in-game save survives a crash or the app being killed before the next pause.
  static auto checkPs1MemoryCards() -> void {
    auto now = std::chrono::steady_clock::now();
    if (now - ps1MemoryCardsChecked < std::chrono::milliseconds(500)) return;
    ps1MemoryCardsChecked = now;
    std::lock_guard<std::mutex> lock(ps1MemoryCardMutex);
    for (u32 port : range(2)) {
      auto& card = ps1MemoryCards[port];
      auto device = ps1MemoryCardDevice(port);
      if (!device || card.seen.size() != device->memory.size) continue;
      if (memcmp(device->memory.data, card.seen.data(), card.seen.size()) != 0) {
        memcpy(card.seen.data(), device->memory.data, card.seen.size());
        card.changed = now;
      } else if (card.seen != card.saved && now - card.changed >= std::chrono::seconds(1)) {
        if (writePs1MemoryCard(port, card.seen)) card.saved = card.seen;
      }
    }
  }

  // Pause and unload: any card with unsaved writes is saved at once. The pause path doesn't hold
  // runMutex and a frame can be halfway through writing a card, so the card is read between frames;
  // when a frame doesn't end in time, checkPs1MemoryCards saves the card after the next frame.
  static auto flushPs1MemoryCards() -> void {
    std::unique_lock<std::recursive_mutex> frame(*runMutex, std::defer_lock);
    for (u32 attempt = 0; !frame.try_lock(); attempt++) {
      if (attempt == 50) {
        LOGW("Saves: PS1 memory cards not flushed, a frame is still running");
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::lock_guard<std::mutex> lock(ps1MemoryCardMutex);
    for (u32 port : range(2)) {
      auto& card = ps1MemoryCards[port];
      auto device = ps1MemoryCardDevice(port);
      if (!device || card.saved.size() != device->memory.size) continue;
      std::vector<u8> data(device->memory.data, device->memory.data + device->memory.size);
      if (data == card.saved) continue;
      if (writePs1MemoryCard(port, data)) card.saved = card.seen = data;
    }
  }

  // Forward declaration — defined with the N64 setters below, but called from
  // unloadSystem() which appears earlier in this translation unit.
  static auto exportControllerPak() -> void;
  // Flush cartridge/battery saves to disk (defined with the setters below,
  // called from unloadSystem() and the pause path).
  static auto flushSavesToDisk() -> void;

  // N64 JIT hang-detector state (see emulationLoop).
  static u32 hangZeroSeconds = 0;
  // Stall-spin detector: if the guest PC is identical across consecutive 1s
  // samples while frames are still being produced (frozen-but-60fps), the CPU
  // is spinning in a wait loop that isn't resolving. Track it and dump the
  // full hardware state once we're sure it's stuck.
  static u64 stallLastPc = 0;
  static u32 stallSameCount = 0;
  static bool stallLogged = false;

  auto addLog(LogLevel level, string message) -> void {
    std::lock_guard<std::mutex> lock(logMutex);
    logBuffer.push_back({level, message});
    if (logBuffer.size() > 5000) logBuffer.pop_front();
  }

  static std::atomic<u32> emuThreadGeneration{0};

  // ── Abandoned ("zombie") emulation threads ─────────────────────────────
  // When unloadSystem() / the N64DD reload path cannot wait out a frame that
  // is taking too long (>2s: Vulkan fence stalls, pipeline-compile storms),
  // the emu thread is deliberately leaked (it holds a localRoot shared_ptr,
  // so the node tree stays alive). BUT the N64 core keeps ALL emulated state
  // (rdram.ram, cartridge.rom, dd.disk, ...) in namespace-global singletons,
  // so the NEXT ::ares::Nintendo64::load() → System::unload() frees those
  // buffers underneath the still-running zombie → SIGSEGV in the interpreter
  // (CPU::LW / RSP DMA) — the "crash while unloading" bug. N64 run() always
  // returns (all fence waits are bounded), so a "stuck" frame eventually
  // completes and the zombie exits at its loop-top generation check. We keep
  // the zombie's pthread_t + an exit flag and join it (bounded) BEFORE any
  // N64 load that would re-initialize the singletons.
  struct EmuThreadCookie {
    u32 generation = 0;
    std::shared_ptr<std::atomic<bool>> exited = std::make_shared<std::atomic<bool>>(false);
  };
  static std::shared_ptr<EmuThreadCookie> currentEmuThreadCookie;
  static std::mutex zombieThreadsMutex;
  static std::vector<std::pair<pthread_t, std::shared_ptr<std::atomic<bool>>>> zombieThreads;

  auto emulationLoop(u32 generation) -> void {
    #if defined(__ANDROID__)
    setpriority(PRIO_PROCESS, 0, -10);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    s32 num_cores = sysconf(_SC_NPROCESSORS_CONF);
    for (s32 i = std::max(0, num_cores - 4); i < num_cores; i++) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    cpu_set_t fastestSet;
    CPU_ZERO(&fastestSet);
    for (s32 cpu : fastestCpus(num_cores)) CPU_SET(cpu, &fastestSet);
    if (CPU_COUNT(&fastestSet) == 0) fastestSet = cpuset;
    bool pinnedToFastest = false;
    #endif
    u32 slowFrames = 0;
    s64 longestFrameIntervalUs = 0;

    // Absolute frame deadline for pacing (see below). Persists across the
    // loop so sleep overshoot never compounds frame-to-frame.
    auto frameDeadline = std::chrono::steady_clock::now();
    auto lastFrameStart = frameDeadline;
    bool haveLastFrameStart = false;
    // Frames left of a Neo Geo CD load boost: it lasts half a second past the last data read, so
    // it carries across the BIOS's short pauses between files; a ZX tape's, half a second past the
    // last loader read.
    u32 loadBoostFrames = 0;
    s32 loadBoostSpeed = 1;
    // Only the Android HUD reads it, to sample this thread's CPU time from /proc.
    #if defined(__ANDROID__)
    emuThreadTid.store((s32)gettid(), std::memory_order_relaxed);
    #endif
    // HUD history is per thread: the abandon and 64DD-reload paths replace the
    // thread without going through unloadSystem()'s clean-path reset.
    emuThreadCore.store(-1, std::memory_order_relaxed);
    frameIntervalCursor.store(0, std::memory_order_release);
    playedMicros.store(0, std::memory_order_relaxed);

    while (emulationRunning && emuThreadGeneration == generation) {
      // Take a local shared_ptr copy so the zombie thread holds a
      // reference to the N64 System even after the main thread
      // replaces the global 'root'. Prevents use-after-free in the
      // abandon path.
      auto localRoot = root;

      if (resetRequestedAtomic.exchange(false)) {
        std::lock_guard<std::recursive_mutex> lock(*runMutex);
        if (localRoot) {
            drainN64RdpIfAsync();
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
        if (haveLastFrameStart) {
          s64 intervalUs = std::chrono::duration_cast<std::chrono::microseconds>(start - lastFrameStart).count();
          u32 cursor = frameIntervalCursor.load(std::memory_order_relaxed);
          frameIntervals[cursor % FrameIntervalHistory].store((f32)intervalUs / 1000.0f, std::memory_order_relaxed);
          frameIntervalCursor.store(cursor + 1, std::memory_order_release);
          if (intervalUs > 0) playedMicros.fetch_add((u64)intervalUs, std::memory_order_relaxed);
          if (intervalUs > 20000) slowFrames++;
          longestFrameIntervalUs = std::max(longestFrameIntervalUs, intervalUs);
        }
        lastFrameStart = start;
        haveLastFrameStart = true;
        // Re-applied every frame: Android resets thread affinity whenever it moves the
        // app between cpusets. The call fails (EINVAL) while the SoC keeps the fastest
        // core paused under light load (Qualcomm core control), leaving the scheduler's choice.
        #if defined(__ANDROID__)
        bool pinFastest = pinFastestCore.load(std::memory_order_relaxed);
        if (pinFastest) sched_setaffinity(0, sizeof(cpu_set_t), &fastestSet);
        else if (pinnedToFastest) sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
        pinnedToFastest = pinFastest;
        emuThreadCore.store((s32)sched_getcpu(), std::memory_order_relaxed);
        #endif
        {
            std::lock_guard<std::recursive_mutex> lock(*runMutex);
            if (localRoot) {
                #if defined(CORE_N64)
                ::ares::Nintendo64::cpu.idleSkip.store(fastForwardAtomic.load(std::memory_order_relaxed),
                                                       std::memory_order_relaxed);
                #endif
                // Set before the frame, so a change made in the pause menu applies from the first frame after it.
                bool zx = isZxKeyboardSystem(localRoot->name());
                if (zx) ::ares::ZXSpectrum::tapeDeck.autoControl = zxTapeAuto.load(std::memory_order_relaxed);
                localRoot->run();
                if (localRoot->name() == "PlayStation") checkPs1MemoryCards();
                if (ngcdLoadSpeed.load(std::memory_order_relaxed) > 1 && localRoot->name() == "Neo Geo CD") {
                    auto& cdd = ::ares::NeoGeo::cdd;
                    if ((cdd.statusCdc & 0x01) && (cdd.control & 0x0100)) loadBoostFrames = 30;
                    else if (loadBoostFrames) loadBoostFrames--;
                    loadBoostSpeed = ngcdLoadSpeed.load(std::memory_order_relaxed);
                } else if (zx) {
                    // While a loader reads the playing tape: loaders read its signal about a thousand
                    // times a frame, games reading the keyboard a few dozen, so a game that starts
                    // before its tape ends runs at its real speed. The reads are counted per frame at
                    // every speed, so raising the speed mid-tape goes by this frame's. The hold
                    // bridges the gaps between a tape's blocks.
                    auto& deck = ::ares::ZXSpectrum::tapeDeck;
                    bool loaderReading = deck.playing() && deck.reads > 200;
                    deck.reads = 0;
                    loadBoostSpeed = zxLoadSpeed.load(std::memory_order_relaxed);
                    if (loadBoostSpeed <= 1) loadBoostFrames = 0;
                    else if (loaderReading) loadBoostFrames = 25;
                    else if (loadBoostFrames) loadBoostFrames--;
                } else if (localRoot->name().beginsWith("MSX")) {
                    // While the motor runs the tape: the BIOS and loaders switch it on only to read the
                    // tape. The hold bridges the motor's short stops between a tape's blocks.
                    loadBoostSpeed = msxLoadSpeed.load(std::memory_order_relaxed);
                    if (loadBoostSpeed <= 1) loadBoostFrames = 0;
                    else if (::ares::MSX::tapeDeck.playing()) loadBoostFrames = 25;
                    else if (loadBoostFrames) loadBoostFrames--;
                } else {
                    loadBoostFrames = 0;
                }
            }
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        flushAudio();
        auto end = std::chrono::steady_clock::now();

        // Per-core pacing: the cap target is derived from the core's native
        // refresh rate (60/75/50 Hz) instead of a hardcoded 60 FPS, so
        // WonderSwan (~75 Hz) and PAL cores are no longer throttled.
        double refreshRate = refreshRateAtomic.load();
        bool spinWait = busyWaitPacing.load(std::memory_order_relaxed) && localRoot
          && localRoot->name() == "Nintendo 64";

        // A load boost paces like fast forward, capped at the chosen speed.
        bool loadBoost = loadBoostFrames && !fastForwardAtomic;
        if (fastForwardAtomic || loadBoost) {
            f64 speed = loadBoost ? (f64)loadBoostSpeed : (f64)ffSpeedLimitAtomic;
            if (speed > 0.0) {
                f64 targetFrameTime = (1000000.0 / refreshRate) / speed;
                auto actualFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                if (actualFrameTime < targetFrameTime) {
                    waitUntil(end + std::chrono::microseconds((s64)(targetFrameTime - (f64)actualFrameTime)), spinWait);
                }
            }
        }

        lastFrameTime = (u64)std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        avgFrameTime = avgFrameTime * 0.9 + (f64)lastFrameTime * 0.1;
        frameCount++;

        // Pacing (non-fast-forward): hold an ABSOLUTE frame deadline so
        // sleep overshoot never compounds frame-to-frame (the old
        // sleep_for(remaining-budget) approach drifted 60fps to ~52 and
        // drained the GPU pipeline each frame, exposing fence latency).
        //
        // Pure deadline pacing (no audio-ring condition): earlier we tried
        // also waiting for the audio ring to drain below a target, but that
        // over-throttled audio-heavy cores (Mega CD/Genesis YM2612 triple-
        // stream fills the ring faster than the DAC drains it → 34fps) and
        // under-throttled light-audio cores (Atari 2600 ran 119fps because
        // the ring was always below target → no wait). The absolute video
        // deadline is the correct universal pace for every core.
        if (!fastForwardAtomic && !loadBoost) {
          double frameTarget = 1000000.0 / refreshRate;
          auto period = std::chrono::microseconds((s64)frameTarget);
          frameDeadline += period;
          waitUntil(frameDeadline, spinWait);
          // Snap the deadline forward only if we're behind by a FULL frame
          // or more (a genuinely heavy frame / hitch). Small sleep overshoot
          // (waking slightly after the deadline — normal on Android) must
          // NOT trigger a full-period advance: doing so advances the
          // deadline 2x per wall-clock frame and locks the core at half
          // speed (N64 ran 30fps, MD-family 34fps). Small overshoot is
          // absorbed by the next iteration running back-to-back, keeping
          // the average locked to the target rate.
          auto now = std::chrono::steady_clock::now();
          while (now > frameDeadline + period) {
            frameDeadline += period;
          }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatsUpdateTime).count();
        if (elapsed >= 1000) {
            currentFps = (f64)frameCount * 1000.0 / (f64)elapsed;
            #if defined(CORE_N64)
            if (root && root->name() == "Nintendo 64") {
              auto& rc = ::ares::Nintendo64::cpu.recompiler;
              LOGI("Emulation Stats: FPS=%.1f, AvgFrameTime=%.2fms LongestFrame=%.1fms Over20ms=%u Target=%.3f linkTaken=%llu cand=%llu miss=%llu budget=%llu irq=%llu dirty=%llu",
                (f64)currentFps, (f64)avgFrameTime / 1000.0,
                (f64)longestFrameIntervalUs / 1000.0, slowFrames, refreshRate,
                (unsigned long long)rc.linkTaken, (unsigned long long)rc.linkCandidates,
                (unsigned long long)rc.linkAbortNoTarget, (unsigned long long)rc.linkAbortBudget,
                (unsigned long long)rc.linkAbortIrq, (unsigned long long)rc.linkAbortDirty);
            } else
            #endif
            {
              LOGI("Emulation Stats: FPS=%.1f, AvgFrameTime=%.2fms LongestFrame=%.1fms Over20ms=%u Target=%.3f",
                (f64)currentFps, (f64)avgFrameTime / 1000.0, (f64)longestFrameIntervalUs / 1000.0, slowFrames, refreshRate);
            }
            frameCount = 0;
            slowFrames = 0;
            longestFrameIntervalUs = 0;
            lastStatsUpdateTime = now;

            // Per-second perf profile (N64 only, gated by debug logging so it
            // costs nothing normally): tells us whether the core is CPU-bound
            // (cpuCycles + icache/dcache tag churn) or RSP-bound (rsp cycles),
            // so we know which lever to pull for a CPU-heavy game.
            #if defined(CORE_N64)
            if (n64DebugLoggingAtomic.load() && root && root->name() == "Nintendo 64") {
              auto& prof = ::ares::Nintendo64::cpu.profile;
              s64 rspCycles = ::ares::Nintendo64::rsp.dma.clock;  // cumulative-ish
              LOGI("N64 Profile: cpuCycles=%lld exc=%lld icache(H=%lld M=%lld W=%lld) dcache(H=%lld M=%lld W=%lld) rspDmaClk=%lld",
                (long long)prof.cpuCycles, (long long)prof.cpuCyclesExc,
                (long long)prof.icacheHits, (long long)prof.icacheMisses, (long long)prof.icacheWritebacks,
                (long long)prof.dcacheHits, (long long)prof.dcacheMisses, (long long)prof.dcacheWritebacks,
                (long long)rspCycles);
            }
            #endif

            // N64 hang/stall diagnostic (Conker's BFD pub/pause-menu freezes):
            //  1) CPU truly halted (FPS ~0)  → "N64 HANG"
            //  2) CPU SPINNING (FPS stays high, screen frozen, SAME PC every
            //     second) → "N64 STALL" — the guest is in a wait loop that
            //     isn't resolving. Dump the FULL hardware state: what is it
            //     waiting on (RSP DMA? RDP busy? SI/PI DMA? an interrupt that
            //     never fires? a scheduler event far in the future?).
            #if defined(CORE_N64)
            if (n64DebugLoggingAtomic.load() && root && root->name() == "Nintendo 64" && !isPausedAtomic.load()) {
                u64 pc = ::ares::Nintendo64::cpu.ipu.pc;
                auto& status = ::ares::Nintendo64::cpu.scc.status;
                auto& cause = ::ares::Nintendo64::cpu.scc.cause;
                // JIT compiles cached RDRAM only. If the spinning PC is NOT in
                // RDRAM, the code is interpreter-run and the stall is a guest
                // hardware wait whose resolution depends on how often the
                // scheduler steps (JitInterleaving).
                bool jittable = (pc >= 0x8000'0000ull && pc < 0x8040'0000ull);

                auto dumpStall = [&](const char* tag) {
                    auto& rspStatus = ::ares::Nintendo64::rsp.status;
                    auto& rdpCmd    = ::ares::Nintendo64::rdp.command;
                    auto& siIo      = ::ares::Nintendo64::si.io;
                    auto& piIo      = ::ares::Nintendo64::pi.io;
                    auto& aiIo      = ::ares::Nintendo64::ai.io;
                    auto& viIo      = ::ares::Nintendo64::vi.io;
                    // [Phobos diag] Disassemble the instructions at the spin
                    // PC so we can see WHAT the guest is polling (RSP status?
                    // RDP status? a memory flag?). PC is a u64 (sign-extended
                    // KSEG0); mask to 32-bit for the disassembler.
                    auto pc32 = (u32)pc;
                    string disasm;
                    for (int i = 0; i < 4; i++) {
                        u32 insn = (u32)::ares::Nintendo64::cpu.readDebug<::ares::Nintendo64::Word>(pc32 + i * 4);
                        string text = ::ares::Nintendo64::cpu.disassembler.disassemble(pc32 + i * 4, insn);
                        disasm.append("\n  ["); disasm.append(hex(pc32 + i * 4, 8L)); disasm.append("] "); disasm.append(text);
                    }
                    // [Phobos diag] Also disassemble the exception vector
                    // (0x80000180) dispatch + the fatal-trap region around the
                    // spin PC so we can see the handler's check that leads to
                    // the hang (Mischief Makers: beq-self at 0x800008b8).
                    u32 vecBase = 0x80000180u;
                    string vecDisasm;
                    for (int i = 0; i < 12; i++) {
                        u32 insn = (u32)::ares::Nintendo64::cpu.readDebug<::ares::Nintendo64::Word>(vecBase + i * 4);
                        string text = ::ares::Nintendo64::cpu.disassembler.disassemble(vecBase + i * 4, insn);
                        vecDisasm.append("\n  ["); vecDisasm.append(hex(vecBase + i * 4, 8L)); vecDisasm.append("] "); vecDisasm.append(text);
                    }
                    u32 trapStart = (pc32 & ~0x3fu) - 0x80;
                    string trapDisasm;
                    for (int i = 0; i < 24; i++) {
                        u32 addr = trapStart + i * 4;
                        u32 insn = (u32)::ares::Nintendo64::cpu.readDebug<::ares::Nintendo64::Word>(addr);
                        string text = ::ares::Nintendo64::cpu.disassembler.disassemble(addr, insn);
                        trapDisasm.append("\n  ["); trapDisasm.append(hex(addr, 8L)); trapDisasm.append("] "); trapDisasm.append(text);
                    }
                    // [Phobos diag] Disassemble the game's REAL IRQ handler
                    // (the vector jumps to it: k0 = 0x800A5FC8) so we can see
                    // what it reads that leads to the fatal trap.
                    u32 handlerBase = 0x800a5fc8u;
                    string handlerDisasm;
                    for (int i = 0; i < 32; i++) {
                        u32 addr = handlerBase + i * 4;
                        u32 insn = (u32)::ares::Nintendo64::cpu.readDebug<::ares::Nintendo64::Word>(addr);
                        string text = ::ares::Nintendo64::cpu.disassembler.disassemble(addr, insn);
                        handlerDisasm.append("\n  ["); handlerDisasm.append(hex(addr, 8L)); handlerDisasm.append("] "); handlerDisasm.append(text);
                    }
                    LOGW("%s: PC=0x%08llx jittable=%d FPS=%.1f mask=%02x pend=%02x "
                         "IE=%d EXL=%d ERL=%d exc=%d clock=%lld thr=%u "
                         "rsp(halt=%d broken=%d dmaBusy=%d dmaFull=%d) "
                         "rdp(pipe=%d buf=%d crash=%d frz=%d cur=%d end=%d) "
                         "si(dmaBusy=%d ioBusy=%d pend=%d) pi(dmaBusy=%d ioBusy=%d "
                         "latch=%d) ai(dmaCnt=%d dmaEn=%d) vi(vc=%d fld=%d) "
                         "queue(next=%d) EPC=0x%08llx%s%s%s%s",
                         tag, (unsigned long long)pc, (int)jittable, (double)currentFps,
                         (int)status.interruptMask, (int)cause.interruptPending,
                         (int)status.interruptEnable, (int)status.exceptionLevel,
                         (int)status.errorLevel, (int)cause.exceptionCode,
                         (long long)::ares::Nintendo64::cpu.clock,
                         (unsigned)::ares::scheduler.threads(),
                         (int)rspStatus.halted, (int)rspStatus.broken,
                         (int)::ares::Nintendo64::rsp.dma.busy.any(), (int)::ares::Nintendo64::rsp.dma.full.any(),
                         (int)rdpCmd.pipeBusy, (int)rdpCmd.bufferBusy,
                         (int)rdpCmd.crashed, (int)rdpCmd.freeze,
                         (int)rdpCmd.current, (int)rdpCmd.end,
                         (int)siIo.dmaBusy, (int)siIo.ioBusy, (int)siIo.readPending,
                         (int)piIo.dmaBusy, (int)piIo.ioBusy, (int)piIo.busLatch,
                         (int)aiIo.dmaCount, (int)aiIo.dmaEnable,
                         (int)viIo.vcounter, (int)viIo.field,
                         (int)::ares::Nintendo64::queue.timeToNextEvent(),
                         (unsigned long long)(u64)::ares::Nintendo64::cpu.scc.epc,
                         (const char*)disasm.data(),
                         (const char*)vecDisasm.data(),
                         (const char*)trapDisasm.data(),
                         (const char*)handlerDisasm.data());
                };

                if (currentFps <= 0.5) {
                    if (++hangZeroSeconds >= 2) {
                        dumpStall("N64 HANG");
                    }
                } else {
                    hangZeroSeconds = 0;
                    if (pc == stallLastPc) {
                        if (++stallSameCount >= 3) {
                            // Confirmed stall: log the full state once, then
                            // keep logging every second while it persists.
                            if (!stallLogged) {
                                stallLogged = true;
                                dumpStall("N64 STALL");
                            } else {
                                dumpStall("N64 STALL(cont)");
                            }
                        }
                    } else {
                        stallSameCount = 0;
                        stallLogged = false;
                    }
                    stallLastPc = pc;
                    LOGI("N64 PC: 0x%08llx (FPS=%.1f) jittable=%d mask=%02x pend=%02x IE=%d exc=%d",
                         (unsigned long long)pc, (double)currentFps, (int)jittable,
                         (int)status.interruptMask, (int)cause.interruptPending,
                         (int)status.interruptEnable, (int)cause.exceptionCode);
                }
            } else {
                hangZeroSeconds = 0;
                stallSameCount = 0;
                stallLogged = false;
            }
            #endif
        }
      } else {
        // A paused gap is not a frame interval.
        haveLastFrameStart = false;
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
    // The cookie carries the exit flag the zombie-parking machinery uses to
    // detect when the thread has finished its in-flight frame. The heap copy
    // is owned by the thread; the shared_ptr here keeps the flag alive.
    auto cookie = std::make_shared<EmuThreadCookie>();
    cookie->generation = gen;
    currentEmuThreadCookie = cookie;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    #if !defined(__ANDROID__)
    // The N64 CPU runs on this thread's own stack, and macOS gives new threads only 512 KiB.
    pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
    #endif
    pthread_create(&emuThread, &attributes, [](void* arg) -> void* {
        std::unique_ptr<EmuThreadCookie> cookie((EmuThreadCookie*)arg);
        emulationLoop(cookie->generation);
        cookie->exited->store(true, std::memory_order_release);
        emuThreadRunning = false;
        return nullptr;
    }, new EmuThreadCookie(*cookie));
    pthread_attr_destroy(&attributes);
    currentEmuThread.store(emuThread);
  }

  auto setEmulationRunning(bool running) -> void {
    if (emulationRunning == running) return;
    if (running && systemUnloading.load()) {
      LOGW("setEmulationRunning(true) deferred: system teardown in progress");
      return;
    }
    emulationRunning = running;
    if (running) ensureThread();
  }

  // Wait for parked zombie threads to finish their in-flight frame and exit.
  // MUST be called before ::ares::Nintendo64::load() (or anything that
  // re-initializes the N64 singleton hardware) and before spawning a fresh
  // emulation thread. N64 run() always returns (all fence waits are bounded),
  // so a "stuck" frame eventually completes and the zombie exits at its
  // loop-top check; this waits it out so it can't race the teardown.
  static auto joinAbandonedThreads(int budgetMs = 10000) -> void {
    std::lock_guard<std::mutex> lock(zombieThreadsMutex);
    for (auto it = zombieThreads.begin(); it != zombieThreads.end(); ) {
      pthread_t handle = it->first;
      auto& exited = it->second;
      if (exited->load(std::memory_order_acquire)) {
        pthread_join(handle, nullptr);
        LOGI("Zombie thread %lu joined (had already exited)", (unsigned long)handle);
        it = zombieThreads.erase(it);
        continue;
      }
      bool joined = false;
      for (int waited = 0; waited < budgetMs; waited += 10) {
        if (exited->load(std::memory_order_acquire)) {
          pthread_join(handle, nullptr);
          LOGI("Zombie thread %lu joined after ~%dms", (unsigned long)handle, waited);
          joined = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (joined) {
        it = zombieThreads.erase(it);
      } else {
        LOGW("Zombie thread %lu still running after %dms — leaving parked", (unsigned long)handle, budgetMs);
        ++it;
      }
    }
  }

  // Park the currently-running emulation thread as a zombie: it is stuck
  // inside root->run() (a frame taking >2s) and we are about to drop the
  // handle. The next N64 load joins it via joinAbandonedThreads() BEFORE
  // touching the singleton hardware, closing the use-after-free window.
  static auto parkZombieThread() -> void {
    if (!emuThread) return;
    {
      std::lock_guard<std::mutex> lock(zombieThreadsMutex);
      zombieThreads.push_back({emuThread,
          currentEmuThreadCookie ? currentEmuThreadCookie->exited
                                 : std::make_shared<std::atomic<bool>>(false)});
      LOGW("Zombie thread %lu parked (joined before the next N64 load)", (unsigned long)emuThread);
    }
    emuThread = 0;
    emuThreadRunning = false;
    currentEmuThread.store(0);
    currentEmuThreadCookie.reset();
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

      u32 buttons = (u32)inputState.buttons.load();
      f32 lx = inputState.lx.load();
      f32 ly = inputState.ly.load();
      f32 rx = inputState.rx.load();
      f32 ry = inputState.ry.load();

      // Invalidate bind-once caches if the WonderSwan orientation mode changed
      // (it remaps several button names). Cheap bool compare per call.
      if (inputCacheOrientation != (s32)orientationVertical) {
          std::lock_guard<std::mutex> lock(inputCacheMutex);
          inputButtonCache.clear();
          inputAxisCache.clear();
          inputCacheOrientation = (s32)orientationVertical;
      }

      if (auto button = input->cast<Node::Input::Button>()) {
          ButtonBinding binding;
          bool tapPending = false;
          {
              std::lock_guard<std::mutex> lock(inputCacheMutex);
              auto it = inputButtonCache.find(button.get());
              if (it == inputButtonCache.end()) {
                  // Bind once, then cache (mirrors ares InputMapping::bind()): names
                  // resolve on the first read; per-read is a map lookup + bit test.
                  // Only player 1 (controller port 0) maps to the single handheld
                  // gamepad; players 2+ have no gamepad source here, so leave them
                  // unmapped. Without this, resolveButtonBit maps purely by leaf
                  // name and P2's "A" resolves to the SAME bit as P1's "A", so one
                  // pad drives both players (observed on Neo Geo KOF2003).
                  string systemName = root->name();
                  ButtonBinding fresh;
                  fresh.playerOne = controllerPlayerIndex(button) == 0;
                  if (fresh.playerOne) {
                      fresh.bits = resolveButtonBit(button->name(), systemName, orientationVertical);
                  }
                  fresh.zxKeyboardKey = isZxKeyboardSystem(systemName) && isZxKeyboardKey(button->name());
                  fresh.seenPresses = pressGeneration(fresh.bits);
                  it = inputButtonCache.emplace(button.get(), fresh).first;
              }
              if (it->second.bits) {
                  u32 presses = pressGeneration(it->second.bits);
                  tapPending = presses != it->second.seenPresses &&
                               steadyMs() - latestPressMs(it->second.bits) < TAP_REPLAY_WINDOW_MS;
                  it->second.seenPresses = presses;
              }
              binding = it->second;
          }

          // Always set the value (resetting if not mapped) to ensure state consistency.
          // ZX Spectrum / 128 keyboard-matrix buttons are keyboard-backed: source the
          // value from the on-screen keyboard set instead of zeroing it (otherwise the
          // core's per-frame Keyboard::read() would immediately clear an on-screen key
          // press). Kempston joystick buttons (Up/Down/Left/Right/Fire) are NOT matrix
          // keys — they must read from the gamepad bitmask, or the ZX branch would zero
          // them every frame → Kempston joystick dead.
          if (binding.zxKeyboardKey) {
              std::lock_guard<std::mutex> klock(keyboardMutex);
              // Pressed if the on-screen keyboard OR the gamepad scheme holds it.
              button->setValue(keyboardKeysPressed.count(button->name()) > 0 || zxSchemeKeysPressed.count(button->name()) > 0);
          } else {
              u32 b = binding.bits;
              bool pressed = tapPending || (b != 0 && (buttons & b) != 0);
              // MSX keyboard keys and the ColecoVision keypad can also be held from
              // the on-screen controls; the set is empty unless such a key is held.
              // Player 1 only: both ColecoVision pads have keypad keys with the same names.
              if (!pressed && binding.playerOne && keyboardKeyCount.load(std::memory_order_relaxed) != 0) {
                  std::lock_guard<std::mutex> klock(keyboardMutex);
                  pressed = keyboardKeysPressed.count(button->name()) > 0;
              }
              button->setValue(pressed);
          }
      } else if (auto axis = input->cast<Node::Input::Axis>()) {
          s32 slot = -1;
          {
              std::lock_guard<std::mutex> lock(inputCacheMutex);
              auto it = inputAxisCache.find(axis.get());
              if (it == inputAxisCache.end()) {
                  string nodeName = axis->name();
                  string lowerName = nodeName.downcase();
                  slot = resolveAxisSlot(lowerName);
                  inputAxisCache[axis.get()] = (u32)(slot + 1);  // 0 = unmapped
              } else {
                  slot = (s32)it->second - 1;
              }
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

          if (slot >= 0) {
              axis->setValue(value);
          }
      } else if (auto rumble = input->cast<Node::Input::Rumble>()) {
          // N64 Rumble Pak + PS1 DualShock: binary motor state. Stored in an
          // atomic for the Kotlin side to poll — no JNI attach needed on the
          // emulation thread.
          //
          // Trust the game's bit exactly: the N64 controller is polled every
          // frame, so a 0-hold means rumbleState mirrors the game's write each
          // poll. This lets the Kotlin side see every hit's rising edge (the
          // game writes 1 on a racket hit, 0 between) so the decaying-hit
          // envelope re-triggers on EVERY hit, not just the first. If the game
          // writes 1 continuously with no 0 gaps (no edge), this can't help —
          // but MT's per-hit rumble implies it writes 0 between hits.
          rumbleState.store(rumble->enable());
      }
    }

    auto video(Node::Video::Screen screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
      if (width == 0 || height == 0 || isPausedAtomic) return;
      // NOTE: no currentEmuThread gate here — ares Video::Threaded=true means
      // this runs on the ares Screen thread, NOT the emulation thread. Gating
      // on currentEmuThread would reject every frame (black screen regression
      // 2026-08-14). The Screen thread is per-system and dies with it, so it
      // cannot become a zombie like the emu thread.

      lock_guard<std::mutex> lock(windowMutex);
      if (!phobos::host::surfaceReady()) return;

      if (!firstFrameRendered) firstFrameRendered = true;

      // WonderSwan vertical games are rotated here, in the frontend.
      bool rotate = root && root->name().beginsWith("WonderSwan") && orientationVertical;
      bool tvPicture = root && (root->name() == "Nintendo 64" || root->name() == "PlayStation"
          || isAleck64Session());
      recordVideoGeometry(screen, width, height, rotate, tvPicture);

      bool isN64Vulkan = false;
      #if defined(CORE_N64)
      isN64Vulkan = isN64VulkanSession();
      // Normal N64 Vulkan frames are presented straight from parallel-RDP's
      // scanout buffer (VI::refresh passes the Screen through). When the VI's
      // CPU fallback rendered instead, cpuScanoutActive is set and `data` holds
      // the picture.
      bool presentScanout = isN64Vulkan && !::ares::Nintendo64::vi.io.cpuScanoutActive;
      #else
      bool presentScanout = false;
      #endif

      // Nearest-neighbour 2x for small software frames keeps them sharp when the
      // compositor scales the window (N64 Vulkan output is already full size).
      bool scale2x = (width <= 320) && !rotate && !isN64Vulkan;
      u32 targetW = rotate ? height : (scale2x ? width * 2 : width);
      u32 targetH = rotate ? width : (scale2x ? height * 2 : height);

      // Ask for a display rate that fits the game (e.g. 60 Hz rather than 120 for a 60 Hz
      // game). On a 120 Hz panel a frame that finishes a little early or late in its period
      // stays up for one refresh or three; at 60 Hz each frame gets one whole refresh.
      static double hintedFrameRate = 0.0;
      double contentRate = refreshRateAtomic.load();
      if (windowChanged || std::abs(contentRate - hintedFrameRate) > 0.5) {
          phobos::host::hintFrameRate(contentRate);
          hintedFrameRate = contentRate;
      }

      if (windowChanged || targetW != bufferWidth || targetH != bufferHeight) {
          bufferWidth = targetW;
          bufferHeight = targetH;
          currentWidth = width;
          currentHeight = height;
          windowChanged = false;
      }

      phobos::host::Frame frame;
      auto locked = phobos::host::lockFrame(targetW, targetH, frame);
      if (locked == phobos::host::LockResult::Rejected) bufferWidth = 0;
      if (locked != phobos::host::LockResult::Locked) return;
      auto* dest = frame.pixels;
      u32 destStride = frame.stride;

      if (presentScanout) {
          presentN64Scanout(dest, destStride, width, height);
      } else if (data) {
          if (lastFrameBuffer.size() < (u64)width * height) lastFrameBuffer.resize((u64)width * height);
          u32 sourceStride = pitch / 4;
          u32 colors = screen->colors();
          if (rotate) {
              for (u32 y = 0; y < height; y++) {
                  u32* saveLine = lastFrameBuffer.data() + y * width;
                  convertToWindowPixels(data + y * sourceStride, saveLine, width, screen, colors);
                  // 90 degree clockwise rotation: (x, y) -> (h - 1 - y, x)
                  for (u32 x = 0; x < width; x++) dest[x * destStride + (height - 1 - y)] = saveLine[x];
              }
          } else {
              for (u32 y = 0; y < height; y++) {
                  u32* saveLine = lastFrameBuffer.data() + y * width;
                  convertToWindowPixels(data + y * sourceStride, saveLine, width, screen, colors);
                  if (scale2x) {
                      doubleLineWidth(saveLine, width, dest + (y * 2) * destStride, dest + (y * 2 + 1) * destStride);
                  } else {
                      memcpy(dest + y * destStride, saveLine, width * sizeof(u32));
                  }
              }
          }
      }
      phobos::host::unlockFrame();
    }

    // Copies the N64 Vulkan scanout (RGBA bytes) into the window, forcing alpha.
    // mapScanoutRead() takes vulkan.mutex even when the fence wait times out
    // (vData null), so unmapScanoutRead() must always follow — otherwise the
    // screen thread keeps the mutex and the emulation thread hangs in
    // scanoutAsync (the old N64 reset hang).
    static auto presentN64Scanout(u32* dest, u32 destStride, u32 width, u32 height) -> void {
      #if defined(CORE_N64)
      const u8* vData = nullptr;
      u32 vW = 0, vH = 0;
      u32 copyW = 0, copyH = 0;
      ::ares::Nintendo64::vulkan.mapScanoutRead(vData, vW, vH);
      if (vData) {
          copyW = std::min(width, vW);
          copyH = std::min(height, vH);
          if (::ares::n64DebugLoggingEnabled() && vW && vH) {
              const u32* dbg0 = (const u32*)vData;
              const u32* dbgMid = (const u32*)(vData + (vH / 2) * vW * 4);
              __android_log_print(ANDROID_LOG_INFO, "PhobosV",
                  "video: vW=%u vH=%u copyW=%u copyH=%u buf0=%08x bufMid=%08x",
                  vW, vH, copyW, copyH, dbg0[0], dbgMid[vW / 2]);
          }
          for (u32 y = 0; y < copyH; y++) {
              const u32* srcLine = (const u32*)(vData + y * vW * 4);
              u32* destLine = dest + y * destStride;
              u32 x = 0;
              #if defined(__aarch64__)
              uint32x4_t alpha = vdupq_n_u32(0xFF000000);
              for (; x + 4 <= copyW; x += 4) vst1q_u32(destLine + x, vorrq_u32(alpha, vld1q_u32(srcLine + x)));
              #endif
              for (; x < copyW; x++) destLine[x] = 0xFF000000 | srcLine[x];
          }
      }
      // Whatever the scanout did not cover (display blanked, fence timeout, smaller
      // scanout) is black; otherwise this window buffer would show an older frame.
      for (u32 y = 0; y < height; y++) {
          u32* destLine = dest + y * destStride;
          for (u32 x = y < copyH ? copyW : 0; x < width; x++) destLine[x] = 0xFF000000;
      }
      ::ares::Nintendo64::vulkan.unmapScanoutRead();
      #endif
    }

    // ares Screen output (0xAARRGGBB) -> RGBA_8888 window pixels (0xAABBGGRR),
    // alpha forced opaque. Values below `colors` are still palette indices (only
    // the LaserActive line-override path can emit those) and resolve through the
    // palette exactly as before; a vector group containing one finishes scalar.
    static auto convertToWindowPixels(const u32* source, u32* target, u32 count,
                                      const Node::Video::Screen& screen, u32 colors) -> void {
      u32 x = 0;
      #if defined(__aarch64__)
      static const uint8_t swapRB[16] = {2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15};
      const uint8x16_t shuffle = vld1q_u8(swapRB);
      const uint32x4_t opaque = vdupq_n_u32(0xFF000000);
      const uint32x4_t paletteLimit = vdupq_n_u32(colors);
      for (; x + 4 <= count; x += 4) {
          uint32x4_t p = vld1q_u32(source + x);
          if (vmaxvq_u32(vcltq_u32(p, paletteLimit)) != 0) break;
          uint8x16_t swapped = vqtbl1q_u8(vreinterpretq_u8_u32(p), shuffle);
          vst1q_u32(target + x, vorrq_u32(vreinterpretq_u32_u8(swapped), opaque));
      }
      #endif
      for (; x < count; x++) {
          u32 p = source[x];
          if (p < colors) p = screen->lookupPalette(p);
          target[x] = 0xFF000000 | ((p << 16) & 0x00FF0000) | (p & 0x0000FF00) | ((p >> 16) & 0x000000FF);
      }
    }

    // Writes each pixel twice across two window lines (nearest-neighbour 2x).
    // Never reads the window buffer, which may be write-combined memory.
    static auto doubleLineWidth(const u32* source, u32 width, u32* line1, u32* line2) -> void {
      u32 x = 0;
      #if defined(__aarch64__)
      for (; x + 4 <= width; x += 4) {
          uint32x4_t p = vld1q_u32(source + x);
          uint32x4x2_t doubled = vzipq_u32(p, p);
          vst1q_u32(line1 + x * 2, doubled.val[0]);
          vst1q_u32(line1 + x * 2 + 4, doubled.val[1]);
          vst1q_u32(line2 + x * 2, doubled.val[0]);
          vst1q_u32(line2 + x * 2 + 4, doubled.val[1]);
      }
      #endif
      for (; x < width; x++) {
          line1[x * 2] = line1[x * 2 + 1] = source[x];
          line2[x * 2] = line2[x * 2 + 1] = source[x];
      }
    }

    auto audio(Node::Audio::Stream stream) -> void override {
      if (isPausedAtomic) return;
      // Reject audio from an abandoned zombie emulation thread (see
      // currentEmuThread): after the abandon path leaks a stuck thread, it may
      // later resume and keep emulating the OLD system. It must not register
      // streams, mix, or push samples into the NEW system's pipeline.
      if (pthread_self() != currentEmuThread.load()) return;

      if (!audioStreamOpen.load(std::memory_order_acquire)) {
        // Without an audio device (a desktop can have none) this would retry on every core
        // write; try again every few seconds and discard the samples until then.
        static s64 nextOpenAttemptMs = 0;
        if (steadyMs() >= nextOpenAttemptMs) {
          std::unique_lock<std::mutex> lock(audioMutex);
          bool open = phobos::host::openAudio();
          audioStreamOpen.store(open, std::memory_order_release);
          if (!open) nextOpenAttemptMs = steadyMs() + 5000;
        }
        if (!audioStreamOpen.load(std::memory_order_acquire)) {
          f64 discarded[2];
          while (stream->pending()) stream->read(discarded);
          return;
        }
      }
      // Restart the audio thread if it was stopped (unloadSystem stops it).
      // This MUST be outside the open block: the host's audio device stays
      // open across loads, so the open block (which used to spawn the
      // thread) is skipped — without this, the audio thread
      // never restarts → NO SOUND in any core after the first load (regression
      // 2026-08-14).
      if (!audioThreadRunning.load()) {
        audioThreadStop.store(false);
        audioThread = std::thread(audioThreadMain);
        audioThreadRunning.store(true);
      }

      // Push MIXED samples into the audio ring buffer (O(1), fixed ~125ms
      // cap). Never blocks; oldest samples are overwritten when the
      // emulator out-produces the DAC so a fast-forward burst can't leave
      // a backlog. Cores expose one stream per sound source; the emulation
      // thread calls this for whichever stream just produced output, so we
      // must MIX all streams sample-aligned — draining a single stream
      // unmixed is what garbled Mega Drive/CD audio (each stream's pending
      // block was appended sequentially instead of summed).
      if (audioStreamOpen.load(std::memory_order_relaxed)) {
        // Thread-local snapshot cache keyed on audioStreamsVersion. The
        // emulation thread is the only audio() caller and is recreated per
        // load, so the cache is naturally scoped to one system's stream set.
        // This removes the per-call mutex lock + linear scan + heap copy that
        // the ZX ULA (firing audio() millions of times/sec) was paying — the
        // source of the remaining ~1 FPS of fat.
        auto& audioState = audioThreadState();
        auto& cachedAudioVersion = audioState.streamsVersion;
        auto& cachedStreams = audioState.streams;
        u64 ver = audioStreamsVersion.load(std::memory_order_acquire);
        if (ver != cachedAudioVersion) {
          std::lock_guard<std::mutex> lock(audioStreamsMutex);
          cachedStreams = audioStreams;
          cachedAudioVersion = ver;
        }

        // Register this stream on first sight (rare — once per stream per
        // load). Only touches the registry when the cached snapshot doesn't
        // contain it, so the hot path stays lock-free.
        bool known = false;
        for (auto& s : cachedStreams) { if (s == stream) { known = true; break; } }
        if (!known) {
          std::lock_guard<std::mutex> lock(audioStreamsMutex);
          bool stillUnknown = true;
          for (auto& s : audioStreams) { if (s == stream) { stillUnknown = false; break; } }
          if (stillUnknown) {
            audioStreams.push_back(stream);
            LOGI("Audio: registered stream '%s' (ch=%u, %.0fHz) — %zu total",
                 (const char*)stream->name(), stream->channels(),
                 stream->frequency(), audioStreams.size());
            audioStreamsVersion.fetch_add(1, std::memory_order_release);
          }
          // Refresh the local snapshot so subsequent calls use it directly.
          cachedStreams = audioStreams;
          cachedAudioVersion = audioStreamsVersion.load(std::memory_order_acquire);
        }

        // Fast path: single stream — drain it directly (no lockstep, no
        // clamp). This is the common case for single-stream cores (N64, GBA,
        // PS1, GB/GBC...) and avoids all mixing overhead.
        auto& localBuffer = audioState.pending;
        if (cachedStreams.size() == 1) {
          f64 samples[2];
          while (stream->pending()) {
            u32 channels = stream->read(samples);
            if (channels == 1) {
              localBuffer.push_back((f32)samples[0]);
              localBuffer.push_back((f32)samples[0]);
            } else {
              localBuffer.push_back((f32)samples[0]);
              localBuffer.push_back((f32)samples[1]);
            }
          }
          if (localBuffer.size() >= audioPushSamples) pushAudio(localBuffer);
          return;
        }

        // Multi-stream: lockstep mix using the cached snapshot (no copy).

        // Lockstep mixing (mirrors upstream desktop ares Program::audio):
        // emit one output frame only when EVERY stream has a pending frame;
        // read one frame from each and sum (mono is duplicated to both
        // channels). All streams resample to 48kHz, so they stay aligned.
        // Bounded at 8192 frames/call so a pathological backlog can never
        // stall the emulation thread inside audio() for an unbounded time.
        u32 drained = 0;
        while (drained < 8192) {
          bool allPending = true;
          for (auto& s : cachedStreams) {
            if (!s->pending()) { allPending = false; break; }
          }
          if (!allPending) break;
          drained++;

          f64 sample[2] = {0.0, 0.0};
          f64 buffer[2];
          for (auto& s : cachedStreams) {
            u32 channels = s->read(buffer);
            if (channels == 1) {
              sample[0] += buffer[0];
              sample[1] += buffer[0];
            } else {
              sample[0] += buffer[0];
              sample[1] += buffer[1];
            }
          }
          localBuffer.push_back((f32)std::clamp(sample[0], -1.0, 1.0));
          localBuffer.push_back((f32)std::clamp(sample[1], -1.0, 1.0));
          drained++;
        }

        if (localBuffer.size() >= audioPushSamples) pushAudio(localBuffer);
      }
    }

    auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
      if (!node) return std::make_shared<vfs::directory>();
      string nodeName = node->name();
      LOGI("VFS: pak() requested for node: %s", (const char*)nodeName);

      if (nodeName == "Memory Card" && root && root->name() == "PlayStation") {
        auto port = node->parent().lock();
        auto& card = ps1MemoryCards[port && port->name().endsWith("2") ? 1 : 0];
        if (card.pak) return card.pak;
      }

      if (nodeName == "Game Boy Cartridge") {
        // Super Game Boy only: the GB game is secondaryMedium. Do not use
        // secondaryMedium for other systems (64DD disk, disc swap, etc.).
        if (root && root->name() == "Super Famicom" && secondaryMedium) {
            if (secondaryMedium->pak) {
                LOGI("VFS: Returning secondaryMedium pak for %s", (const char*)nodeName);
                return secondaryMedium->pak;
            }
            LOGW("VFS: No secondaryMedium pak for %s (SGB)", (const char*)nodeName);
            return {};
        }
        if (currentMedium && currentMedium->pak) {
            LOGI("VFS: Returning currentMedium pak for %s", (const char*)nodeName);
            return currentMedium->pak;
        }
        LOGW("VFS: No medium pak available for %s", (const char*)nodeName);
      }

      if (nodeName.endsWith("Cartridge") || nodeName.endsWith("Disc") || nodeName == "Laserdisc"
          || nodeName.endsWith("Card")) {
        if (currentMedium && currentMedium->pak) {
            LOGI("VFS: Returning currentMedium pak for %s", (const char*)nodeName);
            return currentMedium->pak;
        }
        LOGW("VFS: No currentMedium pak available for %s", (const char*)nodeName);
      }

      // ZX Spectrum and MSX tapes: Tape::load() reads "program.tape" (decoded audio)
      // from this pak — the MIA medium pak IS the tape.
      if (nodeName.endsWith("Tape") && root && (root->name().beginsWith("ZX Spectrum") || root->name().beginsWith("MSX"))) {
        if (root->name().beginsWith("MSX") && msxDataTapeIn && msxDataTape && msxDataTape->pak) {
            LOGI("VFS: Returning the data tape pak for %s", (const char*)nodeName);
            return msxDataTape->pak;
        }
        if (currentMedium && currentMedium->pak) {
            LOGI("VFS: Returning currentMedium pak for %s (tape)", (const char*)nodeName);
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

      // N64 Controller Pak: platform->pak() is only invoked for a Gamepad node
      // when a Controller Pak is attached (a Rumble Pak needs no storage). Seed
      // the directory with save.pak from the persistent saves dir, marking it
      // "loaded" so Gamepad::connect() imports the bank count + data. Cache the
      // dir so unloadSystem()/setN64Pak() can export the RAM back to disk.
      if (nodeName == "Gamepad" && root && root->name() == "Nintendo 64") {
        player1PakDir = std::make_shared<vfs::directory>();
        player1PakDir->setAttribute("name", nodeName);
        if (savesPath) {
          // Per-ROM Controller Pak (same key as cartridge saves): each game
          // gets its own save.pak so games don't clobber each other's
          // controller-pak data (ares models the pak as a single 32KB bank,
          // not multi-page like real hardware).
          string romKey = currentRomBase;
          romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
          if (romKey.size() == 0) romKey = "rom";
          string savePath = string{savesPath, "/Nintendo 64/", romKey, "/save.pak"};
          auto data = nall::file::read(savePath);
          if (data.size()) {
            if (auto fp = vfs::memory::open(data)) {
              fp->setName("save.pak");
              fp->setAttribute("loaded", true);
              player1PakDir->append("save.pak", fp);
              LOGI("VFS: Attached save.pak (%zu bytes) to Gamepad pak [%s]", data.size(), (const char*)romKey);
              return player1PakDir;
            }
          }
        }
        // No persisted save yet — pre-create a blank bank so
        // Gamepad::connect()'s create path can reallocate + format it
        // (that path only runs when save.pak already exists in the dir).
        player1PakDir->append("save.pak", 32_KiB);
        return player1PakDir;
      }

      auto dir = std::make_shared<vfs::directory>();
      dir->setAttribute("name", nodeName);
      dir->setAttribute("title", "Phobos Game");
      dir->setAttribute("region", "NTSC-U");

      string systemPath = string{homePath, "/System/", nodeName, "/"};

      // stripCopierHeader: a PC Engine card dump 512 bytes past a multiple of 8 KiB carries a copier's
      // header, which the core would read as the start of the BIOS.
      auto attachFile = [&](string fileName, string vfsName = "", bool stripCopierHeader = false) {
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
          if (stripCopierHeader && data.size() % 8_KiB == 512) {
              data.erase(data.begin(), data.begin() + 512);
              LOGI("VFS: Dropped the 512-byte copier header of %s", (const char*)fileName);
          }
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
      } else if (nodeName == "Mega Drive" && node->attribute("configuration").find("LaserActive")) {
          // LaserActive (SEGA PAC): MCD::load() reads the PAC BIOS of the configuration's region (loadRom picks
          // the region whose BIOS is set) and MCD::connect() the backup RAM, which mia's Mega LD system pak
          // carries formatted, as here.
          bool japan = (bool)node->attribute("configuration").find("NTSC-J");
          auto it = firmwareMap.find(japan ? "fw_laseractive_sega_jp" : "fw_laseractive_sega_us");
          if (it != firmwareMap.end()) attachFile((const char*)it->second, "bios.rom");
          dir->append("tmss.rom", mia::Resource::MegaDrive::TMSS);
          dir->append("backup.ram", megaCDBackupRam());
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
          // Mega 32X and Mega CD 32X: M32X::load() reads the boot ROMs from this pak.
          if (node->attribute("configuration").find("32X")) {
              for (auto& file : mega32XBootFiles) dir->append(file.name, readMega32XBootFile(file));
          }
          // Mega CD and Mega CD 32X: MCD::connect() reads the backup RAM, which importIntoPak replaces with the
          // game's saved copy before the disc tray connects; MCD::save() writes it back for flushSavesToDisk().
          if (node->attribute("configuration").find("Mega CD")) dir->append("backup.ram", megaCDBackupRam());
      } else if (nodeName == "Neo Geo CD") {
          // Neo Geo CD needs the CD BIOS (neocd.zip via fw_ng_cd) in the system
          // pak, plus the shared LSPC zoom table (000-lo.lo) from neogeo.zip.
          bool attached = false;
          auto it_ngcd = firmwareMap.find("fw_ng_cd");
          if (it_ngcd != firmwareMap.end()) attached = attachFile((const char*)it_ngcd->second, "bios.rom");
          if (!attached) attached = attachFile("bios.rom");
          string zipPath = string{tempFilePath, "/neogeo.zip"};
          bool haveZoomy = false;
          if (file::exists(zipPath)) {
            Decode::ZIP zip;
            if (zip.open(zipPath)) {
              for (auto& zf : zip.file) {
                string n = zf.name.downcase();
                if (!haveZoomy && n.equals("000-lo.lo")) {
                  auto data = zip.extract(zf);
                  if (data.size() == 0x20000) {
                    if (auto fp = vfs::memory::open(data)) {
                      dir->append("zoomy.rom", fp); haveZoomy = true;
                      LOGI("VFS: Neo Geo CD LSPC zoom table (000-lo.lo) attached");
                    }
                  }
                }
              }
            }
          }
          if (!haveZoomy) attachFile("zoomy.rom");
      } else if (nodeName == "Neo Geo" || nodeName == "Neo Geo AES" || nodeName == "Neo Geo MVS") {
          // neogeo.zip is copied to mia_temp. Extract BIOS + fix-layer ROM.
          string zipPath = string{tempFilePath, "/neogeo.zip"};
          bool haveBios = false, haveStatic = false, haveZoomy = false;
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
                // 000-lo.lo = the LSPC vertical zoom table (MAME "spritegen:zoomy")
                if (!haveZoomy && n.equals("000-lo.lo")) {
                  auto data = zip.extract(zf);
                  if (data.size() == 0x20000) {
                    if (auto fp = vfs::memory::open(data)) {
                      dir->append("zoomy.rom", fp); haveZoomy = true;
                      LOGI("VFS: Neo Geo LSPC zoom table (000-lo.lo) attached");
                    }
                  }
                }
              }
            }
          }
          if (!haveBios) attachFile("bios.rom");
          if (!haveStatic) attachFile("static.rom");
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
          #if defined(CORE_N64)
          // The 64DD system node keeps the name "Nintendo 64" (information.dd
          // is the flag that distinguishes it) — so this same branch serves
          // both. When loaded as a 64DD variant, the drive needs its IPL ROM
          // or it can't initialize (no CIC, no boot) → disk games black-screen.
          // Try US, then JP, then DEV — the 64DD firmware scanner may map any
          // of the three keys depending on which IPL the user supplied.
          if (::ares::Nintendo64::_DD()) {
              bool ddAttached = false;
              auto it_us = firmwareMap.find("fw_n64dd_us");
              if (it_us != firmwareMap.end()) ddAttached = attachFile((const char*)it_us->second, "64dd.ipl.rom");
              if (!ddAttached) {
                  auto it_jp = firmwareMap.find("fw_n64dd_jp");
                  if (it_jp != firmwareMap.end()) ddAttached = attachFile((const char*)it_jp->second, "64dd.ipl.rom");
              }
              if (!ddAttached) {
                  auto it_dev = firmwareMap.find("fw_n64dd_dev");
                  if (it_dev != firmwareMap.end()) ddAttached = attachFile((const char*)it_dev->second, "64dd.ipl.rom");
              }
              if (!ddAttached) attachFile("64dd.ipl.rom");
          }
          // [Phobos] The 64DD RTC (time.rtc) must EXIST in the system pak so
          // DD::RTC::save() can write to it. MIA's Nintendo64DD system creates
          // it (0x10 bytes); the ares core writes the live RTC into it via
          // root->save(). Without a file node here, the write silently no-ops
          // and the RTC never persists ("Error 48 — Date/Time not set" on
          // every boot after first save).
          if (::ares::Nintendo64::_DD()) {
            // The 64DD RTC (time.rtc) must EXIST in the system pak so
            // DD::RTC::save() can write to it (pak is rebuilt per call).
            dir->append("time.rtc", 0x10);
          }
          #endif
      } else if (nodeName == "Nintendo 64DD") {
          // Defensive: in case a future ares names the DD root node distinctly.
          bool attached = false;
          auto it_ntsc = firmwareMap.find("fw_n64_pif_ntsc");
          if (it_ntsc != firmwareMap.end()) attached = attachFile((const char*)it_ntsc->second, "pif.ntsc.rom");
          if (!attached) attachFile("pif.ntsc.rom");
          auto it_dd = firmwareMap.find("fw_n64dd_jp");
          if (it_dd != firmwareMap.end()) attachFile((const char*)it_dd->second, "64dd.ipl.rom");
          else attachFile("64dd.ipl.rom");
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
      } else if (nodeName == "WonderSwan" || nodeName == "WonderSwan Color" || nodeName == "Pocket Challenge V2") {
          if (!skipBootRom) attachFile("boot.rom");
      } else if (nodeName == "MSX" || nodeName == "MSX2") {
          // A BIOS set on the Firmware screen comes first: a real MSX's has BASIC, which loading from tape
          // needs, and the bundled C-BIOS doesn't. An MSX2's main and sub ROMs go together, so both must be set.
          bool msx2 = nodeName == "MSX2";
          const char* mainKey = msx2 ? "fw_msx2_main" : "fw_msx";
          if (firmwareSet(mainKey) && (!msx2 || firmwareSet("fw_msx2_sub"))) {
              attachFile((const char*)firmwareMap[mainKey], "bios.rom");
              if (msx2) attachFile((const char*)firmwareMap["fw_msx2_sub"], "sub.rom");
          } else {
              attachFile("bios.rom");
              if (msx2) attachFile("sub.rom");
          }
      } else if (nodeName == "PC Engine" && node->attribute("configuration").find("LaserActive")) {
          // LaserActive (NEC PAC): the first PAC BIOS set, in upstream ares's order (PAC-N10, PAC-N1, PCE-LP1).
          for (auto key : laserActiveNecBiosKeys) {
              auto it = firmwareMap.find(key);
              if (it != firmwareMap.end() && attachFile((const char*)it->second, "bios.rom")) break;
          }
          dir->append("backup.ram", pcEngineCDBackupRam(nodeName));
      } else if (nodeName == "PC Engine" || nodeName == "SuperGrafx" || nodeName == "PC Engine Duo" || nodeName == "PC Engine CD") {
          bool attached = false;
          auto it_pce = firmwareMap.find("fw_pce_cd_3_jp");
          if (it_pce != firmwareMap.end()) attached = attachFile((const char*)it_pce->second, "bios.rom", true);
          if (!attached) {
              auto it_ge = firmwareMap.find("fw_pce_cd_ge_jp");
              if (it_ge != firmwareMap.end()) attached = attachFile((const char*)it_ge->second, "bios.rom", true);
          }
          if (!attached) attached = attachFile("bios.rom");
          // The Duo is the PC Engine model with the CD unit (PC Engine CD games).
          if (node->attribute("configuration").find("Duo")) dir->append("backup.ram", pcEngineCDBackupRam(nodeName));
      } else if (nodeName == "ZX Spectrum" || nodeName == "ZX Spectrum 128") {
          // The ZX Spectrum REQUIRES its system ROM to boot and run the tape
          // loader. Without it the core allocates the ROM filled with 0xFF —
          // the CPU executes RST-38h garbage → colored stripe screen, tape
          // games never load.
          // 48K: 16K bios.rom. 128K: 16K bios.rom + 16K sub.rom. A ROM set on
          // the Firmware screen comes first, else the copies ares bundles.
          bool is128 = nodeName == "ZX Spectrum 128";
          auto it_zx = firmwareMap.find(is128 ? "fw_zx128" : "fw_zx48");
          bool attached = it_zx != firmwareMap.end() && attachFile((const char*)it_zx->second, "bios.rom");
          if (!attached) attached = attachFile("bios.rom");
          if (!attached) dir->append("bios.rom", is128 ? mia::Resource::ZXSpectrum128::BIOS : mia::Resource::ZXSpectrum::BIOS);
          if (is128) {
              // sub.rom = 128K second half (e.g. Fuse 128-1.rom)
              auto it_sub = firmwareMap.find("fw_zx128_sub");
              bool sub = it_sub != firmwareMap.end() && attachFile((const char*)it_sub->second, "sub.rom");
              if (!sub) sub = attachFile("sub.rom");
              if (!sub) dir->append("sub.rom", mia::Resource::ZXSpectrum128::Sub);
          }
      }

      return dir;
    }
  };

  static AndroidPlatform androidPlatform;
  Platform* platform = &androidPlatform;

  auto unloadSystem() -> void {
  // Block setEmulationRunning(true) until the teardown below completes: a
  // fresh emu thread spawned mid-teardown would grab the OLD root (localRoot
  // shared_ptr copy) and run CPU::LW against the freed singleton hardware
  // (rdram/cartridge.rom/dd.disk) -> SIGSEGV (the 64DD quit->reload crash).
  systemUnloading.store(true);
    isPausedAtomic = true;
    fastForwardAtomic = false;
    // Keys held on the on-screen keyboard must not carry into the next game.
    clearOnScreenKeys();
    // The next game reports its own geometry with its first frame.
    videoDisplayWidth.store(0.0f);
    videoDisplayHeight.store(0.0f);
    // Don't leave the previous core's rate on the next load (PAL → NTSC, etc.).
    refreshRateAtomic.store(60.0);
    // A reset requested during a hung session must NOT carry into the next
    // system: the abandoned thread never consumed it, and a fresh load would
    // consume it as a soft-reset right after boot → CPU stuck at the boot ROM
    // (0xffffffffbfc00000, 60fps, 0.00ms frame time, never enters the game).
    resetRequestedAtomic.store(false);

    // Stop the audio thread FIRST (it may be mid-write to the audio device),
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
    // Keep the AAudio stream ALIVE across unload/load (do NOT close it here).
    // Closing + immediately reopening (e.g. quit a hung ZX 128K → reload)
    // corrupts AAudio's internal DefaultDispatch thread → SIGSEGV 0x80. The
    // stream is reused on the next load; audio() sees it non-null and skips
    // the open path. Only the audio THREAD is stopped (so no drain while the
    // menu shows). The stream is closed in the abandon path / process teardown.
    {
      std::lock_guard<std::mutex> lock(audioMutex);
      // Clear the ring so stale samples don't pop on the next load.
      audioRingHead = 0;
      audioRingSize = 0;
      audioRingFill.store(0, std::memory_order_relaxed);
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
      invalidateInputCaches();
      currentMedium.reset();
      secondaryMedium.reset();
      msxDataTape.reset();
      msxDataTapeIn = false;
      msxGameTapePosition = 0;
      ::ares::MSX::tapeDeck.recordArmed = false;
      player1PakDir.reset();
      rumbleState.store(false);
      lastRumbleOnTime = {};
      {
        std::lock_guard<std::mutex> lock(audioStreamsMutex);
        audioStreams.clear();
        audioStreamsVersion.fetch_add(1, std::memory_order_release);
      }
      // Abandon path: the zombie may still hold the AAudio stream. Close it so
      // the next load starts fresh — the zombie's audio() calls are gated by
      // currentEmuThread (now 0) and the stream pointer is nulled. After
      // close(), AAudio's internal DefaultDispatch thread is still winding
      // down asynchronously; wait for its state to reach a terminal state so
      // the next load's open() can't race a live dispatch thread (the SIGSEGV
      // 0x80 on a fresh load after a quit).
      {
        std::lock_guard<std::mutex> lock(audioMutex);
        if (phobos::host::audioOpen()) {
          phobos::host::closeAudio();
          audioStreamOpen.store(false, std::memory_order_release);
        }
      }
      // Park the stuck thread as a zombie so the next N64 load joins it
      // BEFORE re-initializing the singleton hardware (see
      // joinAbandonedThreads). Without this, the next load's System::unload()
      // frees rdram/cartridge.rom/dd.disk under the running zombie → SIGSEGV
      // in CPU::LW (the "crash while unloading" bug).
      parkZombieThread();
      systemUnloading.store(false);
      LOGI("System abandoned (thread stuck)");
      return;
    }
    // Thread exited cleanly — we hold runMutex. Release it and unload normally.
    runMutex->unlock();

    std::lock_guard<std::recursive_mutex> lock(systemMutex);
    if (root) {
        // Flush cartridge/battery saves to the persistent saves directory
        // (same code path as the pause flush). Must run BEFORE root->unload()
        // so the medium pak still holds the live save state.
        flushSavesToDisk();
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

        // No more audio() callbacks can arrive once the core is unloaded
        // (unloadSystem() set isPausedAtomic=true first). Drop the stream
        // registry so the next system starts with a clean mix.
        {
          std::lock_guard<std::mutex> lock(audioStreamsMutex);
          audioStreams.clear();
          audioStreamsVersion.fetch_add(1, std::memory_order_release);
        }

        // Export the N64 Controller Pak: System::unload() → save() flushed
        // Gamepad::save() (Controller Pak RAM) into player1PakDir.
        exportControllerPak();
        player1PakDir.reset();
        rumbleState.store(false);
        lastRumbleOnTime = {};
    }
    cachedPlayer1 = {};
    invalidateInputCaches();
    currentMedium.reset();
    secondaryMedium.reset();
    msxDataTape.reset();
    msxDataTapeIn = false;
    msxGameTapePosition = 0;
    ::ares::MSX::tapeDeck.recordArmed = false;
    removeTempCopies();
    // [Phobos] The emulation thread has exited cleanly (we acquired runMutex and
    // joined/witnessed its exit). Drop the stale pthread handle so the next
    // setEmulationRunning(true) -> ensureThread() doesn't log the misleading
    // "replacing abandoned emulation thread" warning and realloc runMutex (a
    // tiny per-load leak). The abandon path leaves this set on purpose (zombie).
    emuThread = 0;
    emuThreadRunning = false;
    currentEmuThread.store(0);
    currentEmuThreadCookie.reset();
    systemUnloading.store(false);
    frameIntervalCursor.store(0, std::memory_order_release);
    emuThreadCore.store(-1, std::memory_order_relaxed);
    LOGI("System unloaded");
  }

  static auto connectDevices(Node::Object node) -> void {
    if (!node) return;
    LOGI("VFS: connectDevices for system '%s'", (const char*)node->name());
    // The node tree is about to be (re)built: port disconnects/allocates free
    // and can recycle Node::Input object addresses. The input caches are keyed
    // by raw pointer, so stale entries would mis-bind controls after a PS1
    // analog toggle (DualShock <-> Digital Gamepad), N64DD disk mount, or
    // reload. Clear them up front (thread-safe vs the emulation thread).
    invalidateInputCaches();
    auto ports = node->find<Node::Port>();
    s32 portIndex = 1;

    for (auto& port : ports) {
      LOGI("VFS: connectDevices - name='%s', type='%s', family='%s'", (const char*)port->name(), (const char*)port->type(), (const char*)port->family());
      // ZX Spectrum: the tray MUST be connected — Tape::allocate() creates
      // the node/stream/data that Tape::serialize() derefs during power();
      // skipping it crashes with a null-pointer SIGSEGV on load.
      // MSX: a game on tape goes in the deck (the motor relay plays it); a
      // cartridge game leaves the deck empty.
      if (port->type() == "Tape" || port->type() == "Tray" || port->type() == "Tape Deck") {
          string fam = port->family();
          bool msxTape = fam == "MSX" && currentMedium && currentMedium->pak && currentMedium->pak->attribute("tape").boolean();
          if (fam != "ZX Spectrum" && !msxTape) continue;
          // The game's own MSX tape only plays: recording goes to the data tape, and a state that was
          // recording can't start recording over the game when it loads with this tape in.
          if (msxTape) currentMedium->pak->setAttribute("writable", false);
          if (port->allocate()) {
              LOGI("VFS: Connecting %s tape tray", (const char*)fam);
              port->connect();
              // A real MSX doesn't sound its cassette: the signal only reaches the PSG's port.
              if (msxTape) {
                  if (auto& stream = ::ares::MSX::tapeDeck.tray.tape.stream) stream->setMuted(true);
              }
          } else {
              LOGE("VFS: FAILED to allocate %s tape", (const char*)fam);
          }
          portIndex++;
          continue;
      }
      // Disc Tray: connect whenever the core has one. A PC Engine HuCard or
      // SuperGrafx game has no CD unit (PCD::Present()), so no tray either;
      // the Duo's root is also named "PC Engine", so the name can't tell them apart.
      if (port->name() == "Disc Tray") {
          if (port->allocate()) {
              LOGI("VFS: Connecting Disc Tray for '%s'", (const char*)(node ? node->name() : ""));
              port->connect();
          }
          portIndex++;
          continue;
      }
      if (port->type() == "Cartridge" || port->type() == "Compact Disc" || port->type() == "Disk Drive" || port->type() == "Floppy Disk"
          || port->type() == "Universal Media Disc") {
        // Mega CD 32X: the 32X fills the cartridge slot with no cartridge in it
        // (the game is in the Mega CD's tray). Cartridge::power() builds that
        // board only when the slot is left empty; a connected slot would get
        // the disc's pak on a plain board, cutting the 68000 off from the 32X.
        if (port->type() == "Cartridge" && node->attribute("configuration").find("Mega CD 32X")) continue;
        // An MSX game on tape leaves the cartridge slots empty: the tape's pak has no ROM, and a real BIOS
        // scanning the slots for one would read a board built around nothing.
        if (port->type() == "Cartridge" && port->family().beginsWith("MSX") && currentMedium && currentMedium->pak
            && currentMedium->pak->attribute("tape").boolean()) continue;
        // The MSX's second slot stays empty, as ares leaves it: connected there too, the game would be a second
        // cartridge for the BIOS to find. (MSX states are v135 since: a state holds each connected slot's board.)
        if (port->type() == "Cartridge" && port->family().beginsWith("MSX") && port->name() == "Expansion Slot") continue;
        // The N64DD "Disk Drive" port has type "Floppy Disk"; connecting it
        // mounts the .ndd disk medium (returned by pak() for the
        // "Nintendo 64DD Disk" node).
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
      } else if (port->type() == "Controller" || port->type() == "Control Pad" || port->name().find("Controller") || port->name().find("Port")) {
        string defaultDevice = "Gamepad";
        string family = port->family();
        string sysName = node ? node->name() : "";

        if (family.find("Nintendo 64") || sysName.find("Nintendo 64")) defaultDevice = (sysName == "Arcade") ? "Aleck64" : "Gamepad";
        else if (family.find("Super Famicom") || sysName.find("Super Famicom") || sysName.find("SNES")) {
            if (port->name().find("Expansion")) defaultDevice = ""; // Expansion port doesn't take gamepad
            else defaultDevice = "Gamepad";
        }
        else if (family.find("Famicom") || sysName.find("Famicom")) {
            if (port->name().find("Expansion")) defaultDevice = ""; // Expansion port doesn't take gamepad
            else defaultDevice = "Gamepad";
        }
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
        else if (family.find("ZX Spectrum") || sysName.find("ZX Spectrum")) {
            // ZX Spectrum: the Expansion port takes a Kempston joystick
            // (enables gamepad play in joystick games like Manic Miner).
            // No standard gamepad ports exist; only the Expansion port is used.
            if (port->name().find("Expansion")) defaultDevice = "Kempston";
            else defaultDevice = "";
        }

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

                // Attach the configured controller pak (Rumble / Controller Pak)
                // to Player 1's Gamepad. The Pak sub-port is hot-swappable, so a
                // later setN64Pak() call can swap it without a reload.
                if (root && root->name() == "Nintendo 64" && n64Pak != "None") {
                    for (auto& pakPort : pNode->find<Node::Port>()) {
                        if (pakPort->type() != "Pak") continue;
                        if (auto slot = pakPort->allocate(n64Pak)) {
                            pakPort->connect();
                            LOGI("VFS: Attached %s to Player 1 (N64)", (const char*)n64Pak);
                        }
                        break;
                    }
                }
            }
        } else {
            LOGE("VFS: FAILED to allocate %s on %s (Family: '%s', Sys: '%s')", (const char*)defaultDevice, (const char*)port->name(), (const char*)family, (const char*)sysName);
        }
        portIndex++;
      }
else if (port->type() == "Keyboard") {
        // ZX Spectrum keyboard only supports the "Original" matrix layout;
        // MSX uses "Japanese". Pick by family so the ZX matrix buttons are
        // created. MUST call connect() after allocate() — connect() is what
        // builds the 8x5 button matrix; without it the on-screen keys find
        // no buttons and do nothing.
        string defaultLayout = "Japanese";
        string fam = port->family();
        if (fam.find("ZX Spectrum")) defaultLayout = "Original";
        if (port->allocate(defaultLayout)) {
            LOGI("VFS: Allocated %s keyboard on %s", (const char*)defaultLayout, (const char*)port->name());
            port->connect();
            LOGI("VFS: Connected %s keyboard on %s", (const char*)defaultLayout, (const char*)port->name());
        }
      }
    }
  }

  auto initialize(const char* systemNamePtr, const char* uriPtr, const char* romNamePtr) -> bool {
    string systemName = systemNamePtr;
    string uri = uriPtr;
    string romName = romNamePtr ? romNamePtr : "";
    // Key the per-game save directory by the ROM base name (no extension).
    currentRomBase = romName;
    if (auto dot = currentRomBase.findPrevious(currentRomBase.size(), ".")) currentRomBase = currentRomBase.slice(0, *dot);
    if (currentRomBase.size() == 0) currentRomBase = "rom";
    unloadSystem();

    std::unique_lock<std::recursive_mutex> lock(systemMutex);

    isPausedAtomic = false;
    // Belt-and-suspenders: a stale reset must never fire on the fresh system.
    resetRequestedAtomic.store(false);
    firstFrameRendered = false;

    scheduler.reset();

    LOGI("Initializing system: %s, uri: %s", (const char*)systemName, (const char*)uri);

    if (customDriverPath) {
        LOGI("adrenotools: Attempting load. NativeLibDir: %s, DriverPath: %s, RedirectDir: %s", (const char*)nativeLibraryDir, (const char*)customDriverPath, (const char*)tempFilePath);
        if (!phobos::host::loadVulkan(customDriverPath, nativeLibraryDir, tempFilePath)) {
            LOGE("adrenotools: Failed to load custom driver; using the system Vulkan driver");
        }
    } else {
        LOGI("Environment: Using system default Vulkan driver");
        phobos::host::loadVulkan(nullptr, nativeLibraryDir, tempFilePath);
    }

    string directPath = romPath;
    romPath = "";
    if (!directPath) {
      if (romFd == -1) return false;
      struct stat st;
      if(fstat(romFd, &st) != 0) return false;
    }

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
    if (systemName.contains("Neo Geo") || systemName == "Arcade") {
      if (romName.size() > 0) {
        tempFname = romName;
        if (auto dot = tempFname.find(".")) tempFname = tempFname.slice(0, *dot);
      }
    }
    // A PSP program's folder stands for its disc (disc0:), so its copies get a folder of their own rather than the
    // cache other games' copies and the firmware share.
    string copyFolder = tempFilePath;
    if (systemName == "PlayStation Portable") {
      copyFolder = {tempFilePath, "/psp"};
      directory::create(copyFolder);
    }
    string loadPath = directPath;
    if (!loadPath) {
      string tempPath = string{copyFolder, "/", tempFname, ".", extension};

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
      loadPath = tempPath;
      rememberTempCopy(tempPath);
    } else {
      LOGI("Loading in place: %s", (const char*)loadPath);
    }

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
    else if (lookup.find("SG-1000") || lookup.find("SG1000")) identifiedSystem = "SG-1000";
    else if (lookup.find("ZX Spectrum 128") || lookup.find("ZXSpectrum128") || lookup.find("Spectrum 128")) identifiedSystem = "ZX Spectrum 128";
    else if (lookup.find("ZX Spectrum") || lookup.find("ZXSpectrum") || lookup.find("ZX")) identifiedSystem = "ZX Spectrum";
    else if (lookup.find("Super Famicom") || lookup.find("SNES")) identifiedSystem = "Super Famicom";
    else if (lookup.find("Famicom") || lookup.find("NES")) identifiedSystem = "Famicom";
    else if (lookup.find("PlayStation Portable") || lookup.find("PSP")) identifiedSystem = "PlayStation Portable";
    else if (lookup.find("PlayStation") || lookup.find("PS1")) identifiedSystem = "PlayStation";
    else if (lookup.find("Neo Geo Pocket Color") || lookup.find("NGPC") || lookup.find("NGC")) identifiedSystem = "Neo Geo Pocket Color";
    else if (lookup.find("Neo Geo Pocket") || lookup.find("NGP") || lookup.find("NGP ")) identifiedSystem = "Neo Geo Pocket";
    else if (lookup.find("Neo Geo CD") || lookup.find("NeoGeoCD") || lookup.find("Neo-Geo-CD") || lookup.find("NGCD") || lookup.find("neogeocd")) {
        identifiedSystem = "Neo Geo CD";
        forceZipLoad = true;
    }
    else if (lookup.find("Neo Geo")) {
        identifiedSystem = "Neo Geo";
        forceZipLoad = true;
    }
    else if (lookup.find("Arcade") || lookup.find("Aleck64") || lookup.find("Aleck 64") || lookup == "MAME") {
        identifiedSystem = "Arcade";
        forceZipLoad = true;
    }
    else if (lookup.find("Mega LD") || lookup.find("LaserActive (SEGA") || lookup.find("SEGA PAC") || lookup.find("Sega PAC")) {
        identifiedSystem = "Mega LD";
    }
    else if (lookup.find("PC Engine LD") || lookup.find("LaserActive (NEC") || lookup.find("NEC PAC") || lookup.find("LDROM")) {
        identifiedSystem = "PC Engine LD";
    }
    else if (lookup.find("Mega CD 32X") || lookup.find("Sega CD 32X")) identifiedSystem = "Mega CD 32X";
    else if (lookup.find("32X")) identifiedSystem = "Mega 32X";
    else if (lookup.find("Mega Drive") || lookup.find("Genesis")) identifiedSystem = "Mega Drive";
    else if (lookup.find("Master System")) identifiedSystem = "Master System";
    else if (lookup.find("Game Gear")) identifiedSystem = "Game Gear";
    else if (lookup.find("Game Boy Advance")) identifiedSystem = "Game Boy Advance";
    else if (lookup.find("Super Game Boy") || lookup == "SGB") identifiedSystem = "Super Game Boy";
    else if (lookup.find("Game Boy Color")) identifiedSystem = "Game Boy Color";
    else if (lookup.find("Game Boy")) identifiedSystem = "Game Boy";
    else if (lookup.find("Pocket Challenge")) identifiedSystem = "Pocket Challenge V2";
    else if (lookup.find("WonderSwan Color") || lookup.find("WSC")) identifiedSystem = "WonderSwan Color";
    else if (lookup.find("WonderSwan") || lookup.find("WS")) identifiedSystem = "WonderSwan";
    else if (lookup.find("PC Engine CD") || lookup.find("PCE CD") || lookup.find("TG16 CD") || lookup.find("TurboGrafx CD") || lookup.find("turbografx-cd")) identifiedSystem = "PC Engine CD";
    else if (lookup.find("SuperGrafx") || lookup.find("Super Grafx") || lookup.find("supergrafx")) identifiedSystem = "SuperGrafx";
    else if (lookup.find("PC Engine") || lookup.find("PC-Engine") || lookup.find("TG16") || lookup.find("PCE") || lookup.find("TurboGrafx") || lookup.find("tg16")) identifiedSystem = "PC Engine";
    else if (lookup.find("MSX2")) identifiedSystem = "MSX2";
    else if (lookup.find("MSX")) identifiedSystem = "MSX";
    else if (lookup.find("Mega CD") || lookup.find("Sega CD")) identifiedSystem = "Mega CD";

    // Super Game Boy: the user's file is a Game Boy game; the SGB boot cart is
    // firmware. Keep loadPath as the user's game through ZIP extract, load it
    // as a Game Boy medium first, then swap to the Super Famicom SGB cart.
    auto superGameBoyCarts = identifiedSystem == "Super Game Boy"
        ? superGameBoyCartCandidates() : std::vector<string>{};
    if (identifiedSystem == "Super Game Boy") {
        if (superGameBoyCarts.empty()) {
            LOGE("Super Game Boy: cartridge ROM missing — set it in Firmware");
            return false;
        }
        currentMedium = mia::Medium::create("Game Boy");
    } else {
        currentMedium = mia::Medium::create(identifiedSystem);
    }
    if (!currentMedium && identifiedSystem == "Neo Geo") {
        currentMedium = mia::Medium::create("Neo Geo MVS");
        if(!currentMedium) currentMedium = mia::Medium::create("Neo Geo AES");
    }
    // ZX Spectrum 128 shares the ZX Spectrum tape medium (the .tap/.tzx/.wav
    // loader is identical; the 48K vs 128K model is chosen at core load()).
    if (!currentMedium && identifiedSystem == "ZX Spectrum 128") {
        currentMedium = mia::Medium::create("ZX Spectrum");
    }
    // Mega CD 32X games are Mega CD discs; the 32X comes from the system's configuration.
    if (!currentMedium && identifiedSystem == "Mega CD 32X") {
        currentMedium = mia::Medium::create("Mega CD");
    }

    if (!currentMedium) {
        LOGE("MIA: Failed to create medium for %s", (const char*)identifiedSystem);
        return false;
    }
    LOGI("MIA: Created medium for %s", (const char*)identifiedSystem);

    bool isDisc = extension == "chd" || extension == "iso" || extension == "cue" || extension == "mdf" || extension == "img";
    // Neo Geo and Arcade ROMs are multi-file zips. Don't extract them or
    // we lose the internal file structure MIA needs for the database lookup.
    bool isNeoGeo = (string)identifiedSystem == "Neo Geo";
    bool isArcade = (string)identifiedSystem == "Arcade";
    if (!forceZipLoad && !isDisc && extension == "zip" && !isNeoGeo && !isArcade) {
        LOGI("MIA: Attempting ZIP extraction for %s", (const char*)loadPath);
        std::vector<u8> romBuffer = currentMedium->read(loadPath);
        if (!romBuffer.empty()) {
            LOGI("MIA: Extracted %zu bytes from ZIP", romBuffer.size());
            string aresExt = "bin";
            if (identifiedSystem == "Game Boy") aresExt = "gb";
            if (identifiedSystem == "Game Boy Color") aresExt = "gbc";
            if (identifiedSystem == "Game Boy Advance") aresExt = "gba";
            if (identifiedSystem == "Super Game Boy") aresExt = "gb";
            if (identifiedSystem == "Super Famicom") aresExt = "sfc";
            if (identifiedSystem == "Famicom") aresExt = "fc";
            if (identifiedSystem == "Nintendo 64") aresExt = "z64";
            if (identifiedSystem == "Mega 32X") aresExt = "32x";
            if (identifiedSystem == "PlayStation Portable") {
                // mia's PSP medium goes by the extension: an EBOOT.PBP starts "\0PBP", an ELF "\x7fELF".
                if (romBuffer.size() >= 4 && memcmp(romBuffer.data(), "\0PBP", 4) == 0) aresExt = "pbp";
                if (romBuffer.size() >= 4 && memcmp(romBuffer.data(), "\x7f" "ELF", 4) == 0) aresExt = "elf";
            }
            if (identifiedSystem == "ZX Spectrum" || identifiedSystem == "ZX Spectrum 128") {
                // The ZX medium dispatches on filename extension (.tap/.tzx/.wav),
                // so sniff the extracted bytes to pick the right one. TZX has a
                // "ZXTape!\x1a" signature; WAV starts with "RIFF"; TAP has no
                // header (starts with a 2-byte big-endian block length).
                if (romBuffer.size() >= 8 && memcmp(romBuffer.data(), "ZXTape!", 7) == 0) aresExt = "tzx";
                else if (romBuffer.size() >= 4 && memcmp(romBuffer.data(), "RIFF", 4) == 0) aresExt = "wav";
                else aresExt = "tap";
                LOGI("MIA: ZX Spectrum zip content sniffed as .%s", (const char*)aresExt);

                // 48K vs 128K model detection by CONTENT, not filename (the
                // Library merged the two ZX entries, so a bare "ZX Spectrum"
                // load must pick the right model). TAP blocks are
                // [len_lo][len_hi][flag][data...]; a header block (flag==0x00)
                // has data[0] = type: 0x00=program, 0x03=bytes/screen,
                // 0x04=microdrive. 128K tapes typically carry a program header
                // whose NAME (data[1..10]) contains "128", and/or a bytes
                // (0x03) screen header + a separate 128K loader. We treat the
                // tape as 128K if ANY header block's name contains "128" or
                // the first data block is a 0x03 bytes header (screen$ loader
                // pattern is 128K-era). Fall back to filename if content is
                // inconclusive.
                if (identifiedSystem == "ZX Spectrum") {
                    bool is128 = false;
                    if (aresExt == "tap" && romBuffer.size() >= 4) {
                        size_t off = 0;
                        // First block's length (TAP is little-endian 16-bit).
                        if (romBuffer.size() >= 2) {
                            size_t blockLen = romBuffer[0] | (romBuffer[1] << 8);
                            size_t dataStart = 2;
                            size_t dataEnd = dataStart + blockLen;
                            if (blockLen >= 2 && dataEnd <= romBuffer.size()) {
                                u8 flag = romBuffer[dataStart];
                                if (flag == 0x00 && blockLen >= 11) {
                                    u8 type = romBuffer[dataStart + 1];
                                    char name[11] = {};
                                    memcpy(name, romBuffer.data() + dataStart + 2, 10);
                                    if (type == 0x03) is128 = true;  // bytes/screen header
                                    if (strstr(name, "128")) is128 = true;
                                }
                            }
                        }
                    }
                    if (!is128) {
                        // Filename fallback (standard convention).
                        string lower = romName;
                        lower = lower.downcase();
                        if (lower.find("128")) is128 = true;  // nall find: truthy = found
                    }
                    if (is128) {
                        identifiedSystem = "ZX Spectrum 128";
                        LOGI("ZX: tape detected as 128K (content/filename)");
                    } else {
                        LOGI("ZX: tape detected as 48K");
                    }
                }
            }

            string rawRomPath = string{copyFolder, "/phobos_rom_raw.", aresExt};
            FILE* rf = fopen((const char*)rawRomPath, "wb");
            if (rf) { fwrite(romBuffer.data(), 1, romBuffer.size(), rf); fclose(rf); }
            loadPath = rawRomPath;
            rememberTempCopy(rawRomPath);
        } else {
            LOGW("MIA: ZIP extraction returned empty buffer");
        }
    }

    // Non-ZIP ZX files (raw .tap/.tzx/.wav): run the same 48K/128K content
    // detection so raw 128K tapes are gated too (not just zipped ones). TAP
    // header block: [len_lo][len_hi][flag][data...]; header (flag==0) has
    // data[0]=type (0x03=bytes/screen → 128K-era) and data[1..10]=name
    // ("128" in the loader name → 128K). Filename fallback for TZX/WAV.
    if (identifiedSystem == "ZX Spectrum" && extension != "zip") {
        bool is128 = false;
        auto raw = nall::file::read(loadPath);
        if (raw.size() >= 4 && loadPath.iendsWith(".tap")) {
            size_t blockLen = raw[0] | (raw[1] << 8);
            size_t dataStart = 2;
            if (blockLen >= 2 && dataStart + blockLen <= raw.size()) {
                u8 flag = raw[dataStart];
                if (flag == 0x00 && blockLen >= 11) {
                    u8 type = raw[dataStart + 1];
                    char name[11] = {};
                    memcpy(name, raw.data() + dataStart + 2, 10);
                    if (type == 0x03) is128 = true;
                    if (strstr(name, "128")) is128 = true;
                }
            }
        }
        if (!is128) {
            string lower = romName;
            lower = lower.downcase();
            if (lower.find("128")) is128 = true;
        }
        if (is128) {
            identifiedSystem = "ZX Spectrum 128";
            LOGI("ZX: raw tape detected as 128K (content/filename)");
        } else {
            LOGI("ZX: raw tape detected as 48K");
        }
    }

    // ── BROKEN-CORE GATE ───────────────────────────────────────────────────
    // Systems that load but produce NO frames (black screen / 0 FPS). Fail the
    // load cleanly BEFORE any core/thread/audio setup — no emulation thread is
    // spawned, so no hang / zombie / crash. Kotlin shows a popup
    // ("<System> Unsupported") and returns to the library. Remove entries once
    // the underlying core is fixed.
    //   - ZX Spectrum 128: UNGATED 2026-08-18 — root cause was the tape pak
    //     lookup: platform->pak() matched root->name() == "ZX Spectrum" but the
    //     128K core names its root "ZX Spectrum 128" → empty pak → tape
    //     frequency 0 → resampler ratio 0 → infinite loop in Cubic::write.
    //     Fixed via root->name().beginsWith("ZX Spectrum") (Task 10c).
    //   - PC Engine / PC Engine CD / SuperGrafx: UNGATED 2026-08-18 — root
    //     cause was the missing PROFILE_PERFORMANCE define (empty PSG::main
    //     deadlocked the scheduler at the first CPU timer sync); fixed via
    //     CMakeLists.txt PROFILE_PERFORMANCE (Task 10c).
    //   - Neo Geo (MVS/AES): loads, BIOS OK, black screen, 0 FPS. UNGATED
    //     2026-08-18 for diagnosis (instrumentation: MIA/VFS logs).
    if (false && identifiedSystem == "Neo Geo") {
        LOGE("%s: unsupported (scheduler hang) — refusing to load", (const char*)identifiedSystem);
        currentMedium.reset();
        return false;
    }

    // Without its BIOS the Mega CD's sub-68000 runs from empty memory (a black screen); the Library
    // asks missingFirmware() first and says what to add.
    if (!missingFirmware((const char*)identifiedSystem).empty()) {
        LOGE("%s: BIOS missing — refusing to load", (const char*)identifiedSystem);
        currentMedium.reset();
        return false;
    }

    auto loadResult = currentMedium->load(loadPath);
    if (loadResult != successful) {
        LOGE("MIA: Failed to load medium for %s at %s (Result: %d)", (const char*)identifiedSystem, (const char*)loadPath, (s32)loadResult.result);
        return false;
    }
    LOGI("MIA: Successfully loaded medium %s", (const char*)loadPath);

    if (identifiedSystem == "Super Game Boy") {
        // User's Game Boy ROM is now in currentMedium (ZIP already extracted).
        secondaryMedium = currentMedium;
        currentMedium.reset();
        string loadedCart;
        for (auto& cart : superGameBoyCarts) {
            currentMedium = mia::Medium::create("Super Famicom");
            if (!currentMedium) {
                LOGE("Super Game Boy: failed to create Super Famicom medium");
                secondaryMedium.reset();
                return false;
            }
            auto sgbResult = currentMedium->load(cart);
            if (sgbResult == successful) {
                loadedCart = cart;
                break;
            }
            LOGW("Super Game Boy: cart load failed at %s (Result: %d); trying next", (const char*)cart, (s32)sgbResult.result);
            currentMedium.reset();
        }
        if (!loadedCart) {
            LOGE("Super Game Boy: all configured cartridge ROMs failed to load");
            secondaryMedium.reset();
            return false;
        }
        LOGI("Super Game Boy: loaded SGB cart %s + Game Boy game %s", (const char*)loadedCart, (const char*)loadPath);
    }

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
      // Join any parked zombie from a previous abandon BEFORE re-initializing
      // the N64 singleton hardware: System::load → System::unload frees
      // rdram.ram / cartridge.rom / dd.disk, and a still-running zombie reads
      // those → SIGSEGV in CPU::LW (the "crash while unloading" bug).
      joinAbandonedThreads();
      ::ares::Nintendo64::vulkan.enable = true; // DEFAULT TO VULKAN
      // Set pipeline cache path for Vulkan shader persistence.
      // Prefer the user-configured Vulkan cache directory (Task 40); fall back
      // to the saves directory so the cache persists next to the save data.
      string cacheDir = vulkanCachePath;
      if (!cacheDir) cacheDir = savesPath;
      if (cacheDir && strlen(cacheDir) > 0) {
        ::ares::Nintendo64::vulkan.pipelineCachePath = string{cacheDir, "/n64_vulkan_pipeline_cache.bin"};
        LOGI("N64: Pipeline cache path: %s", (const char*)::ares::Nintendo64::vulkan.pipelineCachePath);
      }
      if (n64UpscaleFactor < 1) n64UpscaleFactor = 1;
    if (n64UpscaleFactor > 4) n64UpscaleFactor = 4; // memory safety: see setN64Upscale()
      ::ares::Nintendo64::vulkan.internalUpscale = (u32)n64UpscaleFactor.load();
      ::ares::Nintendo64::vulkan.outputUpscale = n64SupersampleScanout.load() ? 1 : (u32)n64UpscaleFactor.load();
      ::ares::Nintendo64::vulkan.disableVideoInterfaceProcessing = n64DisableVIProcessing.load();
      ::ares::Nintendo64::vulkan.weaveDeinterlacing = n64WeaveDeinterlacing.load();
      ::ares::Nintendo64::vulkan.supersampleScanout = n64SupersampleScanout.load();
      ::ares::Nintendo64::vi.overclockPercent = n64ViOverclock.load();
      ::ares::Nintendo64::cpu.countPerOp = n64CountPerOp.load();
      ::ares::Nintendo64::cpu.overclockFactor = n64CpuOverclock.load();
      ::ares::Nintendo64::cpu.fasterSync = n64FasterSync.load();
      ::ares::Nintendo64::cpu.skipCaches = n64SkipCaches.load();
      ::ares::Nintendo64::cpu.recompiler.enabled = n64Recompiler.load();
      ::ares::Nintendo64::rsp.recompiler.enabled = n64Recompiler.load();
      ::ares::Nintendo64::rsp.taskMode = n64RspTaskMode.load();
      ::ares::Nintendo64::vulkan.asynchronousRdp = n64AsyncRdp.load();
      // video() below presents the Vulkan scanout directly (see VI::refresh).
      ::ares::Nintendo64::vulkan.frontendPresentsScanout = true;
      bool is64DD = (identifiedSystem == "Nintendo 64DD" || extension == "ndd" || extension == "d64" || secondaryMedium != nullptr);
      ::ares::Nintendo64::system.expansionPak = n64ExpansionPak.load();

      const char* regionString = getRegion(
          is64DD ? "[Nintendo] Nintendo 64DD (NTSC-U)" : "[Nintendo] Nintendo 64 (NTSC)",
          is64DD ? "[Nintendo] Nintendo 64DD (NTSC-J)" : "[Nintendo] Nintendo 64 (NTSC-J)",
          "[Nintendo] Nintendo 64 (PAL)"
      );

      success = ::ares::Nintendo64::load(root, regionString);
    } else if (identifiedSystem == "Arcade") {
      // Board is chosen by MIA from Arcade.bml / VsSystem.bml after the zip loads.
      string board = currentMedium && currentMedium->pak ? currentMedium->pak->attribute("board") : "";
      if (board == "nintendo/aleck64") {
        joinAbandonedThreads();
        ::ares::Nintendo64::vulkan.enable = true;
        string cacheDir = vulkanCachePath;
        if (!cacheDir) cacheDir = savesPath;
        if (cacheDir && strlen(cacheDir) > 0) {
          ::ares::Nintendo64::vulkan.pipelineCachePath = string{cacheDir, "/n64_vulkan_pipeline_cache.bin"};
        }
        if (n64UpscaleFactor < 1) n64UpscaleFactor = 1;
        if (n64UpscaleFactor > 4) n64UpscaleFactor = 4;
        ::ares::Nintendo64::vulkan.internalUpscale = (u32)n64UpscaleFactor.load();
        ::ares::Nintendo64::vulkan.outputUpscale = n64SupersampleScanout.load() ? 1 : (u32)n64UpscaleFactor.load();
        ::ares::Nintendo64::vulkan.disableVideoInterfaceProcessing = n64DisableVIProcessing.load();
        ::ares::Nintendo64::vulkan.weaveDeinterlacing = n64WeaveDeinterlacing.load();
        ::ares::Nintendo64::vulkan.supersampleScanout = n64SupersampleScanout.load();
        ::ares::Nintendo64::vi.overclockPercent = n64ViOverclock.load();
        ::ares::Nintendo64::cpu.countPerOp = n64CountPerOp.load();
        ::ares::Nintendo64::cpu.overclockFactor = n64CpuOverclock.load();
        ::ares::Nintendo64::cpu.fasterSync = n64FasterSync.load();
        ::ares::Nintendo64::cpu.skipCaches = n64SkipCaches.load();
        ::ares::Nintendo64::cpu.recompiler.enabled = n64Recompiler.load();
        ::ares::Nintendo64::rsp.recompiler.enabled = n64Recompiler.load();
        ::ares::Nintendo64::rsp.taskMode = n64RspTaskMode.load();
        ::ares::Nintendo64::vulkan.asynchronousRdp = n64AsyncRdp.load();
        ::ares::Nintendo64::vulkan.frontendPresentsScanout = true;
        success = ::ares::Nintendo64::load(root, "[SETA] Aleck 64");
      } else if (board == "sega/sg1000a") {
        success = ::ares::SG1000::load(root, "[Sega] SG-1000A");
      } else if (board == "nintendo/vs") {
        // Vs. UniSystem is in Arcade.bml's companion VsSystem.bml, but its
        // root is still "Famicom" and needs separate cabinet input wiring.
        // Overnight #6 is Aleck64 (+ SG-1000A); refuse Vs until that lands.
        LOGE("Arcade: Vs. UniSystem is not supported yet");
      } else {
        LOGE("Arcade: unsupported board '%s'", (const char*)board);
      }
    } else if (identifiedSystem == "Super Famicom" || identifiedSystem == "Super Game Boy") {
      ::ares::SuperFamicom::ppu.implementation = &::ares::SuperFamicom::ppuPerformanceImpl;
      ::ares::SuperFamicom::ppu.accurate = false;
      success = ::ares::SuperFamicom::load(root, getRegion("[Nintendo] Super Famicom (NTSC)", "[Nintendo] Super Famicom (NTSC)", "[Nintendo] Super Famicom (PAL)"));
    } else if (identifiedSystem == "Famicom") {
      success = ::ares::Famicom::load(root, getRegion("[Nintendo] Famicom (NTSC-U)", "[Nintendo] Famicom (NTSC-J)", "[Nintendo] Famicom (PAL)"));
    } else if (identifiedSystem == "PlayStation") {
      success = ::ares::PlayStation::load(root, getRegion("[Sony] PlayStation (NTSC-U)", "[Sony] PlayStation (NTSC-J)", "[Sony] PlayStation (PAL)"));
    } else if (identifiedSystem == "PlayStation Portable") {
      // Every game shares one memory stick (saves in PSP/SAVEDATA, as on a PSP) unless the user picked a folder.
      string memoryStick = pspMemoryStickPath;
      if (!memoryStick && savesPath) memoryStick = {savesPath, "/PlayStation Portable/Memory Stick"};
      if (memoryStick) directory::create(memoryStick);
      LOGI("PSP: memory stick at '%s'", (const char*)memoryStick);
      ::ares::PlayStationPortable::option("Memory Stick", memoryStick);
      ::ares::PlayStationPortable::option("Recompiler", "true");
      success = ::ares::PlayStationPortable::load(root, "[Sony] PlayStation Portable");
    } else if (identifiedSystem == "Game Boy Advance") {
      success = ::ares::GameBoyAdvance::load(root, "[Nintendo] Game Boy Advance");
    } else if (identifiedSystem == "Game Boy") {
      success = ::ares::GameBoy::load(root, "[Nintendo] Game Boy");
    } else if (identifiedSystem == "Game Boy Color") {
      success = ::ares::GameBoy::load(root, "[Nintendo] Game Boy Color");
    } else if (identifiedSystem == "Mega Drive") {
      success = ::ares::MegaDrive::load(root, getRegion("[Sega] Mega Drive (NTSC-U)", "[Sega] Mega Drive (NTSC-J)", "[Sega] Mega Drive (PAL)"));
    } else if (identifiedSystem == "Neo Geo CD") {
       success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo CD");
    } else if (identifiedSystem == "Neo Geo") {
       success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo MVS");
       if(!success) success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo AES");
    } else if (identifiedSystem == "Master System") {
       success = ::ares::MasterSystem::load(root, getRegion("[Sega] Master System (NTSC-U)", "[Sega] Master System (NTSC-J)", "[Sega] Master System (PAL)"));
    } else if (identifiedSystem == "Game Gear") {
       success = ::ares::MasterSystem::load(root, getRegion("[Sega] Game Gear (NTSC-U)", "[Sega] Game Gear (NTSC-J)", "[Sega] Game Gear (PAL)"));
    } else if (identifiedSystem == "Mega LD") {
       // pak() attaches the PAC BIOS of the configuration's region, so the region follows the BIOSes set: the
       // preferred one when its BIOS is there, else the other.
       string config = getRegion(
           "[Pioneer] LaserActive (SEGA PAC) (NTSC-U)",
           "[Pioneer] LaserActive (SEGA PAC) (NTSC-J)",
           "[Pioneer] LaserActive (SEGA PAC) (NTSC-U)");
       bool japan = (bool)config.find("NTSC-J");
       if (!firmwareSet(japan ? "fw_laseractive_sega_jp" : "fw_laseractive_sega_us")) japan = !japan;
       success = ::ares::MegaDrive::load(root, japan
           ? "[Pioneer] LaserActive (SEGA PAC) (NTSC-J)" : "[Pioneer] LaserActive (SEGA PAC) (NTSC-U)");
    } else if (identifiedSystem == "PC Engine LD") {
       success = ::ares::PCEngine::load(root, getRegion(
           "[Pioneer] LaserActive (NEC PAC) (NTSC-U)",
           "[Pioneer] LaserActive (NEC PAC) (NTSC-J)",
           "[Pioneer] LaserActive (NEC PAC) (NTSC-U)"));
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
    } else if (identifiedSystem == "Mega 32X" || identifiedSystem == "Mega CD 32X") {
       // The SH-2s' recompiler is off unless asked for. Its code goes in the
       // executable buffer the N64's recompilers use, which M32X::power()
       // releases, so a parked N64 thread must be gone first.
       joinAbandonedThreads();
       ::ares::MegaDrive::option("Recompiler", "true");
       if (identifiedSystem == "Mega 32X") success = ::ares::MegaDrive::load(root, getRegion("[Sega] Mega 32X (NTSC-U)", "[Sega] Mega 32X (NTSC-J)", "[Sega] Mega 32X (PAL)"));
       else success = ::ares::MegaDrive::load(root, getRegion("[Sega] Mega CD 32X (NTSC-U)", "[Sega] Mega CD 32X (NTSC-J)", "[Sega] Mega CD 32X (PAL)"));
    } else if (identifiedSystem == "WonderSwan Color") {
       success = ::ares::WonderSwan::load(root, "[Bandai] WonderSwan Color");
    } else if (identifiedSystem == "WonderSwan") {
       success = ::ares::WonderSwan::load(root, "[Bandai] WonderSwan");
    } else if (identifiedSystem == "Pocket Challenge V2") {
       success = ::ares::WonderSwan::load(root, "[Benesse] Pocket Challenge V2");
    } else if (identifiedSystem == "Neo Geo Pocket" || identifiedSystem == "Neo Geo Pocket Color") {
       // MIA always returns "Neo Geo Pocket" but the library may specify
       // "Neo Geo Pocket Color" — use the system name from the library
       // to disambiguate, since the TLCS900H CPU boots differently per model.
       string aresName = "[SNK] Neo Geo Pocket";
       if (systemName.downcase().find("color") || identifiedSystem == "Neo Geo Pocket Color") aresName = "[SNK] Neo Geo Pocket Color";
       success = ::ares::NeoGeoPocket::load(root, aresName);
    } else if (identifiedSystem == "Neo Geo CD") {
       success = ::ares::NeoGeo::load(root, "[SNK] Neo Geo CD");
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
    } else if (identifiedSystem == "ZX Spectrum 128") {
       success = ::ares::ZXSpectrum::load(root, "[Sinclair] ZX Spectrum 128");
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
      applyVideoSettings();
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

      // Import game save data (SRAM, EEPROM, Flash, RTC, 64DD disk) — always
      // restores. Must run BEFORE connectDevices(): the core reads cartridge/
      // disk save files from the medium pak at port-connect time, so a
      // post-connect import never reaches the cartridge (fresh each load).
      if (savesPath) {
        // Per-game save subdirectory (same key as flushSavesToDisk):
        // saves/<System>/<RomBase>/. SGB shares the Game Boy folder so the
        // same ROM keeps progress across GB and SGB launches.
        string romKey = currentRomBase;
        romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
        if (romKey.size() == 0) romKey = "rom";
        string sysFolder = identifiedSystem == "Super Game Boy" ? string{"Game Boy"} : root->name();
        string saveDir = {savesPath, "/", sysFolder, "/", romKey, "/"};
        directory::create(saveDir);
        // Import matching save files from the persistent dir into a given pak.
        auto importIntoPak = [&](auto& pak) -> void {
          for (auto& saveNode : pak->files()) {
            string fileName = saveNode->name();
            if (!fileName.endsWith(".ram") && !fileName.endsWith(".srm") &&
                !fileName.endsWith(".eeprom") && !fileName.endsWith(".card") &&
                !fileName.endsWith(".sav") && !fileName.endsWith(".fla") &&
                !fileName.endsWith(".flash") && !fileName.endsWith(".rtc") &&
                !fileName.endsWith(".disk") && !fileName.endsWith(".disk.error") &&
                !fileName.endsWith(".cartrom")) continue;
            string fullPath = {saveDir, fileName};
            auto existing = file::read(fullPath);
            if (existing.size() == 0) continue;
            if (auto fp = pak->write(fileName)) {
              // vfs::memory::write silently drops bytes past the current size —
              // resize to the imported size first (mirrors MIA's Pak::load).
              if (fp->size() != existing.size()) fp->resize(existing.size());
              fp->write({existing.data(), (u32)existing.size()});
              LOGI("Saves: imported %s (%zu bytes) for %s", (const char*)fileName, existing.size(), (const char*)identifiedSystem);
            }
          }
        };
        // Cartridge medium pak.
        if (currentMedium && currentMedium->pak) importIntoPak(currentMedium->pak);
        // 64DD disk medium pak (program.disk / program.disk.error).
        if (secondaryMedium && secondaryMedium->pak) importIntoPak(secondaryMedium->pak);
        // System pak (root->pak()): time.rtc for 64DD.
        if (root) {
          auto sysPak = root->pak();
          if (sysPak) importIntoPak(sysPak);
        }
      }

      bool playStation = root->name() == "PlayStation";
      if (playStation) loadPs1MemoryCards();
      connectDevices(root);
      // Super Game Boy: the nested Game Boy Cartridge Slot only appears after
      // the SGB cart connects. Do NOT re-run connectDevices — that reconnects
      // the SNES cart, tears down the ICD, and leaves an orphaned GB port.
      // Only allocate empty Game Boy cartridge ports that appeared under ICD.
      if (identifiedSystem == "Super Game Boy") {
        bool gbAttached = false;
        for (auto& port : root->find<Node::Port>()) {
          if (port->type() != "Cartridge") continue;
          if (port->family() != "Game Boy" && port->family() != "Game Boy Color") continue;
          if (port->connected()) { gbAttached = true; continue; }
          if (port->allocate()) {
            LOGI("VFS: Connecting nested %s (%s) for SGB", (const char*)port->name(), (const char*)port->family());
            port->connect();
            if (port->connected()) gbAttached = true;
          } else {
            LOGE("VFS: FAILED to allocate nested Game Boy cartridge for SGB");
          }
        }
        if (!gbAttached) {
          LOGE("Super Game Boy: Game Boy cartridge did not attach — refusing to start");
          setEmulationRunning(false);
          root->unload();
          root.reset();
          currentMedium.reset();
          secondaryMedium.reset();
          return false;
        }
        for (auto& setting : root->find<Node::Setting::Boolean>()) {
          if (setting->name() == "Fast Boot") setting->setValue(fastBootAtomic);
        }
        for (auto& setting : root->find<Node::Setting::Setting>()) setting->setLatch();
      }
      if (playStation) trackPs1MemoryCards();

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
          if (identifiedSystem == "Game Boy" || identifiedSystem == "Game Boy Color"
              || identifiedSystem == "Super Game Boy") {
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

      // The old system's teardown (unloadSystem at the top) is complete and
      // the new root is fully loaded — re-allow thread spawns, then start.
      systemUnloading.store(false);
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

  // Folder under savesPath for battery saves. SGB shares "Game Boy" so the
  // same ROM keeps progress when launched as GB or SGB (root is Super Famicom).
  static auto saveSystemFolder() -> string {
    if (!root) return {};
    if (root->name() == "Super Famicom" && secondaryMedium) return "Game Boy";
    return root->name();
  }

  // Flush cartridge/battery saves to the persistent saves directory. Called
  // on pause (so backing out / app-switch doesn't lose progress) and on clean
  // unload. Writes to savesPath/<system>/<RomBase>/ so different games never
  // overwrite each other's saves. Only runs while the system is loaded and the
  // emulation thread is NOT mid-frame (pause path holds the emulation; unload
  // path holds systemMutex).
  // The running game's save folder, saves/<System>/<RomBase>/, created if missing.
  static auto gameSaveFolder() -> string {
    string romKey = currentRomBase;
    romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
    if (romKey.size() == 0) romKey = "rom";
    string saveDir = {savesPath, "/", saveSystemFolder(), "/", romKey, "/"};
    directory::create(saveDir);
    return saveDir;
  }

  // Writes what the MSX's data tape recorded since it was last written to its .wav.
  static auto saveMsxDataTape() -> void {
    if (!msxDataTape || !msxDataTape->pak) return;
    if (msxDataTapeIn) ::ares::MSX::tapeDeck.tray.tape.save();
    if (!msxDataTape->pak->attribute("modified").boolean()) return;
    if (msxDataTape->save(msxDataTapePath)) {
      msxDataTape->pak->setAttribute("modified", false);
      LOGI("Saves: wrote the MSX data tape %s", (const char*)msxDataTapePath);
    } else {
      LOGE("Saves: couldn't write the MSX data tape %s", (const char*)msxDataTapePath);
    }
  }

  static auto flushSavesToDisk() -> void {
    if (!savesPath || !root) return;
    // CRITICAL: flush the LIVE core state into the pak(s) FIRST. The game's
    // SRAM/EEPROM/Flash live in the ares core (e.g. N64 cartridge.ram, 64DD
    // disk), NOT in the pak — reading the pak directly returns whatever was
    // last imported/written, so the flush would persist STALE data. root->save()
    // → Cartridge::save() / DD::save() / RTC → live state into the pak(s), then
    // we copy to the persistent dir.
    root->save();
    string sysName = saveSystemFolder();
    if (root->name() == "PlayStation") flushPs1MemoryCards();
    if (root->name().beginsWith("MSX")) saveMsxDataTape();
    // Per-game subdirectory (keyed by ROM base name) so games don't clobber
    // each other's saves: saves/<System>/<RomBase>/
    string romKey = currentRomBase;
    // Sanitize: the ROM name can contain chars that are legal on Linux but
    // awkward on some filesystems — keep it simple, drop slashes/colons.
    romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
    if (romKey.size() == 0) romKey = "rom";
    string saveDir = {savesPath, "/", sysName, "/", romKey, "/"};
    directory::create(saveDir);
    bool wrote = false;
    // Copy a save node's bytes to disk if it matches the save filter and is
    // non-empty/non-zero.
    auto flushNode = [&](auto& saveNode) -> void {
      string fileName = saveNode->name();
      if (!fileName.endsWith(".ram") && !fileName.endsWith(".srm") &&
          !fileName.endsWith(".eeprom") && !fileName.endsWith(".card") &&
          !fileName.endsWith(".sav") && !fileName.endsWith(".fla") &&
          !fileName.endsWith(".flash") && !fileName.endsWith(".rtc") &&
          !fileName.endsWith(".disk") && !fileName.endsWith(".disk.error") &&
          !fileName.endsWith(".cartrom")) return;
      auto fp = saveNode;
      fp->seek(0);
      auto size = fp->size();
      if (size == 0) return;
      std::vector<u8> buf(size);
      fp->read({buf.data(), size});
      bool allZero = true;
      for (auto b : buf) { if (b != 0) { allZero = false; break; } }
      if (allZero) return;
      string fullPath = {saveDir, fileName};
      file::write(fullPath, {buf.data(), size});
      wrote = true;
      LOGI("Saves: flushed %s (%zu bytes) for %s [%s]", (const char*)fileName, size, (const char*)sysName, (const char*)romKey);
    };
    // Cartridge medium pak.
    if (currentMedium && currentMedium->pak) {
      for (auto& saveNode : currentMedium->pak->files()) flushNode(saveNode);
    }
    // 64DD disk medium pak (program.disk / program.disk.error) — the disk save
    // area and error table live in secondaryMedium, NOT the cartridge pak.
    if (secondaryMedium && secondaryMedium->pak) {
      for (auto& saveNode : secondaryMedium->pak->files()) flushNode(saveNode);
    }
    // System pak (root->pak()): holds time.rtc for 64DD (RTC save area) and
    // pif.rom for every N64. The RTC is written here by DD::RTC::save() via
    // root->save(), so we must flush it too or the 64DD RTC never persists
    // ("Error 48 — Date/Time not set" on every boot after first save).
    if (root && root->pak()) {
      for (auto& saveNode : root->pak()->files()) flushNode(saveNode);
    }
    if (!wrote) LOGI("Saves: flush complete (nothing to write) for %s [%s]", (const char*)sysName, (const char*)romKey);
  }

  auto setPause(bool paused) -> void {
    isPausedAtomic = paused;
    // Flush saves when PAUSING (leaving gameplay): the user may back out or
    // swipe the app away, which skips the clean-unload export. The emulation
    // thread is idle at this point (pause gate), so this is race-free.
    if (paused) flushSavesToDisk();
    // Stop the audio stream while paused so it doesn't keep draining with no
    // new samples (underrun pops in the pause menu); restart it on resume.
    // The audio thread checks isPausedAtomic and drops queued samples while
    // paused, so stale audio never plays on resume.
    std::lock_guard<std::mutex> lock(audioMutex);
    phobos::host::pauseAudio(paused);
    LOGI("Emulation %s", paused ? "paused" : "resumed");
  }
  auto setFastForward(bool enabled) -> void { fastForwardAtomic = enabled; LOGI("Fast forward %s", enabled ? "enabled" : "disabled"); }
  auto setFastForwardSpeed(f32 speed) -> void { ffSpeedLimitAtomic = speed; LOGI("Fast forward speed set to %.1fx", (f64)speed); }
  auto setNgcdLoadSpeed(s32 speed) -> void { ngcdLoadSpeed = std::max(1, speed); LOGI("Neo Geo CD loading speed set to %dx", std::max(1, speed)); }
  auto setZxLoadSpeed(s32 speed) -> void { zxLoadSpeed = std::max(1, speed); LOGI("ZX Spectrum tape loading speed set to %dx", std::max(1, speed)); }
  auto setMsxLoadSpeed(s32 speed) -> void { msxLoadSpeed = std::max(1, speed); LOGI("MSX tape loading speed set to %dx", std::max(1, speed)); }
  auto setZxTapeAuto(bool enabled) -> void { zxTapeAuto = enabled; LOGI("ZX Spectrum tape: automatic control %s", enabled ? "on" : "off"); }
  auto setN64DebugLogging(bool enabled) -> void { n64DebugLoggingAtomic = enabled; LOGI("N64 debug logging %s", enabled ? "enabled" : "disabled"); }
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
    ::ares::Nintendo64::vi.overclockPercent = n64ViOverclock.load();
    ::ares::Nintendo64::cpu.countPerOp = n64CountPerOp.load();
    ::ares::Nintendo64::cpu.overclockFactor = n64CpuOverclock.load();
    ::ares::Nintendo64::cpu.fasterSync = n64FasterSync.load();
    ::ares::Nintendo64::cpu.skipCaches = n64SkipCaches.load();
    ::ares::Nintendo64::vulkan.outputUpscale = n64SupersampleScanout.load() ? 1 : (u32)n64UpscaleFactor.load();
    ::ares::Nintendo64::cpu.recompiler.enabled = n64Recompiler.load();
    ::ares::Nintendo64::rsp.recompiler.enabled = n64Recompiler.load();
    ::ares::Nintendo64::rsp.taskMode = n64RspTaskMode.load();
    ::ares::Nintendo64::vulkan.asynchronousRdp = n64AsyncRdp.load();
    #endif
    LOGI("System reset requested");
  }
  auto frameAdvance() -> void { lock_guard<recursive_mutex> lock(*runMutex); if (root) root->run(); }
  auto dumpNgGfx(const char* dir) -> void {
    lock_guard<recursive_mutex> lock(*runMutex);
    ::ares::NeoGeo::system.dumpNgGfx(dir);
  }
  auto setMuteAudio(bool muted) -> void { muteAudioAtomic = muted; }
  auto setShader(const char* path) -> bool { return true; }
  // The loaded game's battery saves, for the pause menu's save import and export: one
  // "name\tsize\tpath" entry per save the core keeps, where path is the file
  // flushSavesToDisk() (or exportControllerPak() for save.pak) writes it to.
  auto getSaveFiles() -> std::vector<string> {
    lock_guard<recursive_mutex> lock(*runMutex);
    std::vector<string> files;
    if (!root || !savesPath) return files;
    string romKey = currentRomBase;
    romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
    if (romKey.size() == 0) romKey = "rom";
    string saveDir = {savesPath, "/", saveSystemFolder(), "/", romKey, "/"};
    auto listPak = [&](auto& pak) -> void {
      for (auto& file : pak->files()) {
        string name = file->name();
        if (name != "save.eeprom" && name != "save.ram" && name != "save.flash") continue;
        files.push_back(string{name, "\t", (u64)file->size(), "\t", saveDir, name});
      }
    };
    if (currentMedium && currentMedium->pak) listPak(currentMedium->pak);
    // SGB: battery lives on the Game Boy secondary medium, not the SGB cart.
    if (secondaryMedium && secondaryMedium->pak) listPak(secondaryMedium->pak);
    if (player1PakDir && root->name() == "Nintendo 64") {
      if (auto fp = player1PakDir->read("save.pak")) {
        files.push_back(string{"save.pak\t", (u64)fp->size(), "\t", savesPath, "/Nintendo 64/", romKey, "/save.pak"});
      }
    }
    return files;
  }

  // The loaded game's player-one buttons by the pad bit that presses them, for the controller
  // settings: one "bit\tname" entry per pair, resolveButtonBit read backwards. A button several
  // bits press (the Neo Geo shoulder combos) has an entry for each. Keyboards are left out, and so
  // is the ZX Spectrum, whose pad plays the keys of its control scheme.
  auto getButtonNames() -> std::vector<string> {
    lock_guard<recursive_mutex> lock(*runMutex);
    std::vector<string> names;
    if (!root || isZxKeyboardSystem(root->name())) return names;
    string systemName = root->name();
    for (auto& button : root->find<Node::Input::Button>()) {
      if (controllerPlayerIndex(button) != 0) continue;
      bool keyboard = false;
      Node::Object node = button;
      for (int depth = 0; depth < 8 && node && !keyboard; depth++, node = ares::Node::parent(node)) {
        keyboard = node->name().endsWith("Keyboard");
      }
      if (keyboard) continue;
      u32 bits = resolveButtonBit(button->name(), systemName, orientationVertical);
      for (u32 bit = 1; bits; bit <<= 1) {
        if (!(bits & bit)) continue;
        bits &= ~bit;
        names.push_back(string{(u64)bit, "\t", button->name()});
      }
    }
    return names;
  }

  // Writes every battery save, the Controller Pak's included, to disk now instead of at unload.
  auto flushSaves() -> void {
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> lock(*runMutex);
    if (root) {
      flushSavesToDisk();
      exportControllerPak();
    }
    isPausedAtomic.store(wasPaused);
  }

  auto saveState(const char* path) -> bool {
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> lock(*runMutex);
    if (!root) { isPausedAtomic.store(wasPaused); return false; }
    drainN64RdpIfAsync();
    auto s = root->serialize(true);
    // A core that can't make a state gives an empty one, which couldn't be loaded back.
    bool result = s.size() && nall::file::write(path, {s.data(), s.size()});
    LOGI("Save state to %s: %s (%u bytes)", path, result ? "success" : "failed", (unsigned)s.size());
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
    drainN64RdpIfAsync(); // in-flight GPU writes must not land in the restored RDRAM
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
  auto setN64ViOverclock(s32 percent) -> void {
    if (percent < 100) percent = 100;
    if (percent > 300) percent = 300;
    n64ViOverclock = percent;
    LOGI("N64 VI overclock set to %d%% (applies on next reset)", percent);
  }
  auto setN64CountPerOp(s32 value) -> void {
    if (value < 1) value = 1;
    if (value > 3) value = 3;
    n64CountPerOp = value;
    LOGI("N64 count per op set to %d (applies on next reset)", value);
  }
  auto setN64CpuOverclock(s32 factor) -> void {
    if (factor < 0) factor = 0;
    if (factor > 5) factor = 5;
    n64CpuOverclock = factor;
    LOGI("N64 CPU overclock factor set to %d (2^%d) (applies on next reset)", factor, factor);
  }
  // Unlike the VI settings above this only changes whether SyncFull waits for
  // the GPU, so it is safe to apply live. Turning it off takes effect at the
  // next SyncFull, which waits for all earlier work.
  auto setN64AsyncRdp(bool enabled) -> void {
    n64AsyncRdp = enabled;
    #if defined(CORE_N64)
    ::ares::Nintendo64::vulkan.asynchronousRdp = enabled;
    #endif
    LOGI("N64 asynchronous RDP %s (applies immediately)", enabled ? "enabled" : "disabled");
  }
  auto setN64FasterSync(bool enabled) -> void {
    n64FasterSync = enabled;
    #if defined(CORE_N64)
    ::ares::Nintendo64::cpu.fasterSync = enabled;
    #endif
    LOGI("N64 faster sync %s (applies immediately)", enabled ? "enabled" : "disabled");
  }
  auto setN64SkipCaches(bool enabled) -> void {
    n64SkipCaches = enabled;
    #if defined(CORE_N64)
    ::ares::Nintendo64::cpu.skipCaches = enabled;
    #endif
    LOGI("N64 skip cache timing %s (applies immediately)", enabled ? "enabled" : "disabled");
  }
  auto setN64RspTaskMode(bool enabled) -> void {
    n64RspTaskMode = enabled;
    #if defined(CORE_N64)
    ::ares::Nintendo64::rsp.taskMode = enabled;
    #endif
    LOGI("N64 RSP task mode %s (applies immediately)", enabled ? "enabled" : "disabled");
  }
  auto setPinFastestCore(bool enabled) -> void {
    pinFastestCore = enabled;
    LOGI("Emulation thread %s", enabled ? "pinned to the fastest CPU cores" : "placed by the scheduler");
  }
  auto setBusyWaitPacing(bool enabled) -> void {
    busyWaitPacing = enabled;
    LOGI("N64 frame pacing %s between frames", enabled ? "busy-waits" : "sleeps");
  }
  auto setVideoSettings(bool overscan, bool colorEmulation, bool interframeBlending) -> void {
    videoOverscan = overscan;
    videoColorEmulation = colorEmulation;
    videoInterframeBlending = interframeBlending;
    LOGI("Video: overscan %s, color emulation %s, interframe blending %s",
         overscan ? "on" : "off", colorEmulation ? "on" : "off", interframeBlending ? "on" : "off");
    lock_guard<std::recursive_mutex> lock(systemMutex);
    applyVideoSettings();
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

  // N64 Player 1 controller pak (Rumble Pak / Controller Pak). Hot-swappable:
  // re-allocates the Gamepad's "Pak" sub-port. The Controller Pak's save.pak
  // is flushed to disk on swap and on unload.
  static auto exportControllerPak() -> void {
    if (!player1PakDir || !savesPath) return;
    if (auto fp = player1PakDir->read("save.pak")) {
      fp->seek(0);
      auto size = fp->size();
      if (!size) return;
      std::vector<u8> buf(size);
      fp->read({buf.data(), size});
      bool allZero = true;
      for (auto b : buf) { if (b != 0) { allZero = false; break; } }
      if (allZero) return;
      // Per-ROM Controller Pak (same key as cartridge saves).
      string romKey = currentRomBase;
      romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
      if (romKey.size() == 0) romKey = "rom";
      string saveDir = {savesPath, "/Nintendo 64/", romKey, "/"};
      directory::create(saveDir);
      file::write({saveDir, "save.pak"}, {buf.data(), size});
      LOGI("Saves: exported save.pak (%zu bytes) for Nintendo 64 [%s]", size, (const char*)romKey);
    }
  }

  auto setN64Pak(const char* pakName) -> void {
    lock_guard<std::recursive_mutex> lock(systemMutex);
    string desired = pakName ? pakName : "None";
    if (n64Pak == desired) return;
    n64Pak = desired;
    LOGI("N64 controller pak set to %s", (const char*)desired);
    if (root && root->name() == "Nintendo 64" && cachedPlayer1) {
      for (auto& pakPort : cachedPlayer1->find<Node::Port>()) {
        if (pakPort->type() != "Pak") continue;
        // allocate() disconnects the old pak first; Gamepad::disconnect()
        // flushes Controller Pak RAM into player1PakDir — export it, then
        // connect the newly allocated slot.
        pakPort->allocate(desired);
        exportControllerPak();
        if (desired != "None" && pakPort->connected()) {
          pakPort->connect();
          LOGI("VFS: Attached %s to Player 1 (N64)", (const char*)desired);
        }
        break;
      }
    }
  }

  auto getRumbleState() -> bool {
    return rumbleState.load();
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
  // Returns the NEW ps1AnalogMode value (true = analog ON) so the Kotlin
  // side can persist the correct state — it previously returned "true"
  // (toggle succeeded) always, so DataStore was set to true on EVERY toggle
  // and the pause-menu switch showed ON even when analog was actually OFF.
  auto togglePs1AnalogMode() -> bool {
    lock_guard<std::recursive_mutex> lock(systemMutex);
    if (!root || root->name() != "PlayStation") return false;
    ps1AnalogMode = !ps1AnalogMode;
    LOGI("PS1 analog toggle -> %d", (int)ps1AnalogMode);
    connectDevices(root);
    return ps1AnalogMode;
  }
  auto setStickToDpad(bool enabled) -> void {
    // Deprecated
  }
  auto setCustomDriverPath(const char* path) -> void { customDriverPath = path ? (string)path : ""; LOGI("Custom driver path set: %s", (const char*)customDriverPath); }
  auto setOrientationMode(bool vertical) -> void { orientationVertical = vertical; LOGI("Orientation mode set to %s", vertical ? "Vertical" : "Horizontal"); }
  auto setRomFd(s32 fd) -> void { lock_guard<recursive_mutex> lock(systemMutex); if (romFd != -1) ::close(romFd); romFd = fd; }
  auto setSecondaryRomFd(s32 fd) -> void { lock_guard<recursive_mutex> lock(systemMutex); if (secondaryRomFd != -1) ::close(secondaryRomFd); secondaryRomFd = fd; }
  auto setRomPath(const char* path) -> void { lock_guard<recursive_mutex> lock(systemMutex); romPath = path ? (string)path : ""; }
  auto setSecondaryRomPath(const char* path) -> void { lock_guard<recursive_mutex> lock(systemMutex); secondaryRomPath = path ? (string)path : ""; }
  auto setTempFilePath(const char* path) -> void {
    tempFilePath = path ? (string)path : "";
    // Games load from where they are, so mia must not look beside them for saves (another emulator's .sav
    // would be imported) or write any there: Phobos keeps saves itself (importIntoPak, flushSavesToDisk).
    mia::setSaveLocation([] { return string{tempFilePath, "/mia-saves/"}; });
    // A parent set such as aleck64.zip that isn't beside the game: the app copies its Firmware pick here.
    mia::setParentLocation([] { return string{tempFilePath, "/"}; });
  }
  auto setLoadDiskImageToRam(bool enabled) -> void { /* Deprecated */ }

  auto setInput(f32 lx, f32 ly, f32 rx, f32 ry, s32 buttons) -> void {
      inputState.lx = lx;
      inputState.ly = ly;
      inputState.rx = rx;
      inputState.ry = ry;
      u32 previous = (u32)inputState.buttons.exchange(buttons);
      u32 rising = (u32)buttons & ~previous;
      if (rising && !isPausedAtomic.load(std::memory_order_relaxed)) {
          s64 now = steadyMs();
          for (; rising; rising &= rising - 1) {
              u32 bit = __builtin_ctz(rising);
              bitPressTimeMs[bit].store(now, std::memory_order_relaxed);
              bitPressCount[bit].fetch_add(1, std::memory_order_release);  // publishes the time
          }
      }

      // ZX gamepad control scheme: translate the gamepad bitmask into
      // keyboard keys for the active scheme (QAOP / ZXZX / ELITE). Updated
      // every setInput so the keyboard path sources scheme presses alongside
      // the on-screen keyboard. When zxStickToKeys is on, the left stick's
      // cardinal directions are OR'd in as D-pad bits first, so the stick
      // drives the SAME scheme keys as the D-pad (and reverse pitch applies
      // to both). inputState.buttons keeps the ORIGINAL mask — stick-derived
      // keys are scheme-only, never exposed to other systems.
      if (isZxKeyboardSystem(root ? root->name() : "")) {
          s32 scheme = zxControlScheme.load();
          u32 effButtons = (u32)buttons;
          if (zxStickToKeys.load()) {
              // Left stick cardinal -> D-pad bits (Up=1<<0, Down=1<<1,
              // Left=1<<2, Right=1<<3). Android AXIS_Y is negative when
              // pushed up; threshold matches the ~50% hysteresis press.
              if (ly < -0.5f) effButtons |= VirtualGamepad::Up;
              if (ly >  0.5f) effButtons |= VirtualGamepad::Down;
              if (lx < -0.5f) effButtons |= VirtualGamepad::Left;
              if (lx >  0.5f) effButtons |= VirtualGamepad::Right;
          }
          std::set<string> schemeKeys;
          if (scheme != 0) {
              // Scheme 4 = CUSTOM: the per-key rebind map IS the scheme (plus
              // Start->ENTER universal). Presets 1/2/3 ignore the map entirely
              // so QAOP/ZXZX/ELITE stay pristine — rebinds only take effect in
              // CUSTOM mode, keeping the state obvious from the scheme label.
              if (scheme == 4) {
                  std::map<string, u32> overrides;
                  {
                      std::lock_guard<std::mutex> lock(keyboardMutex);
                      overrides = zxKeyBindings;
                  }
                  for (u32 bit = 1; bit; bit <<= 1) {
                      if (!(effButtons & bit)) continue;
                      if (bit == VirtualGamepad::Start) { schemeKeys.insert("ENTER"); continue; }
                      for (auto& [key, bindBit] : overrides) {
                          if (bindBit == bit) { schemeKeys.insert(key); break; }
                      }
                  }
              } else {
                  for (u32 bit = 1; bit; bit <<= 1) {
                      if (!(effButtons & bit)) continue;
                      for (auto& key : zxSchemeKeys(bit, scheme)) schemeKeys.insert(key);
                  }
              }
          }
          {
              std::lock_guard<std::mutex> lock(keyboardMutex);
              zxSchemeKeysPressed.swap(schemeKeys);
          }
      }
  }

  // ZX scheme-translation toggles (Layer 2 — orthogonal to per-core rebinding).
  auto setZxStickToKeys(bool enabled) -> void {
      zxStickToKeys = enabled;
      LOGI("ZX stick-to-keys set to %d", (int)enabled);
  }

  auto setZxReversePitch(bool enabled) -> void {
      zxReversePitch = enabled;
      LOGI("ZX reverse pitch set to %d", (int)enabled);
  }

  // Per-key rebind: bind ZX keyboard key `label` to gamepad bit `bit`
  // (0 = clear). Takes effect immediately on the next setInput.
  auto setZxKeyBinding(const char* label, s32 bit) -> void {
      if (!label) return;
      std::lock_guard<std::mutex> lock(keyboardMutex);
      if (bit == 0) {
          zxKeyBindings.erase(label);
          LOGI("ZX key binding cleared: %s", label);
      } else {
          zxKeyBindings[label] = (u32)bit;
          LOGI("ZX key binding set: %s -> bit %d", label, (int)bit);
      }
  }

  // ZX gamepad control scheme setter (the ids zxControlScheme lists).
  auto setZxControlScheme(s32 scheme) -> void {
      zxControlScheme = scheme;
      LOGI("ZX control scheme set to %d", scheme);
  }

  // Neo Geo CD drive speed is fixed at 1x (authentic 75Hz CDD tick). The
  // CDD runs on the 68K's clock in this model, so the BIOS's access-machine
  // ($C0E99E) and DMA handlers must drain each sector in the ~80,000 68K
  // clocks between ticks. At >1x the BIOS can't finish a state transition
  // before the next CDD tick preempts it, and the access machine reads back
  // a CDC register that hasn't been written yet (DISC I/O ERROR ID=0002 on
  // the loader screen, ID=0000 elsewhere) — so the drive speed is fixed at
  // 1x. A real loader fast-forward would need 68K time-slicing (multiple
  // emulation frames per host frame), not a faster CDD tick.

  // Mute the ZX tape's Audio stream (the raw EAR waveform — the loud screech
  // while LOAD "" plays). The GAME still receives the EAR bit via
  // TapeDeck::read() (independent of the audio stream), so loading is
  // unaffected — only the speaker output is silenced. On unmute the stream
  // resumes normally. Find the tape node's child Audio::Stream and setMuted().
  // Sticky: remembers the desired state so it applies even when the tape node
  // doesn't exist yet (loadRom pushes this BEFORE connectDevices creates the
  // tape) — applyZxTapeMuted() re-applies it whenever a tape connects.
  static std::atomic<bool> zxTapeMutedState{false};
  auto applyZxTapeMuted() -> void {
    if (!root || !isZxKeyboardSystem(root->name())) return;
    auto tapes = root->find<Node::Tape>();
    if (tapes.empty()) return;
    auto tapeNode = tapes[0];
    auto streams = tapeNode->find<Node::Audio::Stream>();
    if (streams.empty()) return;
    streams[0]->setMuted(zxTapeMutedState.load());
  }
  auto setZxTapeMuted(bool muted) -> void {
    zxTapeMutedState = muted;
    applyZxTapeMuted();
    LOGI("ZXTape: audio %s", muted ? "muted" : "unmuted");
  }

  // Keyboard-based cores (ZX Spectrum, MSX, ColecoVision keypad): press or
  // release a key by its core input label (e.g. "J", "ENTER", "SPACE BREAK",
  // "RETURN", "F1 F6", "#"). AndroidPlatform::input() sources those buttons
  // from the set whenever the core polls its keyboard/keypad.
  auto setKeyboardKey(const char* label, bool pressed) -> void {
    if (!label) return;
    // Releases are always applied so a key can't stay latched across a reload.
    if (pressed && (!root || !isOnScreenKeyboardSystem(root->name()))) return;
    string key = label;
    std::lock_guard<std::mutex> lock(keyboardMutex);
    if (pressed) keyboardKeysPressed.insert(key);
    else keyboardKeysPressed.erase(key);
    keyboardKeyCount.store((u32)keyboardKeysPressed.size());
  }

  // Start playback of the ZX Spectrum tape (equivalent to ares desktop's
  // "Play Tape" button). Without this, Tape::read() returns 0 (stopped) and
  // LOAD "" never receives the tape signal.
  auto playTape() -> bool {
    std::lock_guard<std::recursive_mutex> lock(*runMutex);
    if (!root || !isZxKeyboardSystem(root->name())) return false;
    // Search the whole tree for the Tape node (the ZX tray nests it under
    // TapeDeck -> Tray; a direct find is more robust than assuming the path).
    auto tapes = root->find<Node::Tape>();
    if (tapes.empty()) { LOGI("ZXTape: no tape node found in tree"); return false; }
    auto tape = tapes[0];
    if (tape->length() == 0) { LOGI("ZXTape: tape empty (length=0)"); return false; }
    tape->setPosition(0);
    tape->play();
    ::ares::ZXSpectrum::tapeDeck.heldByUser = false;
    LOGI("ZXTape: playing (len=%llu)", (unsigned long long)tape->length());
    return true;
  }

  // The ZX Spectrum tape for the tape controls: whether one is in, whether it plays, and where it
  // stands and how long it runs, in milliseconds.
  auto getZxTapeState() -> std::array<s32, 4> {
    if (!root || !isZxKeyboardSystem(root->name())) return {};
    auto& tape = ::ares::ZXSpectrum::tapeDeck.tray.tape.node;
    if (!tape || tape->length() == 0) return {};
    u64 hz = std::max<u64>(tape->frequency(), 1);
    return {1, tape->playing() ? 1 : 0, (s32)(tape->position() * 1000 / hz), (s32)(tape->length() * 1000 / hz)};
  }

  // Plays the tape from where it stands, or from its start once it has run out, or stops it. The
  // automatic control leaves a tape the user stopped alone until the game moves on from that load.
  auto setZxTapePlaying(bool play) -> void {
    std::lock_guard<std::recursive_mutex> lock(*runMutex);
    if (!root || !isZxKeyboardSystem(root->name())) return;
    auto& deck = ::ares::ZXSpectrum::tapeDeck;
    auto& tape = deck.tray.tape.node;
    if (!tape || tape->length() == 0) return;
    if (play) {
      if (tape->position() >= tape->length()) tape->setPosition(0);
      deck.heldByUser = false;
      tape->play();
    } else {
      tape->stop();
      deck.heldByUser = true;
    }
    LOGI("ZXTape: %s at %llu of %llu", play ? "played" : "stopped",
         (unsigned long long)tape->position(), (unsigned long long)tape->length());
  }

  // Stops the tape at its start.
  auto rewindZxTape() -> void {
    std::lock_guard<std::recursive_mutex> lock(*runMutex);
    if (!root || !isZxKeyboardSystem(root->name())) return;
    auto& deck = ::ares::ZXSpectrum::tapeDeck;
    auto& tape = deck.tray.tape.node;
    if (!tape) return;
    tape->stop();
    tape->setPosition(0);
    deck.heldByUser = false;
    LOGI("ZXTape: rewound");
  }

  // The MSX tape for the tape controls, as getZxTapeState() gives the ZX Spectrum's, then whether it's
  // the data tape, whether recording is armed and whether it's recording. The motor relay plays and
  // stops it, so the controls only show it, wind it back, swap it and arm recording.
  auto getMsxTapeState() -> std::array<s32, 7> {
    if (!root || !root->name().beginsWith("MSX")) return {};
    auto& deck = ::ares::MSX::tapeDeck;
    auto& tape = deck.tray.tape.node;
    if (!tape || (tape->length() == 0 && !msxDataTapeIn)) return {};
    u64 hz = std::max<u64>(tape->frequency(), 1);
    return {1, tape->playing() ? 1 : 0, (s32)(tape->position() * 1000 / hz), (s32)(tape->length() * 1000 / hz),
            msxDataTapeIn ? 1 : 0, deck.recordArmed ? 1 : 0, tape->recording() ? 1 : 0};
  }

  // Puts the MSX's data tape in the deck, or takes it out for the game's own tape if the game has one.
  // The data tape is data-tape.wav in the game's save folder; the first one starts blank.
  auto setMsxTape(bool data) -> void {
    std::lock_guard<std::recursive_mutex> lock(*runMutex);
    if (!root || !root->name().beginsWith("MSX") || !savesPath || data == msxDataTapeIn) return;
    auto& deck = ::ares::MSX::tapeDeck;
    auto& port = deck.tray.port;
    if (!port) return;
    saveMsxDataTape();
    deck.recordArmed = false;
    if (port->connected()) {
      if (!msxDataTapeIn && deck.tray.tape.node) msxGameTapePosition = deck.tray.tape.node->position();
      port->disconnect();
    }
    msxDataTapeIn = false;
    if (data) {
      if (!msxDataTape) {
        string path = {gameSaveFolder(), "data-tape.wav"};
        // 4 x 44.1 kHz: a recording lands each edge the CPU writes on the tape's next sample, and at 44.1 kHz
        // that shifted the BIOS's 2400 Hz pulses by an eighth of their length, enough to misread a bit now and then.
        if (!file::exists(path)) Encode::WAV::mono<u16>(path, std::span<const u16>{}, 176400);
        auto medium = mia::Medium::create("MSX");
        if (medium && medium->load(path) == successful && medium->pak) {
          msxDataTape = medium;
          msxDataTapePath = path;
        } else {
          LOGE("MSXTape: couldn't load the data tape %s", (const char*)path);
        }
      }
      msxDataTapeIn = (bool)msxDataTape;
    }
    bool gameTape = currentMedium && currentMedium->pak && currentMedium->pak->attribute("tape").boolean();
    if ((msxDataTapeIn || gameTape) && port->allocate()) {
      port->connect();
      // A real MSX doesn't sound its cassette: the signal only reaches the PSG's port.
      if (auto& stream = deck.tray.tape.stream) stream->setMuted(true);
      // The game's tape goes back in where it was taken out, as a multi-load game left it.
      if (auto& tape = deck.tray.tape.node; tape && !msxDataTapeIn) tape->setPosition(std::min(msxGameTapePosition, tape->length()));
    }
    // A loader waiting with the motor on (and interrupts off) won't write port C again to start this tape.
    deck.motor(::ares::MSX::cpu.cassetteMotor());
    LOGI("MSXTape: %s in the deck", msxDataTapeIn ? "the data tape" : gameTape ? "the game's tape" : "no tape");
  }

  // Arms or disarms recording onto the data tape; while armed, the motor relay records instead of playing.
  auto setMsxTapeRecord(bool armed) -> void {
    std::lock_guard<std::recursive_mutex> lock(*runMutex);
    if (!root || !root->name().beginsWith("MSX")) return;
    auto& deck = ::ares::MSX::tapeDeck;
    deck.recordArmed = armed && msxDataTapeIn;
    if (!deck.recordArmed) {
      if (auto& tape = deck.tray.tape.node; tape && tape->recording()) tape->stop();
      saveMsxDataTape();
    }
    deck.motor(::ares::MSX::cpu.cassetteMotor());
    LOGI("MSXTape: recording %s", deck.recordArmed ? "armed" : "off");
  }

  auto rewindMsxTape() -> void {
    std::lock_guard<std::recursive_mutex> lock(*runMutex);
    if (!root || !root->name().beginsWith("MSX")) return;
    ::ares::MSX::tapeDeck.rewind();
    LOGI("MSXTape: rewound");
  }

  auto setNativeLibraryDir(const char* path) -> void { nativeLibraryDir = path ? (string)path : ""; LOGI("Native library dir set: %s", (const char*)nativeLibraryDir); }
  auto setFirmwarePath(const char* path) -> void { LOGI("Firmware path set: %s", path ? path : ""); }
  auto mapFirmwareFile(const char* name, const char* path) -> void { firmwareMap[name] = path ? (string)path : ""; LOGI("Firmware mapped: %s -> %s", name, (const char*)path); }
  auto missingFirmware(const char* system) -> std::vector<string> {
    std::vector<string> missing;
    string name = system ? system : "";
    if ((name == "Mega CD" || name == "Mega CD 32X") && !hasMegaCDBios()) missing.push_back("fw_mcd");
    if (name == "Super Game Boy" && !hasSuperGameBoyCart()) missing.push_back("fw_sgb");
    if (name == "PC Engine CD" && !hasPCEngineCDBios()) missing.push_back("fw_pce_cd");
    if (name == "Mega LD" && !hasLaserActiveSegaBios()) missing.push_back("fw_laseractive_sega");
    if (name == "PC Engine LD" && !hasLaserActiveNecBios()) missing.push_back("fw_laseractive_nec");
    return missing;
  }
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
  auto setMemoryCardKey(const char* key) -> void {
    ps1MemoryCardKey = key ? (string)key : "";
  }
  auto setPspMemoryStickPath(const char* path) -> void {
    pspMemoryStickPath = path ? (string)path : "";
  }
  auto setVulkanCachePath(const char* path) -> void {
    vulkanCachePath = path ? (string)path : "";
    LOGI("Vulkan cache path set: %s", (const char*)vulkanCachePath);
  }

  auto loadSecondaryRom(const char* systemNamePtr, const char* uriPtr) -> bool {
    string systemName = systemNamePtr;
    string uri = uriPtr;
    lock_guard<std::recursive_mutex> lock(systemMutex);
    LOGI("Loading secondary medium: %s, uri: %s", (const char*)systemName, (const char*)uri);

    string loadPath = secondaryRomPath;
    secondaryRomPath = "";
    if (!loadPath && secondaryRomFd == -1) return false;

    string extension = "bin";
    if(auto position = uri.findPrevious(uri.size(), ".")) {
        extension = uri.slice(*position + 1).downcase();
    }

    if (!tempFilePath) return false;
    if (!loadPath) {
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
      loadPath = tempPath;
      rememberTempCopy(tempPath);
    } else {
      LOGI("Loading secondary medium in place: %s", (const char*)loadPath);
    }

    // The .ndd/.d64/.n64dd disk images are a separate MIA medium type that
    // exposes program.disk for the 64DD drive; plain "Nintendo 64" would
    // treat the file as a cartridge ROM.
    string mediumName = systemName;
    if (systemName == "Nintendo 64" && (extension == "ndd" || extension == "d64" || extension == "n64dd")) {
        mediumName = "Nintendo 64DD";
    }
    secondaryMedium = mia::Medium::create(mediumName);
    if (!secondaryMedium) {
        LOGE("MIA: Failed to create secondary medium for %s", (const char*)mediumName);
        return false;
    }

    auto loadResult = secondaryMedium->load(loadPath);
    if (loadResult != successful) {
        LOGE("MIA: Failed to load secondary medium for %s (Result: %d)", (const char*)mediumName, (s32)loadResult.result);
        return false;
    }

    #if defined(CORE_N64)
    // N64DD: mounting a disk requires the system to be a 64DD variant — that
    // is what creates the "Nintendo 64DD" node with its "Disk Drive" port.
    // A plain "Nintendo 64" system has no drive, so the .ndd can't attach.
    // Reload the system as 64DD (keeping the cartridge medium) so the cart +
    // expansion disk boot together, mirroring desktop ares (load game, then
    // pick disk). The emulation thread is paused here (menu), so resetting
    // root while it sleeps in the pause branch is safe.
    if (root && root->name() == "Nintendo 64" && mediumName == "Nintendo 64DD") {
        LOGI("N64DD: reloading system as Nintendo 64DD to mount disk");
        // Block setEmulationRunning(true) while we tear down the old system
        // below (a fresh thread would grab the OLD root and run CPU::LW against
        // the freed singleton hardware). Cleared before the thread restart.
        systemUnloading.store(true);
        // Stop the emulation thread BEFORE touching root, and WAIT for it to
        // fully exit. The emu thread may be stuck inside root->run() (not
        // sleeping in the pause branch) — nulling root then spawns a new
        // thread that races the old thread's teardown (CPU::LW / RSP DMA
        // SIGSEGV on freed cartridge/RDRAM). We must not flip
        // emulationRunning back on until the old thread has confirmed exit,
        // or it resumes with its stale localRoot.
        isPausedAtomic = true;
        fastForwardAtomic = false;
        setEmulationRunning(false);
        bool joined = false;
        if (emuThread && emuThreadRunning) {
            // Wait for the old thread to exit (bounded: it may be stuck).
            for (int i = 0; i < 200 && emuThreadRunning.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!emuThreadRunning.load()) {
                pthread_join(emuThread, nullptr);
                emuThread = 0;
                joined = true;
            }
        } else {
            emuThread = 0;
            joined = true;
        }
        if (!joined) {
            LOGW("N64DD: emulation thread stuck inside a frame (>2s); abandoning the old system and aborting the disk mount");
            // The zombie is still executing on the N64 singleton hardware
            // (rdram.ram / cartridge.rom / dd.disk). Do NOT proceed to
            // ::ares::Nintendo64::load() here — System::load → System::unload
            // would free those buffers underneath the running zombie → SIGSEGV
            // in CPU::LW (the "crash while unloading" bug). Same abandon
            // semantics as unloadSystem(): drop the global root (the zombie's
            // localRoot keeps the node tree alive), leak the old runMutex (the
            // zombie holds it), bump the generation so the zombie exits at its
            // loop-top check once its frame completes, and PARK the handle so
            // the next N64 load joins it before re-initializing the singletons.
            root = {};
            runMutex = new std::recursive_mutex();
            emuThreadGeneration.fetch_add(1);
            parkZombieThread();
            systemUnloading.store(false);
            return false;
        }
        // Old thread is fully dead — safe to tear down root. Also join any
        // OLDER parked zombies before re-initializing the singleton hardware.
        root = {};
        joinAbandonedThreads();
        // The old system's audio streams are dead now — clear the registry so
        // the fresh 64DD system's streams register cleanly (otherwise the new
        // system mixes with stale dead streams → no sound after the reload).
        {
            std::lock_guard<std::mutex> lock(audioStreamsMutex);
            audioStreams.clear();
            audioStreamsVersion.fetch_add(1, std::memory_order_release);
        }
        ::ares::Nintendo64::vulkan.enable = true;  // settings persist from cart load
        ::ares::Nintendo64::vulkan.frontendPresentsScanout = true;
        const char* regionString = [&]() -> const char* {
            // No PAL 64DD exists; every non-NTSC-U preference uses NTSC-J.
            if (regionPreference == 2 || regionPreference == 3 ||
                regionPreference == 4 || regionPreference == 5) {
                return "[Nintendo] Nintendo 64DD (NTSC-J)";
            }
            return "[Nintendo] Nintendo 64DD (NTSC-U)";
        }();
        bool ok = ::ares::Nintendo64::load(root, regionString);
        if (ok && root) {
            ::ares::Nintendo64::option("Recompiler", n64Recompiler ? "true" : "false");
            // Re-import the cartridge + disk + RTC saves for the fresh 64DD
            // node. Per-ROM dir (same key as flushSavesToDisk). Must run
            // BEFORE connectDevices so the core reads the restored saves at
            // port connect time (and so the system pak's time.rtc is present
            // when DD::load() reads it).
            if (savesPath) {
                string romKey = currentRomBase;
                romKey.replace("/", "_"); romKey.replace("\\", "_"); romKey.replace(":", "_");
                if (romKey.size() == 0) romKey = "rom";
                string saveDir = {savesPath, "/Nintendo 64/", romKey, "/"};
                directory::create(saveDir);
                auto importPak = [&](auto& pak) -> void {
                  for (auto& saveNode : pak->files()) {
                    string fileName = saveNode->name();
                    if (!fileName.endsWith(".ram") && !fileName.endsWith(".srm") &&
                        !fileName.endsWith(".eeprom") && !fileName.endsWith(".card") &&
                        !fileName.endsWith(".sav") && !fileName.endsWith(".fla") &&
                        !fileName.endsWith(".flash") && !fileName.endsWith(".rtc") &&
                        !fileName.endsWith(".disk") && !fileName.endsWith(".disk.error") &&
                        !fileName.endsWith(".cartrom")) continue;
                    string path = {saveDir, fileName};
                    auto data = nall::file::read(path);
                    if (!data.empty()) {
                      if (saveNode->size() != data.size()) saveNode->resize(data.size());
                      saveNode->seek(0);
                      saveNode->write(data.data(), data.size());
                      LOGI("Saves: imported save for %s", (const char*)fileName);
                    }
                  }
                };
                if (currentMedium && currentMedium->pak) importPak(currentMedium->pak);
                if (secondaryMedium && secondaryMedium->pak) importPak(secondaryMedium->pak);
                if (root) {
                  auto sysPak = root->pak();
                  if (sysPak) importPak(sysPak);
                }
            }
            // [Phobos] Attach the cartridge + disk BEFORE powering on. Desktop
            // ares loads the .z64 and .ndd TOGETHER, so the IPL/boot sees both
            // from the start. Powering on first (old order) booted the 64DD IPL
            // with NO cartridge attached → F-Zero X's "cannot play with this
            // disk alone" check failed on reload.
            connectDevices(root);  // attaches Cartridge + mounts .ndd (Disk Drive)
            root->power();
            // Restart the emulation thread for the new 64DD system (we stopped
            // it above so it wouldn't race the teardown). The teardown is done,
            // so re-allow thread spawns.
            systemUnloading.store(false);
            setEmulationRunning(true);
            LOGI("N64DD: system reloaded with disk drive");
            return true;
        }
        systemUnloading.store(false);
        LOGE("N64DD: failed to reload system as 64DD");
        return false;
    }
    #endif

    // PS1 multi-disc swap: the Disc Tray is hot-swappable (ares mounts on
    // tray->connect()). Replace currentMedium with the newly-loaded disc and
    // re-connect the tray — the core re-reads cd.rom + TOC from the new pak.
    // No full reload — the console stays running and the game sees the new
    // disc (multi-disc games poll for a change). This is the fix for the old
    // "Change Disc" button, which only ran connectDevices() and never mounted
    // the new disc.
    if (root && systemName == "PlayStation") {
        // NOTE: use scan(), NOT find() — find<T>(name) only searches DIRECT
        // children, and the PS1 Disc Tray is nested at root → PlayStation →
        // Disc Tray. find() returned null → "Disc Tray not found" on every
        // swap (MGS disc change). scan() recurses the whole tree.
        auto discTray = root->scan<Node::Port>("Disc Tray");
        if (discTray) {
            // A state load can swap discs mid-game, so the frame in progress finishes first; the pause menu keeps
            // the game paused afterwards.
            bool wasPaused = isPausedAtomic.exchange(true);
            lock_guard<recursive_mutex> frame(*runMutex);
            currentMedium = secondaryMedium;
            fastForwardAtomic = false;
            // Disconnect (ejects the current disc), then re-allocate + connect
            // so the core re-reads cd.rom + TOC from the new medium's pak.
            // Disc::connect() expects cd (the peripheral) to exist, so allocate
            // recreates it before connect.
            discTray->disconnect();
            discTray->allocate("PlayStation Disc");
            discTray->connect();
            isPausedAtomic = wasPaused;
            LOGI("PS1: disc swapped to %s", (const char*)secondaryMedium->name());
            return true;
        }
        LOGE("PS1: Disc Tray not found — cannot swap");
        return false;
    }

    if (root) {
        connectDevices(root); // Refresh ports to attach new medium
        LOGI("Secondary medium loaded successfully");
        return true;
    }
    return false;
  }

  auto laserdiscSides() -> std::vector<string> {
    lock_guard<std::recursive_mutex> lock(systemMutex);
    std::vector<string> sides;
    if (!root || !root->attribute("configuration").find("LaserActive")) return sides;
    if (!currentMedium || !currentMedium->pak) return sides;
    string media = currentMedium->pak->attribute("medium");
    for (auto& side : nall::split_and_strip(media, ",")) {
      if (side) sides.push_back(side);
    }
    return sides;
  }

  // As ares desktop's Change Side: the tray changes at once, since on a LaserActive the BIOS opens the tray
  // and closes it again itself.
  auto setLaserdiscSide(const char* side) -> bool {
    lock_guard<std::recursive_mutex> lock(systemMutex);
    if (!root || !root->attribute("configuration").find("LaserActive")) return false;
    auto tray = root->scan<Node::Port>("Disc Tray");
    if (!tray) return false;
    bool wasPaused = isPausedAtomic.exchange(true);
    lock_guard<recursive_mutex> frame(*runMutex);
    tray->disconnect();
    if (side && *side) {
      tray->allocate(side);
      tray->connect();
    }
    isPausedAtomic = wasPaused;
    LOGI("LaserActive: %s", side && *side ? side : "disc taken out");
    return true;
  }

#if defined(__ANDROID__)
  auto setSurface(JNIEnv* env, jobject surface) -> void {
    lock_guard<std::mutex> lock(windowMutex);
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    LOGI("PhobosSurface: setSurface called. New=%p", window);
    phobos::host::setWindow(window);
    windowChanged = true;
  }
#endif
  auto getNewLogs() -> std::vector<LogEntry> {
    lock_guard<mutex> lock(logMutex);
    std::vector<LogEntry> logs;
    logs.reserve(logBuffer.size());
    for (auto& entry : logBuffer) logs.push_back(std::move(entry));
    logBuffer.clear();
    return logs;
  }
  auto isFirstFrameRendered() -> bool { return firstFrameRendered.load(); }
  auto getVideoGeometry() -> VideoGeometry {
    return {videoDisplayWidth.load(std::memory_order_relaxed), videoDisplayHeight.load(std::memory_order_relaxed)};
  }
  auto getRefreshRateHint() -> f64 {
    return refreshRateAtomic.load();
  }
  auto getPerformanceStats() -> PerformanceStats {
    PerformanceStats stats;
    stats.fps = currentFps.load();
    stats.frameTime = avgFrameTime.load() / 1000.0;
    // The emulation thread's core; the caller runs on a UI worker thread.
    stats.activeCore = emuThreadCore.load(std::memory_order_relaxed);
    stats.emuTid = emuThreadTid.load(std::memory_order_relaxed);
    stats.targetFps = refreshRateAtomic.load();
    stats.playTimeMs = (s64)(playedMicros.load(std::memory_order_relaxed) / 1000);
    #if defined(CORE_N64)
    stats.pipelineFailures = ::ares::Nintendo64::Vulkan::pipelineFailureCount.load(std::memory_order_relaxed);
    stats.isAdrenoDriver = (bool)::ares::Nintendo64::Vulkan::gpuDeviceName.find("Adreno");
    #else
    stats.pipelineFailures = 0;
    stats.isAdrenoDriver = false;
    #endif
    return stats;
  }
  auto getFrameTimes(f32* out, u32 capacity) -> u32 {
    u32 end = frameIntervalCursor.load(std::memory_order_acquire);
    u32 count = std::min<u32>(std::min<u32>(end, FrameIntervalHistory), capacity);
    for (u32 i = 0; i < count; i++) {
      out[i] = frameIntervals[(end - count + i) % FrameIntervalHistory].load(std::memory_order_relaxed);
    }
    return count;
  }
  auto takeScreenshot(const char* path) -> bool {
    #if defined(CORE_N64)
    // N64 Vulkan frames are presented straight from the scanout buffer and never
    // pass through lastFrameBuffer, so read the retained scanout (valid while
    // paused) and convert RGBA bytes to the ARGB the PNG encoder expects.
    if (isN64VulkanSession() && !::ares::Nintendo64::vi.io.cpuScanoutActive) {
      std::vector<u32> pixels;
      u32 width = 0, height = 0;
      if (!::ares::Nintendo64::vulkan.readScanout(pixels, width, height)) return false;
      for (auto& p : pixels) p = 0xFF000000 | ((p >> 16) & 0x000000FF) | (p & 0x0000FF00) | ((p << 16) & 0x00FF0000);
      return nall::Encode::PNG::RGBA8(path, pixels.data(), (s32)width * 4, (s32)width, (s32)height);
    }
    #endif
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
