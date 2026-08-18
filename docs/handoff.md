# Phobos — Android Multi-System Emulator (ares fork) — HANDOFF

**Phobos** is an Android N64-first multi-system emulator (package `com.phobos.emulator`, module `:app`, native lib `libphobos_android.so`).
Native core is a heavily customized fork of **ares** (JIT recompilers, parallel-RDP Vulkan renderer, libadrenotools Turnip driver). UI is Jetpack Compose.

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

## Open Priority Queue
- **#9:** Ape Escape cinematic skip (CD-XA).
- **#10c/10a:** PCE/CD + Neo Geo MVS/AES joint investigation (ARM64/libco coroutine black screen / gated).
- **#46:** Audio pops residual.
- **#49:** N64 save import/export UI (.sra/.eep/.fla/.mpk).
- **#52:** PS1 multi-disc swap verify (MGS disc 2 swap fix applied via recursive `scan`).
- **#61:** Proper release APK signing (keystore in GH Actions).
- **Other:** Task 13c (Multi-controller), Task 42 (Perf Monitor settings), Task 51 (ZX multi-tape swap).

## Working Agreements & Policies
- **Measure first, then fix** — never blind-patch the recompiler.
- **Accuracy vs. Speed** — accuracy is preferred, but never below full 60 FPS (raise `JitInterleaving` / perf knobs if below floor).
- **Commit + push on every good result** — checkpoint working states immediately (`git add -u`, descriptive commit, push).
- **Auto-deploy after every build** — immediately deploy to device after `assembleDebug`.
- **UI Toggles** — use `rememberUpdatedState` for state params inside `pointerInput` gesture handlers.
- **Vulkan Mutex** — the abandon path must NEVER take `vulkan.mutex` (zombie deadlock).
