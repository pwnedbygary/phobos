# Phobos — Android Multi-System Emulator (ares fork) — HANDOFF

**Phobos** is an Android N64-first multi-system emulator (package `com.phobos.emulator`, module `:app`, native lib `libphobos_android.so`).
Native core is a heavily customized fork of **ares** (JIT recompilers, parallel-RDP Vulkan renderer, libadrenotools Turnip driver). UI is Jetpack Compose.

## 🚀 CURRENT STATUS (2026-08-18)

**Latest work:** Task #10c ZX Spectrum 128K FIXED & VERIFIED on-device (commit `1bf97e3ac`). Root cause (proven on-device via temporary ZX128Diag instrumentation, since removed): the 128K core names its system node "ZX Spectrum 128", but `PhobosRunner::pak()`'s tape branch matched `root->name() == "ZX Spectrum"` → empty pak for 128K loads → `Tape::load()` read frequency 0 → cubic resampler ratio 0 → infinite loop in `Cubic::write` on the tape thread's first frame → scheduler wedged, zero frames. Fix: `root->name().beginsWith("ZX Spectrum")`. Verified: Enduro Racer (128K) 50.8 FPS stable (PAL 50Hz), audio ring healthy; Elite 48K regression 50.6 FPS; ZX→SFC reload 60.2 FPS. Prior work same day: Task #10c PCE family FIXED (commit `2795e5813`) — missing PROFILE_PERFORMANCE define compiled out `PSG::main()` → scheduler deadlock on first timer sync; Final Lap Twin 60 FPS, SuperGrafx 60 FPS, Rondo of Blood 60 FPS, SFC/PS1/MD regression 59.9-60.2 FPS.

**Neo Geo (Task #10c, UNCOMMITTED state):** Un-gated for diagnosis via `if (false && identifiedSystem == "Neo Geo")` in PhobosRunner.cpp. kof2003.zip loads (MIA: AES; core reports "Neo Geo MVS", MVS BIOS sp-e.sp1 attached; "VFS: Failed to attach static.rom" = benign warning), **runs 59.2-60.1 FPS sustained** (old black-screen/0-FPS hang GONE). Streams registered: FM (ch=2, 500kHz) + SSG (ch=1, 500kHz). **BUT audio ring stays 0/12000 (0%) and no sound** — multi-stream lockstep (`PhobosRunner.cpp` audio() ~1384-1417: emit only when EVERY stream pending, bounded 8192) or mute path suspect. Video presentation unverified — every adb loader load ran HEADLESS (no navigation → no SurfaceView). FIXES LANDED UNCOMMITTED: (1) debug loader now navigates to the emulator screen (mirrors SystemDetailScreen flow); (2) swap-screen feature (below). After build+deploy: verify NG video via loader, then investigate the 0% audio ring, then finalize gate state + verify MVS/AES + commit.

**Swap-Screen feature (VERIFIED 2026-08-18 on-device, commit pending):** Hotkey to leave a running game to the library/console/settings — emulation PAUSES on leave, UNPAUSES on return; the game is unloaded ONLY via the quit dialog. Implementation: `MainViewModel.navigateTo(route)` SharedFlow navEvents + `emulatorScreenVisible` StateFlow + `swapToLibrary()`/`swapBackToGame()`; `MainScaffold` collects navEvents (special-case library/console/settings with popUpTo(start)+launchSingleTop); EmulatorScreen hotkey `"library"` → swapToLibrary, pause menu gains "Library" button, `onDispose` NO LONGER unloads (kept GameInputState.reset + emulatorScreenVisible=false), quit dialog Quit button calls `unloadSystem()` then onBack; MainActivity debug loader NOW navigates to the emulator screen after loadRom (mirrors SystemDetailScreen load-then-navigate — fixes ALL previous adb loads running headless behind the library); `SettingsStore` defaults mirrored from the developer's device config (Z-button based: 101=Z + A/B/X/Y/L1/R1/L2/R2/DPAD/SELECT/START/C etc.; library = Z+C = [101,98]; ff_hold unbound; analog_toggle = C alone [98]); HotkeyMappingScreen label "Swap to Library". KEY INSIGHT: the content-view OnKeyListener only fires when NO focused view consumes the key — on the library screen Compose absorbs them — so the RETURN hotkey is intercepted at window level via a `Window.Callback` wrapper (ComponentActivity restricts overriding dispatchKeyEvent itself). Swap away+back verified round-trip on-device; quit dialog unload verified. Testing caveat: adb `input keyevent` cannot hold combos — hotkeys must be tested with the real controller.

**All cores verified WORKING** except gated broken ones (Neo Geo MVS/AES — see above).

**Next priority:** finish + verify swap-screen & Neo Geo (Task #10c), then Task #49 (N64 save import/export UI).

## NEO GEO CD (future task — reference)
ares has a dedicated Neo Geo CD fork branch by Luke Usher: `https://github.com/ares-emulator/ares/tree/neogeo-cd`. Closely linked hardware to AES/MVS — once AES/MVS work, pull ONLY the files needed for the NG CD core (CD-ROM hardware + CD audio paths; do NOT pull the whole branch) and port them into `ares/`. Track as a new task after Task #10c lands.

## ALWAYS-READ REFERENCES (for details not covered here)
- **Implementation plan (DEEP reference — task queue + full notes):** `/home/garyb/LLM-Projects/phobos/docs/implementation-plan.md` — also at `.cache/.../implementation_plan.artifact.md` (same content; the in-repo copy is authoritative). Priority queue = open only; ✅ Complete section = archived write-ups; IN FLIGHT = detailed status.
- **README.md** (repo root) — full systems matrix, feature list, build requirements.
- **Prior huge conversation (grep for context):** `/home/garyb/Desktop/agent-mode-conversation.json`.

## Repo Layout & Key Files
- `/home/garyb/LLM-Projects/phobos/android` — Android app Gradle project.
  - `app/src/main/cpp/PhobosRunner.cpp` — Platform bridge: emulation thread, AndroidPlatform (input/video/audio/pak), save import/export, settings sync.
  - `PhobosRunner.hpp`, `PhobosJNI.cpp`, `PhobosCore.kt`, `MainViewModel.kt`, `EmulatorScreen.kt`, `SettingsStore.kt`, `PathSettingsScreen.kt`, `MainActivity.kt`, `TouchControls.kt`, `GameInputState.kt`.
- `/home/garyb/LLM-Projects/phobos/ares` — Native core fork (N64 in `ares/n64/`, parallel-RDP Vulkan in `n64/vulkan/`).
- `/home/garyb/LLM-Projects/phobos/thirdparty/` — parallel-rdp, sljit, adrenotools, volk, etc.

## Build / Deploy / Test
- Build: `gradle app:assembleDebug`. Deploy: IDE deploy module `:app`, `DEFAULT_ACTIVITY`, RUN.
- Device: serial **49016109** (Retroid Pocket 6, Adreno 740, custom Turnip driver at `files/gpu_drivers/libvulkan_freedreno.so`).
- Logcat tags: `Phobos`, `PhobosCore`, `PhobosJNI`, `PhobosVulkan`, `Granite`, `hook_impl`, `vulkan`.

## Verified Major Features & Recent Fixes
1. **N64DD & 64DD Save/RTC Persistence:** Fully working (`fw_n64dd_jp/us`, disk mount, Error 48 RTC seed fix, `program.disk` persistence, base cart reload cleanup).
2. **N64 Rumble Pak & Controller Pak:** Player 1 support, rumble envelope decay per hit (`MainViewModel` / `PhobosRunner`).
3. **GBA RTC (Task 38):** Pokémon Unbound RTC fixed via broadened MIA detection (`RTC_V001`) and pre-connect save import.
4. **PS1 DualShock Analog Toggle (Task 57):** Input cache mutex protection + correct state return for pause menu.
5. **ZX Spectrum 48K:** Fully functional (BIOS, authentic on-screen keyboard, TZX/TAP tape playback, custom rebinds, signed-char fix).
6. **Multi-Stream Audio Mixer (Task 23):** Lockstep mixer supporting multi-stream cores (MD/MCD, ZX, MSX, PCE-CD).
7. **Auto Save-State Slot (Task 17):** Auto-save on quit / auto-load on boot, integrated in pause menu and cycler.
8. **Rogue Squadron & Conker/MT:** Render stability via parallel-RDP scanout race fix and field-toggle correctives.
9. **PS1 CD-DA Audio Pops (Task 46 — 2026-08-18):** Fixed via fade envelope on playback state transitions. Root cause: disc drive state changes (reading/playingCDDA) would snap samples from music→0, creating hard clicks. Solution: 150-sample linear fade (3.4ms @ 44.1kHz) applied in `cdda.cpp:clockSample()` when entering/exiting CD-DA playback. Eliminates pops without audio artifacts. Commit `1822c5757`.
10. **Ape Escape cinematics (Task 9 — 2026-08-18):** Fixed XA filter routing. Ape Escape's `TITLE.STR` interleaves `subMode=0x48` channel-0 MDEC video/data sectors with `subMode=0x64` channel-1 XA audio sectors. Phobos applied the configured file/channel filter to both, dropping the video sectors before the CD FIFO. Filtering now applies only to XA audio sectors (`subMode & 0x44`), allowing video data to reach MDEC. Verified on Retroid Pocket 6 with a clean build and cold USA-region launch; intro plays and reaches the menu normally.

## Open Priority Queue
- **Neo Geo MVS/AES — finish the core FIRST (gating everything below):** graphics FIXED (vscale/`loadZoomy` + vflip/zoom decode, KOF2003 verified at 59.2 FPS); P1/P2 input mirror FIXED (player-aware binding, `PhobosRunner.cpp`); **Audio 0% still OPEN** (follow-up bug, tackle right after video); then broad game-compat pass (Matrimelee/SamSho/etc.). PCEngineCD deferred (doc-only bug) until NG fully stable.
- **PER-CORE + PER-GAME CONTROLLER REMAPPING — NEXT PRIORITY (right after NG stable):** Tasks **13a** (per-core custom controller layouts), **13b** (per-core rebinding), **13c** (multi-controller / per-player pad assignment), **13d** (per-game rebinding). All four done together (same code path). 3-tier hierarchy like RetroArch — **Global** default → **Per-Core** override → **Per-Game** override (top priority wins). Pulled up from the QoL phase: Neo Geo C/D currently land on R1/R2 (Genesis-heritage bit mapping in `resolveButtonBit`); per-core defaults + user rebinding (and per-game overrides for weird default control schemes) is the proper fix.
- **#72:** N64 RDP-ParaLLEl perf investigation vs pwnedbygary/mupen64plus-ae-turnip (some games far faster there — port applicable fast paths).
- **#49:** N64 save import/export UI (.sra/.eep/.fla/.mpk).
- **#52:** PS1 multi-disc swap verify (MGS disc 2 swap fix applied via recursive `scan`).
- **#61:** Proper release APK signing (keystore in GH Actions).
- **Other:** Task 42 (Perf Monitor settings), Task 51 (ZX multi-tape swap).

## Working Agreements & Policies

- **Measure first, then fix** — never blind-patch the recompiler.
- **Accuracy vs. Speed** — accuracy is preferred, but never below full 60 FPS (raise `JitInterleaving` / perf knobs if below floor).
- **Commit + push on every good result** — checkpoint working states immediately (`git add -u`, descriptive commit, push).
- **Auto-deploy after every build** — immediately deploy to device after `assembleDebug`.
- **UI Toggles** — use `rememberUpdatedState` for state params inside `pointerInput` gesture handlers.
- **Vulkan Mutex** — the abandon path must NEVER take `vulkan.mutex` (zombie deadlock).
- **Documentation** — update BOTH handoff.md AND implementation-plan.md on every completed task before moving to next one.

## Practical Build & Debug Tips

**Gradle Performance (if VS Code CPU spiking to 100%):**
- Add to `android/gradle.properties`:
  ```properties
  org.gradle.workers.max=2
  org.gradle.jvmargs=-Xmx1024m
  android.enableBuildCache=true
  ```
- Only compile one variant: `./gradlew app:assembleModernDebug` (not full `assembleDebug`)
- GPU acceleration: Not practical for Gradle/C++ compilation (CPU-bound)

**ADB from VS Code terminal:**
- Device: `adb devices` — should show **49016109** (Retroid Pocket 6)
- Install: `adb install -r android/app/build/outputs/apk/modern/debug/app-modern-debug.apk`
- Launch: `adb shell am start -n com.phobos.emulator/.MainActivity`
- Logcat: `timeout 60 adb logcat | grep Phobos` (or use `-c` to clear first)
- **Debug ROM loader (no UI automation needed):** MainActivity reads intent extras
  `load_uri` / `load_name` / `load_system` (file:// URIs work; quote the whole `am start`
  command so the device shell sees the double quotes — filenames contain parens).
  ALWAYS add `--activity-single-top` for repeat loads (plain `am start` on the running
  instance silently drops onNewIntent on this device). Example:
  ```
  adb shell 'am start --activity-single-top -n com.phobos.emulator/.MainActivity --es load_uri "file:///storage/EBFF-F6C0/ROMs/tg16/Final Lap Twin (USA).zip" --es load_name "Final Lap Twin (USA).zip" --es load_system "PC Engine"'
  ```

**Crash debugging:**
- Quick check: `adb logcat | grep -E "SIGSEGV|crash|FATAL"`
- Full trace: `debuggerd -b <PID>` (watchdog auto-dumps on PhobosRunner hang, ~6s timeout)
- Verify: audio ring buffer health in logcat: `AudioDiag: ring=XXXX/12000 (XX%) xruns+0`
