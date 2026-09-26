# Phobos — Android Multi-System Emulator (ares fork) — HANDOFF

## Resume with another LLM — start here

Repository: https://github.com/pwnedbygary/phobos, branch `master`.
Use the portable startup prompt in [coordinator-prompt.md](coordinator-prompt.md).
No prior chat history or access to Replit is required to read this handoff.
Verify the checkout, tools and device access rather than assuming this environment.

Read in order:
1. [Development process](development-process.md): independent review before every authored commit.
2. [Implementation plan](implementation-plan.md): authoritative current priorities and task dispositions.
3. [Performance audit](performance-audit.md) and [benchmark protocol](mario-tennis-benchmark.md).
4. [Implementation history](implementation-history.md) only for relevant historical details.

Next action: establish matched Phobos/v336 APK, device, driver, settings and scene
identities, then collect baseline traces. No optimization or performance parity
has been demonstrated. Do not resume the cancelled signing or audio-buffer tasks
without new authorization. Preserve saves and do not assume APK update compatibility.

Publication preparation: the plan reconciliation received independent PASS,
including byte-for-byte preservation of the original plan in its archival appendix.
This follow-up corrects cancelled signing-task status and adds this portable entry
point, and restores tracking of the existing `.gitmodules` file for fresh clones.
Documentation and Git-metadata checks/review accompany the commit; no APK/device test
is implied. Verify GitHub's branch tip against local HEAD after publication.

**Active work (2026-09-25):** branch `feature/n64-accuracy-neutral-perf-2026-09`
([PR #4](https://github.com/pwnedbygary/phobos/pull/4), commits `c02166932` and `ffe337ceb`).
Stacked on it: branch `feature/perf-hud-2026-09` (its own PR, based on PR #4's branch) with
the MangoHud-style performance HUD (builds, 50 host tests pass, verified on the RP6; see plan
tasks 42–44/69).
Accuracy-neutral: cross-section `J` and not-taken-edge (`LinkSlot`) linking with runtime
PC/live-state-key gates, RSP pipeline hash-skip, `Screen::frame` CV. Opt-in N64
Experimental speed hacks (faster CPU sync, skip cache timing, RSP task mode; default
off). Measured one at a time on the RP6: no clear FPS gain in Mario vs Boo, and Faster
CPU sync stalled Conker's pub for 91.5 s at a time — a lost CP0 timer interrupt (Count only
advanced at sync; Conker's ~25 µs timer landed behind Count), root-caused on device and
fixed (accurate between-sync Count reads, wrap-safe timer check). Conker with Faster CPU
sync then ran 280 s stall-free; at default settings the fix measured Mario vs Boo 58.5 /
58.8 FPS and the smoke titles ran clean. Touch: seamless D-pad diagonals; N64 L large / Z pills both
sides in landscape (right Z hidden in portrait by default). Mario vs Boo final snapshot 55.6 / 58.3 FPS mean over two runs (RP6 run-to-run
variance); smoke OK on Mario Tennis, Mischief Makers, F-Zero X, Paper Mario, OoT, Conker.
All RP6 runs that day had Asynchronous RDP **on** (the persisted setting); earlier notes
and the commit message said off. Host unit tests 41/41. Bugbot: three high findings
fixed, final pass clean. See
[performance audit](performance-audit.md#2026-09-25-follow-up-accuracy-neutral-backlog--experimental-speed-hacks).

Earlier linking: branch `feature/n64-block-linking-2026-09` merged. Same-section
unconditional `J` linking measured on RP6 ~59.8 FPS / ~109% emu-thread CPU.

Earlier: branch `feature/touch-controls-perf-2026-09` (merged PR #2). On top of the
2026-09-24 change set (touch controls, performance, Asynchronous RDP, Rogue Squadron
hold, save-state previews), a second commit made N64 emulation substantially cheaper —
Mario Tennis gameplay 50.6 → 58.4 FPS average on the Retroid Pocket 6 (Mario vs Boo
save state, 30 s × 2, Asynchronous RDP on; default is off) — and fixed the squashed
N64/PS1 picture and save-state previews; the tap-to-show top bar is replaced by the
on-screen menu button at the user's request ([touch controls](touch-controls.md)).
Local builds for performance numbers must use NDK 28.2 (the CI toolchain).

## Touch controls overhaul and performance scan — 2026-09-24 (in progress)

Status: **implemented on branch `feature/touch-controls-perf-2026-09` (base
`6acee27cb`); compiled for both flavors and host unit tests pass (see "Checks run");
not yet run on a device.** Committed only after independent review of the frozen
snapshot, per the development process. Device checks are the remaining step.

User requests (2026-09-24, Cursor session):
1. Scan the whole codebase for performance gains in every core, keeping accuracy
   (ares' cycle-accuracy philosophy) first; the Mupen64Plus parallel-RDP gap was the
   trigger. Prior analysis: [performance audit](performance-audit.md) and the archived
   2026-08-20 comparison in [implementation history](implementation-history.md#task-72).
2. Perform the remaining implementation-plan tasks; top priority is a complete,
   pretty overhaul of the touch controls, as good as or better than Mupen64Plus-AE's,
   borrowing freely from [mupen64plus-ae-turnip](https://github.com/pwnedbygary/mupen64plus-ae-turnip)
   and [Argosy Launcher](https://github.com/rommapp/argosy-launcher).
3. Refactor code that could be more readable/maintainable when touching it (ares core
   files stay close to upstream; only targeted behavior-preserving fixes there).
4. Document everything found, changed and still to do, for posterity and LLM handoff.
5. Add an **Asynchronous RDP** option (Mupen's `SynchronousRDP=False`) under N64
   Experimental, default off, toggleable from both the pause menu and Settings; if it
   needs a restart, tell the user (it does not: it applies at the next full sync).
6. Review the Star Wars: Rogue Squadron fix (a delay before presenting frames after a
   VI mode change) that causes extended black screens at boot in some games, and find a
   better fix that keeps Rogue Squadron rendering correctly.

Constraints in force:
- **Do not run anything that touches the phone** (user is using it with another
  LLM). Before that instruction, read-only `adb` queries were run once: device
  `FY24227104AB` = nubia Red Magic 9 Pro (NX769J, SM8650, Android 16, 1116×2480,
  60/90/120 Hz). Phobos is **not installed** on it; installed: Mupen64Plus turnip
  (`org.mupen64plusae.turnip.pwnedbygary` + `.debug`), Argosy, RetroArch, AetherSX2,
  Dolphin, Eden. No adb commands since. The auto-reviewer also blocked local command
  execution under this instruction, so the work was written with file edits and
  read-only research; the user later authorized the local build, review and commit
  (still no phone access).
- `/Users/gbagley/LLM-Projects/mupen64plus-ae-turnip` is being edited by another
  agent (branch `fix/fullscreen-gallery-branding`): **read-only**, no git commands.
- Local toolchain found (unverified by a build): JDK 17 at
  `/Library/Java/JavaVirtualMachines/temurin-17.jdk`; SDK at `~/Library/Android/sdk`
  (platform 37.0, build-tools 36.0.0, CMake 3.22.1, NDK **26.1.10909125 only**; CI
  uses 28.2.13676358); `thirdparty/libadrenotools` submodule **not initialized**;
  no Gradle cache yet.

### What was done

- **Touch controls** (design, findings F1–F24, reference comparison and device
  checklist in [touch-controls.md](touch-controls.md)): `ui/touch/` (model, per-family
  layouts, placement, engine, renderer, overlay, layout editor),
  `ui/TouchSettingsScreen.kt`, persistence in `SettingsStore`/`MainViewModel`, routes in
  `MainScaffold`, pause-menu Touch Controls section and quick actions, portrait picture
  at the top, hide while a controller is used. Research-driven additions: 48 dp minimum
  targets, auto-hold, swap hands, idle fade, separate portrait opacity, quick-tap
  delivery. Old `ui/TouchControls.kt` deleted.
- **Input refactor:** `input/InputBindings.kt`, `input/Hotkeys.kt`, rewritten
  `input/GameInputState.kt`; `EmulatorScreen.kt` split into `EmulatorScreen`,
  `EmulationMenu`, `EmulatorDialogs`, `ZxTapeProgress`; `MainActivity` cleanup.
- **Native input fixes** (`PhobosRunner.cpp`): 2600 console switches, SMS Pause, NGP
  Option; on-screen keys for MSX and the ColecoVision keypad; key set cleared on unload;
  per-bit press counters so quick taps reach the core.
- **Aspect ratio (F13):** `recordVideoGeometry()` / JNI `getVideoGeometry()` /
  `MainViewModel.videoGeometry`; Core Provided uses each core's real aspect, Integer
  Scaled works in physical pixels.
- **Performance** (full list, reasons and the ranked Mupen-gap explanation in the
  [performance audit](performance-audit.md#2026-09-24-scan-and-behavior-preserving-changes)):
  N64 Vulkan frames presented once instead of three CPU passes (`Screen::setPassthrough`,
  `vulkan.frontendPresentsScanout`); N64 screenshots from the scanout; NEON non-N64
  video conversion; cached input bindings; Granite error rate limiter re-enabled;
  hot-path logs removed (Neo Geo SMA, PS1 DualShock/CD-XA, video, axis); PS1 BIOS TTY
  tracer off on Android; Neo Geo LSPC model check hoisted; ThinLTO link at `-O3`;
  flavor `-march` for C sources.
- **Asynchronous RDP** (request 5): `vulkan.asynchronousRdp` gates the SyncFull
  timeline wait; `setN64AsyncRdp` JNI → `PhobosCore` → `SettingsStore`
  (`n64_async_rdp`, default false) → `MainViewModel.setN64AsyncRdp` → switches in
  Settings → N64 Experimental → Rendering and in the pause menu's N64 Experimental
  screen. Applies live; the frontend drains the GPU (`Vulkan::drainRdp`) before state
  save, state load and reset while it is on, or while async work is still unwaited
  (`vulkan.rdpWorkPending`, for a setting switched off moments earlier). Other N64 Experimental options that only apply at reset
  or reload now show a toast saying so while an N64 game runs.
- **Rogue Squadron** (request 6): see below.
- **Save-state previews (plan task 50):** each save writes `<state>.thumb` next to the
  state; the pause menu shows the current slot's preview and save time.
- **Run-Ahead audit:** confirmed the switch was never wired to native code; hidden
  (stored preference kept). Settings summary text corrected.

### Rogue Squadron transition hold — analysis and fix

History (see [implementation history](implementation-history.md#rogue-squadron)): the
menu's black screen was fixed by reverting the VI field toggle (`eae573558`,
`f21bfe54e`). The white/rainbow flash when the attract demo (VI width 400) switches to
the menu (VI width 1024, a 512-wide RDP buffer at `0x790000` scanned with a 1024 stride
for interlacing) was hidden by re-presenting the previous frame for a fixed 210
scanouts, about 3.5 s (`6a556f111`), limited to widths above 640 (`89fa3ad5f`).

Problems found:
1. **Boot freeze (the black screens).** `last_vi_width` starts at 0, so the first valid
   scanout of any game whose VI width is above 640 armed the hold even though no
   previous picture existed. That first frame, usually black, then became the
   "previous" picture and was re-presented for about 3.5 s. Games that boot into a wide
   VI mode froze on it, and so could any later wide transition from a black fade.
2. **Wrong scanout geometry.** Three places used the RDP's latest color image instead of
   the buffer the VI actually displays: `VideoInterface::scanout_memory_range` (the
   range synced before scanout when upscaling or when RDRAM is not host-coherent), and
   the ares CPU fallback in `vi.cpp` (used when Vulkan is unavailable). Under double
   buffering the latest color image is the back buffer, so CPU-drawn frames such as
   boot logos never reached the upscaled RDRAM (black) and the fallback showed
   half-drawn frames without the interlaced field offset. With Rogue Squadron's 1024
   stride over a 512-wide buffer, the substituted range also covered only half the rows
   the VI reads. These came from a hypothesis the history lists as a false lead
   (striding by the RDP width); the extract itself was already restored to the upstream
   VI geometry on 2026-08-15.

Fix (`ares/n64/vulkan/parallel-rdp/parallel-rdp/{video_interface,rdp_device}.{hpp,cpp}`,
`ares/n64/vi/vi.cpp`, comments in `ares/n64/rdp/{rdp.hpp,render.cpp}`):
- The hold arms only on a real transition: a previous picture exists and the old width
  was stable for 30 scanouts. Boot-time VI setup never arms it.
- It ends as soon as the VI origin lies within the first 16 lines of a framebuffer the
  RDP completed *after* the change (a buffer address reused across modes does not
  count). `CommandProcessor` records each frame's color images and commits them at
  `SYNC_FULL`, stamped with a sequence number, into an 8-entry history (ring worker
  thread, guarded by a small mutex, declared before the ring so it outlives the worker);
  `scanout()` hands the history to the VI after draining the ring. The 210-scanout cap
  remains as a safety net. For Rogue Squadron the VI origin alternates
  `0x790400`/`0x790800`, one or two lines into the `0x790000` buffer.
- `scanout_memory_range` and the CPU fallback use the VI origin and width, exactly
  what the VRAM extract reads (upstream). `setRdpFramebuffer`/`rdpFramebuffer*` now feed
  only the N64 Debug Logging coherency probe.

Device validation (N64 Debug Logging shows `PhobosVI mode-change: …` and
`hold released with N scanouts left`):
- Rogue Squadron: boot (no long black screen), attract demo → menu (no white/rainbow
  flash), menu → mission and back; also with 2× upscale.
- A game that boots into a wide VI mode, plus Mario Tennis, Conker and Zelda OoT, to
  check nothing else holds or changes.
- If Rogue Squadron's flash returns, the RDP must be completing frames into the menu
  buffer before the menu is drawn. Then keep the arming fix and remove the
  `origin_recently_rendered` release (fixed cap again), or require a minimum hold.

### Changed files (for review)

Under `android/app/src/main/java/com/phobos/emulator/`: new `ui/touch/{TouchModel,
TouchLayouts,TouchPlacement,TouchEngine,TouchRenderer,TouchControlsOverlay,
TouchLayoutEditor}.kt`, `ui/{TouchSettingsScreen,EmulationMenu,EmulatorDialogs,
ZxTapeProgress}.kt`, `input/{InputBindings,Hotkeys}.kt`; modified
`ui/{EmulatorScreen,Components,MainViewModel,MainScaffold,InputsSettingsScreen,
ZXKeyboard,N64ExperimentalSettingsScreen,EmulationSettingsScreen,SettingsScreen}.kt`,
`data/SettingsStore.kt`, `input/GameInputState.kt`, `MainActivity.kt`, `PhobosCore.kt`;
deleted `ui/TouchControls.kt`. Tests: new
`android/app/src/test/java/com/phobos/emulator/ui/touch/{TouchEngineTest,
TouchLayoutCodecTest,TouchLayoutsTest}.kt`.

Native: `android/app/src/main/cpp/{PhobosRunner.cpp,PhobosRunner.hpp,PhobosJNI.cpp}`;
`ares/n64/vulkan/{vulkan.hpp,vulkan.cpp}`; `ares/ares/node/video/{screen.hpp,screen.cpp}`;
`ares/n64/vi/vi.cpp`; `ares/n64/rdp/{rdp.hpp,render.cpp}`;
`ares/n64/vulkan/parallel-rdp/parallel-rdp/{video_interface.hpp,video_interface.cpp,
rdp_device.hpp,rdp_device.cpp}`; `ares/ng/cartridge/board/sma.cpp`;
`ares/ng/lspc/render.cpp`; `ares/ps1/{disc/cdxa.cpp,peripheral/dualshock/dualshock.cpp,
cpu/debugger.cpp}`. Build: `CMakeLists.txt`, `android/app/build.gradle.kts`. Docs:
`docs/{touch-controls,handoff,implementation-plan,performance-audit,
implementation-history}.md`.

### Pre-build review (advisory)

Three independent read-only reviews (native C++, the touch package, the app layer; no
commands run) found no definite compile errors and about twenty logic issues. Fixed:
- Native: the nall `contains()` misuse that disabled the Neo Geo map and rotated every
  system in WonderSwan vertical mode (touch-controls F20, F21); stale frames when the N64
  scanout is missing or smaller than the window (now black) and a guard against a
  window buffer smaller than requested; quick-tap replays limited to presses under
  100 ms and not counted while paused; `endScanout()` in the passthrough path taken under
  `vulkan.mutex`; the Rogue Squadron release requires a frame completed after the change;
  the new parallel-RDP members declared before the command ring (teardown order);
  Granite rate limiter state made atomic.
- Touch: editor taps no longer pin elements; taps between a cluster's buttons are not
  background taps; cancelled gestures; sliding onto action buttons; one finger per stick;
  equal D-pad sectors on first touch; press stickiness; per-axis minimum targets; stick
  thumb shows output; idle fade only after the last finger lifts; capped render caches;
  touch settings written in one DataStore transaction.
- App: hat D-pad clobbering key D-pads (F22); ZX rebind capture and scheme persistence
  (F23); pause-menu controller navigation, editor key isolation and quit-dialog cancel
  (F24); focus-loss release on the right node; keyboard hotkey only on ZX; fast-forward
  reset when leaving the screen; the first tap after using a controller only reveals
  hidden controls when they were hidden; the Settings-side editor honors WonderSwan
  vertical mode; save-state previews captured before the state and decoded downsampled.

Not changed (pre-existing, recorded for follow-up): `contains("Neo Geo")` when naming the
ROM temp file; a debug-only `NoSuchElementException` in `MainActivity`'s adb load path
when the app starts cold (`viewModel.systems.first {}`); touch placement avoids display
cutouts but not system gesture areas; the Neo Geo 2×2 grid's center presses B+C (they
are corridor neighbours; A–D, the other pair, is too far apart).

### Checks run (2026-09-24, local Mac, no device)

Toolchain: Temurin OpenJDK 17.0.20.1, Gradle 9.6.0 (wrapper), AGP 9.3.2, Kotlin 2.2.10,
Compose BOM 2024.06.00, CMake 3.22.1, **NDK 26.1.10909125** (the only NDK installed here;
CI's AGP default is 28.2.13676358, so CI compiles with a newer clang).
`thirdparty/libadrenotools` initialized at its pinned `8fae8ce25`.

Local-only setup, all git-ignored: `android/local.properties` (`sdk.dir`),
`GRADLE_USER_HOME=.local/gradle-home`, an init script
`.local/gradle/init-local-ndk.gradle` that sets `android.ndkVersion = "26.1.10909125"`,
`-Pandroid.builder.sdkDownload=false`, and a Java trust store
`.local/certs/truststore.p12` (JDK roots plus the Mac's keychain roots) passed with
`-Djavax.net.ssl.trustStore…`, because the JDK rejected the TLS certificate presented
for `services.gradle.org` on this network.

Results, all from `android/` with the setup above:
- `./gradlew :app:compileModernDebugKotlin`: BUILD SUCCESSFUL. Warnings only, all in
  untouched code (deprecated `Icons.Filled.ArrowBack`/`List`, `statusBarColor`, two
  unchecked casts in `SettingsStore.updateInputMapping`).
- `./gradlew :app:testModernDebugUnitTest`: BUILD SUCCESSFUL; `TouchEngineTest` 25,
  `TouchLayoutCodecTest` 6, `TouchLayoutsTest` 5 tests, 0 failures.
- `./gradlew :app:externalNativeBuildModernDebug`: BUILD SUCCESSFUL (4 min 7 s);
  `libphobos_android.so` linked. `build.ninja` confirms `-flto=thin -O3` on the link
  line and `-march=armv8.2-a+fp16+dotprod` on C sources (e.g. `libco/aarch64.c`).
- `./gradlew :app:externalNativeBuildLegacyDebug`: BUILD SUCCESSFUL (3 min 52 s);
  `-flto=thin -O3` on the link line, `-march=armv8-a+simd` on C sources.

Formal review of that snapshot: native PASS; touch and app/docs NEEDS CHANGES (menu
release reach, auto-hold latches across relayout, focus after resuming from a
controller-navigated pause menu, three doc statements). After the fixes (plus
non-blocking items: `rdpWorkPending` drain, ColecoVision keypad player 1 only, editor
pinch-to-drag handoff, save order restored with per-call temp files, compressed
previews, listener ownership) the checks were rerun:
- `./gradlew :app:testModernDebugUnitTest`: BUILD SUCCESSFUL; `TouchEngineTest` 30,
  `TouchLayoutCodecTest` 6, `TouchLayoutsTest` 5 tests (41), 0 failures.
- `./gradlew :app:externalNativeBuildModernDebug :app:externalNativeBuildLegacyDebug`:
  BUILD SUCCESSFUL (3 min 49 s); both libraries relinked with `-flto=thin -O3`.

Not run: APK packaging/signing, a build with NDK 28.2, and anything on a device.

### Checks run (2026-09-25, local Mac + Retroid Pocket 6 `49016109`)

Toolchain: Temurin OpenJDK 17, Gradle wrapper, **NDK 28.2.13676358** (CI's AGP
default; installed locally for this pass). Same git-ignored Gradle/truststore setup as
2026-09-24, with the init script pinning `android.ndkVersion = "28.2.13676358"`.

Host:
- `./gradlew :app:externalNativeBuildModernRelease :app:externalNativeBuildLegacyRelease`
  and matching `assemble*Release` packaging: BUILD SUCCESSFUL for both flavors.
- `./gradlew :app:testModernDebugUnitTest`: BUILD SUCCESSFUL; 41 tests, 0 failures
  (`TouchEngineTest` / `TouchLayoutCodecTest` / `TouchLayoutsTest`).

Device (RP6 only; Red Magic never used):
- Mario vs Boo save-state FPS (30 s × 2, Asynchronous RDP on): CI `373cec0` 50.6 FPS;
  this change set 58.4 / 58.4 FPS (see
  [performance audit](performance-audit.md#2026-09-25-device-profiling-retroid-pocket-6-and-changes)).
- N64 picture and a freshly captured save-state preview show at 4:3 (progressive
  640×240 scanouts); older previews stay squashed until re-saved.
- Smoke: Zelda OoT, Paper Mario, Rogue Squadron, Mischief Makers, F-Zero X, Wave Race 64
  boot and run; Conker's pub menu held ~4 minutes at ~60 FPS.
- Tap-to-pause with the menu button hidden was exercised after the top bar removal;
  tap-while-loading is gated on `isLoaded` (review fix).

Formal review of the frozen snapshot: native PASS (LOW timing-doc notes folded into the
audit); Kotlin/docs NEEDS CHANGES (tap-while-loading, FPS claim context, orientation
source, rate wording) — fixed in this tree before commit.

### Next steps

1. Device checks: the checklist in [touch-controls.md](touch-controls.md#verification-plan),
   the Rogue Squadron list above, and the regressions in the
   [performance audit](performance-audit.md#required-verification).
2. A CI build (NDK 28.2) before release.
3. Publish (push) the branch only when authorized.

## NGCD-M3 title-menu text fix — 2026-09-24

Scope: Neo Geo CD only. `ares/ng/disc/dma.cpp` (`0xe2dd` write order) and a
host harness in `tests/ngcd/`. `0xe2dd` writes wherever its destination points,
e.g. the FIX, PCM, Z80 and SPR upload zones; the DMA engine only exists on the
CD, so no MVS/AES path changes. Base `cc8f86f80`; commits `23c7a1b5a` (fix),
`2cac30152` (tests) and `cfa84f339` (docs) from branch
`cursor/ngcd-title-text-e2dd-36ae`.

Status (2026-09-24): the user tested the change and reported that the SamSho
CD title-menu text now renders correctly. The APK and device identity were
not recorded. The Android CI release build (legacy + modern) of `cfa84f339`
passed in GitHub Actions run 36038721749.

Observations:
- User screenshot, SamSho CD title menu (2026-09-24, phone capture at about
  1.4x): each menu letter has a detached top stroke with a gap row below it.
  Marks like underscores sit after "HOW" and before "GAME", in the columns
  directly above the "T" of "EXIT" and the "T" of "TO". The footer text,
  which is off the 8-pixel FIX grid (sprites), is intact.
- `tests/ngcd/run-tests.sh` drives the real `Dma::start`, the transfer-area
  `CPU::write` zones, `System::readC` and `LSPC::render` with synthetic data.
  Expected values come from Geolith (`geo_cd.c`, `geo_lspc.c`), libretro NeoCD
  (`memory.cpp`, `memory_mapped.cpp`, `video.cpp`) and MAME
  (`snk/neogeocd.cpp`, `snk/neogeo_spr.cpp`). On the base: 2823 checks,
  321 failures, all `0xe2dd`. FIX, PCM and Z80 receive every byte pair
  swapped, where all three references deliver the source bytes in order to
  byte-wide DRAM. SPR gets `[s0,s1,s1,s0]` instead of Geolith/NeoCD's
  `[s1,s0,s0,s1]` (MAME's SPR result differs from both). FIX glyphs uploaded
  this way render 244/256 pixels wrong. With the patch: 0 failures.
- `0xfc2d` (FIX and SPR), `0xffc5`, the `[1,0,3,2]` sprite plane order, flips
  and the CDC buffer → `0xffc5` → `LSPC::render` chain already match.

Derived:
- The screenshot is consistent with FIX row pairs swapped (0↔1, 2↔3, 4↔5,
  6↔7) in a font whose letters use rows 1..7. FIX byte
  `((x << 2 & 24) ^ 16) | row` holds one row of a two-pixel column, so a
  byte-pair swap is a row-pair swap, and only `0xe2dd` produces one in this
  core. `9230e4d60` introduced the reversed order; the earlier MAME-style code
  delivered FIX bytes in order.
- The archived "`0xfc2d` one byte off" candidate quotes `[d0,00,d1,d0]`,
  which is the host little-endian byte view of the `n16` words holding the
  reference `[00,d0,d0,d1]` in `ng_spr.raw`. It is not a defect, and flipping
  it would break the FIX/PCM/Z80 loads that use `0xfc2d`. The archive's
  "`0xe2dd` verified numerically" note has the same misreading. The Sep-23
  device trace (2942 DMAs, from temporary probes in unmerged WIP commit
  `78c220ad2` on `fix/ngcd-m3-fc2d-byte-phase`) showed `0xfc2d` targeting
  only PCM (256), Z80 (32) and FIX (52), with sprites arriving via `0xffc5`.

Limitations and separate findings:
- PCM and Z80 uploads through `0xe2dd` change too; this is host-tested only
  and cannot be heard on the device yet (next item).
- On the CD the Z80 zone is backed only by the APU's 2KiB work RAM, and the
  Z80 fetches code through `cartridge.readM` (0xFF without a cartridge). Z80
  sound programs therefore cannot run on the CD before or after this change,
  and the YM2610 is the core's only audio stream (no CD-audio stream), so
  NGCD games should be silent (derived from the code, not observed). This
  separate, pre-existing issue also blocks the older "verify NGCD audio"
  item below.
- The CD sprite tile index in `ares/ng/lspc/render.cpp` ORs the odd word's
  MSB field into tile bits 12..15, while all three references use
  `even word & 0x7fff`. It is not visible in the screenshot, and `9230e4d60`
  claims HUD/UI sprites need it, so it needs its own device check.

Checks run (Ubuntu 24.04, g++ 13.3.0):
`bash tests/ngcd/run-tests.sh /tmp/ngcd-build-A` in this checkout, and the
same `tests/ngcd` copied into a clean worktree at `cc8f86f80`, run with
`bash tests/ngcd/run-tests.sh /tmp/ngcd-build-base`. Not run: Android build
(no SDK/NDK here and `thirdparty/libadrenotools` is not checked out;
`android.yml` builds release APKs on push) and device validation (no device).

Remaining checks (not yet reported): compare the BIOS menu, other text screens
and in-fight HUD colours against a build from before this change. If HUD or
BIOS colours differ, compare the same screen in Geolith or NeoCD before
treating it as a regression: `9230e4d60` recorded those screens as correct with
the old order. The CD sprite tile index and CD Z80 program memory findings
above are separate follow-ups.

Method notes for future NGCD work (how this was found):
- Compare against Geolith, libretro NeoCD and MAME source code, not against
  comments or summaries, and translate their expressions literally. The CD
  tile-index fold came from porting `(attr & 0x00f0) << 12` (bits 4..7 to
  16..19) as `attr.bit(4,7) << 12` (bits 12..15).
- Test the real core on the host before a device round. `bash
  tests/ngcd/run-tests.sh` needs no SDK, BIOS, disc or device. Add a case for
  the path you intend to change, and show it failing on the base first.
- `dumpNgGfx` files such as `ng_spr.raw` and `ng_wram.raw` store `n16` words in
  host little-endian order, so each byte pair is swapped relative to 68K byte
  order. Reading them as byte arrays produced the archived `0xfc2d` and
  `0xe2dd` misreadings.
- Map the screenshot symptom to a layer before choosing a fix. Rows swapped
  in pairs inside 8x8 glyphs point to a FIX byte-pair swap; wrong glyphs or
  tiles point to a tile index; right shapes in wrong colours point to sprite
  plane or byte order.
- Keep device rounds to one change. `9230e4d60` verified several rendering
  and DMA changes together; its `0xe2dd` part broke FIX uploads, and the
  leftover glitch was then attributed to other causes.

## Post-merge environment repair

Scope: configure the absent Replit post-merge hook; no emulator, signing or device
changes. Observed failure: HOOK_NOT_FOUND; native submodule was uninitialized.
The hook restores pinned submodules and runs Gradle help without prompting for
licenses. Full APK compilation remains separate. Verification and exact-snapshot
independent review results are recorded outside the frozen snapshot.

## Reconciled documentation baseline — 2026-09-15

The authoritative roadmap is [implementation-plan.md](implementation-plan.md);
its dated detailed evidence is preserved in
[implementation-history.md](implementation-history.md). The current performance
record is [performance-audit.md](performance-audit.md), and the reproducible
comparison gate is [mario-tennis-benchmark.md](mario-tennis-benchmark.md).
The [development process](development-process.md) and
[coordinator prompt](coordinator-prompt.md) apply before every authored change
and supersede the historical broad-staging, immediate-commit, and auto-deploy
instructions retained below.

The user-supplied comparison source is
[mupen64plus-ae-turnip](https://github.com/pwnedbygary/mupen64plus-ae-turnip)
stable tag `v336`, read-only resolved to
`dc955483a97daa99cb1f9db06e2334464fa1664d`. Debug/DD branches are excluded.
The tag contents and “stable” characterization are user-reported, not
independently measured here; an attested reference APK, matched device/driver/
settings, ROM identity, and traces are still blockers. Upstream context is
[ares](https://github.com/ares-emulator/ares), not a drop-in patch source.

This iteration changes documentation only. No emulator behavior, signing,
saves, APK, build, or device data changed. Native performance measurements are
pending; old FPS reports are historical, not freshly reproduced results.
Preserve timing defaults. Task #4 audio-buffer reuse is cancelled, not
authorized, and not the next automatic change. Task #2 release APK update
safety is now cancelled; update compatibility remains unverified.

**Phobos** is an Android N64-first multi-system emulator (package `com.phobos.emulator`, module `:app`, native lib `libphobos_android.so`).
Native core is a heavily customized fork of **ares** (JIT recompilers, parallel-RDP Vulkan renderer, libadrenotools Turnip driver). UI is Jetpack Compose.

## Historical session detail (retained for context; not a current verification)

**Latest reported work: Neo Geo CD — RENDERING ROUND COMPLETE (committed
`9230e4d60` + `783a6cc27`).** Full technical record (incl. Aug-28 appendix +
2026-09-01 static round) is in [implementation-history.md](implementation-history.md)
at [Task NGCD-M2](implementation-history.md#task-ngcd-m2), the
[Neo Geo CD consolidated reference](implementation-history.md#neo-geo-cd-consolidated-reference),
and [Task NGCD-M3](implementation-history.md#task-ngcd-m3). The current
disposition is in [implementation-plan.md](implementation-plan.md).

**Verified on-device (RP6 `49016109`, SamSho RPG):** BIOS menu, NEO-GEO CD logo, title, character select,
level-select (was a black screen), in-fight HUD (life/POW bars, KO counter) + characters + backgrounds all
correct @59.2 FPS. Game code stays in its main loop.

**Neo Geo CD rendering fixes (2026-08-28, `9230e4d60` — committed & pushed):**
1. **CD sprite DRAM plane order [1,0,3,2]** (MVS cart is [0,2,1,3]) — the CD data bus is word-lane-swapped
   (Geolith `cdmode`; libretro neocd agrees) — `lspc/render.cpp`.
2. **`System::readC` byte lane**: byte@even lives in lane 1 (`.byte(!(address&1))`) — was inverted vs the
   transfer-area upload, scrambling sprite pixels.
3. **CD sprite slots 1..381** (slot 0 unused, 381 IS used; MVS is 0..380).
4. **Tile-number MSB**: CD odd-word bits 4..7 fold into tile bits 12..15 masked to `0x7fff` (Geolith), not
   bits 16..19 like MVS.
5. **THE HUD root cause — DMA `0xe2dd`/`0xfc2d` ("skip odd bytes")**: the port had followed MAME's
   unvalidated LC8953 heuristic (byte zero-extended into separate words = `[b,0,b,0]`, halving the 4bpp
   planes on word-aligned tile data → HUD black boxes / "88888888" glyphs / half-detail characters).
   Rewritten to libretro's mirrored-word layout (`[lo,hi,hi,lo]` per source word) — `dma.cpp`.
6. **OPNB ADPCM** now reads `pcmRam` via `system.readVA` on CD (was `0xff`); PCM upload bank math fixed.
7. **CD read-speed feature REMOVED** (user directive 2026-08-28): `cdd.readSpeed` + all app wiring deleted —
   the drive is fixed at the authentic 75 Hz 1x CDD tick. The feature was unsound: at >1x the BIOS's access
   machine (`$C0E99E`) reads CDC registers the pipeline hasn't written yet → **DISC I/O ERROR ID=0000/0002**
   (observed even at 2x on the final build). The `dumpNgGfx` debug harness remains (`--ez dump_ng_gfx true`).

**Open residual (Task NGCD-M3 — track it):** the **title-menu text glitch** (menu text sprites whose tile
families `0x2000`/`0x0c00`/`0x4376` had a 128-byte lead-in: data at tile*128+0x80, blank at base). Ruled out:
global +0x80 fetch (breaks BIOS menu/characters — block-specific), 0xe2dd source-header skip (breaks
everything — the work-RAM source is already plain tiles). Open hypothesis: dest-bank mismatch at DMA time.
Static CD file-format analysis is the next round (step 2 of the 2026-09-01 plan).

**Neo Geo / Neo Geo CD input default (2026-09-01, `resolveButtonBit`):** Xbox-layout reference,
physical → core button: **X→A, Y→B, A→C, B→D; R1→A+B, R2→C+D, L1→B+C, L2→A+B+C, R3→B+C+D**
(bitmask per core button; Supersedes the Genesis-heritage C→R1/D→R2 single-bit mapping for Neo Geo systems
only). D-pad + Start/Select (coin) unchanged. The full per-core/per-game rebinder is still Tasks 13a-13d.

**Prior status (2026-08-27 — Neo Geo CD M2 boot achieved):**

1. **Raw-sector LBA offset (THE hidden data bug):** `nall::vfs::cdrom` images begin at the disc lead-in —
   logical LBA N lives at physical sector `LeadInSectors + LBAtoABA(N)` = `7500 + N + 150`.
   `Disc::readSectorRaw(lba)` was seeking `lba*2448` → returned ALL-ZERO sectors (blank lead-in), so every
   BIOS content check (`$C0D4C4` directory compare, access machine reads) silently failed. Fixed to
   `2448 * (LeadInSectors + LBAtoABA(lba))` (same mapping as PS1 `drive.cpp`). Raw reads now return real
   disc data (`00ff...` sync pattern, valid directories).
2. **Decoder IRQ vector mix-up (the boot-completion blocker):** the LC8951 decoder-complete event
   (`ctrlChecks`) was sent to vector `$17` (stub `$C0A44E` — unused). It MUST go to vector **`$15`**
   (stub `$C0A40A`, ack `$FF000F=0x20`) which calls the CDD **access machine `$C0E99E`** — the routine that
   advances `$76BC/$7688/$76B6`, letting the boot wait loop `$C0CF56` exit. Wired via `type3Pending`
   (CDDType3=21=0x15). With it, the boot-init completes: BIOS reads the directory, DMA (`ffc5`→`$111204`,
   `fe6d` copies) lands it, and the BIOS **sets `$10F656` bit 7 (disc-ready) itself at `$C0D2E4`**.
3. **IPL gating + ack-based pending (earlier this session):** CDD interrupts are normal level-2 IRQs —
   dispatch only when `2 > r.i`; pending flags cleared by the `$FF000F` ack write, not the dispatch
   (prevents type2 starvation under sector streaming and IRQ-storm wedges).

**VERIFIED ON-DEVICE (final clean build, diagnostics stripped):** boot → CD Player → boot-init (real
directory reads at 75 Hz, DMA, decoder IRQs) → BIOS sets disc-ready itself → Start gate latches
(`$76B9=0x80`) → game code loads → **SamSho RPG title/attract screen renders and animates**, CPU in game
main loop. The `$10F656` bit-0 poke (disc-detected HLE at settle) remains — it triggers the BIOS's own
boot-init, which then does everything else naturally.

**Same-day follow-up (2026-08-27 evening):**
- **"WAIT FOR A MOMENT" 2s blink fixed** — the bit-0 poke is now gated on bit 7 clear
  (`if(!(w.byte(1) & 0x80))`). Every STOP re-armed the settle and every settle re-poked bit 0, so the
  BIOS's boot-init re-ran forever and the menu blipped WAIT every ~2s. Now the disc check runs once.
- **FIX (text) upload-zone heap overrun fixed** — `case 5` mapped `(address>>1) & 0x3FFFF` into a
  128KiB `fixRam`; mask must be `0x1FFFF` (matches libretro neocd). Fixes garbled in-game text.
- **Per-core CD read speed option — REMOVED 2026-08-28 (`9230e4d60`, see CURRENT STATUS above).**
  The drive is fixed at authentic 1x. The old caption: the CDD tick scaled to `75×N` Hz; at >1x the BIOS's
  access machine cannot drain sectors fast enough (MSF=0 / no-response → DISC I/O ERROR ID=0000/0002), so the
  feature was removed at the user's directive. Ring overflow remains guarded in `tick()`.

Open items (non-blocking): flag the unrelated AGP 9.3.1→9.3.2 bump in `android/gradle/libs.versions.toml`;
verify NGCD audio (OPNB/ADPCM path) on-device while in a fight.

**Prior status (2026-08-25)**:

**Latest work:** Task #10c ZX Spectrum 128K FIXED & VERIFIED on-device (commit `1bf97e3ac`). Root cause (proven on-device via temporary ZX128Diag instrumentation, since removed): the 128K core names its system node "ZX Spectrum 128", but `PhobosRunner::pak()`'s tape branch matched `root->name() == "ZX Spectrum"` → empty pak for 128K loads → `Tape::load()` read frequency 0 → cubic resampler ratio 0 → infinite loop in `Cubic::write` on the tape thread's first frame → scheduler wedged, zero frames. Fix: `root->name().beginsWith("ZX Spectrum")`. Verified: Enduro Racer (128K) 50.8 FPS stable (PAL 50Hz), audio ring healthy; Elite 48K regression 50.6 FPS; ZX→SFC reload 60.2 FPS. Prior work same day: Task #10c PCE family FIXED (commit `2795e5813`) — missing PROFILE_PERFORMANCE define compiled out `PSG::main()` → scheduler deadlock on first timer sync; Final Lap Twin 60 FPS, SuperGrafx 60 FPS, Rondo of Blood 60 FPS, SFC/PS1/MD regression 59.9-60.2 FPS.

**Neo Geo — Neo Geo CD (NGCD) M1 VERIFIED ON-DEVICE + COMMITTED (`2c5f7f444`) (2026-08-25):** M1 = BIOS-boot skeleton WORKS — SNK logo renders, CD Player UI boots and displays. Port details: upload zones with banks baked at UPLOAD time (`write()` case 0: `addr&=0xfffff; addr.bit(20,21)=spriteUploadBank`, byte-wise upper/lower; case 5 fix: `(address & 0x3ffff) >> 1`), fetch-side `readC/S/VA/VB` are dead-simple fall-throughs (`spriteRam[addr>>1].byte(addr&1)` / `fixRam[addr]` / `pcmRam[addr]` / `0xff`). KEY GOTCHA: `Model` is BOTH `ares::NeoGeo::Model` (helper struct, `ng.hpp`) and `ares::NeoGeo::System::Model` (enum) — helper calls inside `System::` members MUST be `NeoGeo::Model::NeoGeoCD()`. **M2 (CD drive) CANNOT BE PORTED — the reference branch contains NO CDD/CDC/disc implementation** (verified by fetching the whole tree; it stops exactly at BIOS→CD Player). M2 must be written from scratch; reusable infra exists: PS1 pattern `vfs::cdrom::open(.cue/.chd) → cd.rom` pak file, 2448-byte sectors + `session.decode(subchannel,96)` TOC, libchdr already linked in build. M2 plan: (a) extend `mia/medium/neo-geo-cd.cpp` to attach disc images, (b) CDD command/status processor (research register map — MAME `neocd.cpp` usable as docs only, GPL vs ISC licensing care), (c) CDC DMA into TRANSAREA upload zones, (d) CDDA audio streaming. **AES/MVS REGRESSIONS FROM THE PORT — FIXED:** (1) memory-card read byte order in `cpu/memory.cpp` MUST stay `byte(0)=cardSlot.read(), byte(1)=0xff` (swapping → BIOS "MEMORY CARD ERROR" black screen); (2) `lspc/render.cpp` must NEVER be wholesale-replaced by reference stock code — user's hardware-verified extras (cromMask wrap, MVS rx>=512 hwrap, garou fixBank offsets, vflip/vscale handling) live there; only swap `cartridge.readC/readS`→`system.readC/readS` (×4/×1). Also this day: firmware scanner keyword heuristic (`keywordFirmwareMatch`, commit `656ddd651`) picks up N64DD IPLs under any naming ("64dd"+"ipl" → region keywords).

**Neo Geo CD M2 IN PROGRESS (2026-08-25, uncommitted, CD Player disc detection VERIFIED):** M2a disc plumbing LANDED + COMMITTED (`9511cea73`): `mia/medium/neo-geo-cd.cpp` now attaches `cd.rom` via `vfs::cdrom::open` for `.cue/.chd`; core `Disc` device (`ares/ng/disc/disc.{hpp,cpp}`) decodes TOC via `CD::Session` and serves `readSectorRaw(LBA)` (2448-byte raw). M2b CDD/CDC/DMA research COMPLETE (MAME `neogeocd.cpp` + `megacdcd.cpp` + libretro `neocd`): register map `$FF0002`/`$FF0016`/`$FF0100`/`$FF0102`/`$FF011C`/`$FF0160`-`$FF0164`/`$FF0180`-`$FF0182`/`$FF01A0`-`$FF01A2`/`$FF0060`-`$FF007E`, 10-nibble CDD serial (cmd/status + `+0x5` checksum quirk, StatusHack), LC8951 regs, DMA modes (`cffd`/`e2dd`/`fc2d`/`fe3d`/`fef5`/`ffc5`/`ffcd`), 75Hz sector pipeline. M2b phase-1+2+4 LANDED: `cdd.{hpp,cpp}` (serial `rxRead`/`txWrite`/`commsControl` + all TOC + CDZ `subcmd 7` disc-recognition fix → value `2` for SamSho RPG), `cdc.{hpp,cpp}` (LC8951 regfile `addressWrite`/`dataRead`), `dma.{hpp,cpp}` (LC8359 modes), IRQ wiring (level-2 vectors `0x15`-`0x17`, `FF000E`/`FF000F` ack). **Masks FIXED:** `TRANSAREA`/`SPRBANK`/`PCMBANK`/`Z80RST` were `(addr&0xfffe)==0x01xx` never matching `0xFF01xx` (broken in reference; fixed to `0xfffffe==0xff01xx` — BIOS writes `FF0105`/`FF01A1`). Region `FF011C` defaults to US (English menus). **Verified:** CD Player disc detection WORKS — TOC enumeration completes, disc shows **TRACK 29 / TIME 68:12** (SamSho RPG) and `FF0101`/`FF0103` init writes logged; sector engine streams (`lba 15→43+`, `ctrl=0x0100` data mode, `ctrl0=0xa7`) and disc is recognized. **Remaining blocker:** game boot `START` does nothing — boot state reaches `3` (2426 reads) then stalls waiting for game code at `0x100000` never arriving; `FF0061` DMA never fires (`0` lines), `type1` IRQ (sector completion) starved by constant `type2` (75Hz) — priority `type1>type2` + ack-gating tried, pending at `IPL=7` (3000 `PEND ipl=7` from boot critical section). Next: wire `type1` dispatch without IPL gate and ensure DMA setup (`FF0064`/`FF0070`/`FF0061`) path from `c0ebc0` sector processing reaches `dma.start()`.

**Neo Geo CD (NGCD) M2 — handoff (2026-08-25): input question CLOSED by on-device probe.** A temporary
PAD probe in `ares/ng/controller/arcade-stick/arcade-stick.cpp` (`readControls()`/`pollCoin()`), throttled
`++n % 30 == 1`, logged button state while holding **A** then pressing **Start** on the Retroid built-in pad.

| Evidence (logcat, `NGCD` tag) | Conclusion |
|---|---|
| `PAD probe … a=1` (held A) | A (`k:96`) reaches the core — **input works** |
| `PAD probe … start=1` (pressed Start) | Start (`k:108`) reaches the core — **input works** |
| BIOS programmed CDC (`reg 01←e2`, `0a←a7`, `0b←f0`) + began read (`stat=0001`, ~8 s) | BIOS received Start, started a CD load |
| `t1=0` entire run; `pend=0010` (CDD pending) yet never dispatched | **type1 (sector-completion) IRQ never fires — the boot blocker** |

**Front-end input (`k:` mapping, hotkey capture, `controllerPlayerIndex`) is 100% working — no further input
work.** The blocker is entirely the in-progress NGCD M2 drive/IRQ/DMA pipeline.

**Root cause (static review, cross-ref wiki.neogeodev.org):**
- **Defect 1 — DMA trigger mismatch** (`ares/ng/cpu/memory.cpp:560-575`): LC8953 trigger is the **byte**
  `$FF0061` (`$00`=load microcode, `$40`=start) per the [DMA](https://wiki.neogeodev.org/index.php/DMA) +
  [LC8953](https://wiki.neogeodev.org/index.php/LC8953) pages; params 32-bit at `$FF0064`/`$FF0068`/`$FF0070`,
  microcode `$FF0080`-`$FF008E`. Code matches **word** `0xff0060`/`bit6` and swallows microcode → "`FF0061` DMA
  never fires".
- **Defect 2 — CDD/type1 pending but not dispatched** (`ares/ng/cpu/cpu.cpp:38-64`): `pend=0010`, `t1=t2=t3=0`.
  Hypotheses: empty `if(NeoGeo::Model::NeoGeoCD()){}`/early returns skip the CDD block; `type1Pending` cleared
  before dispatch or sector pipeline body not running; `$FF000F` ack swallowed.

> Reference: MAME `neogeocd.cpp` (GPL, docs-only) + proven Sanyo CDC `DTEI`/`DTBSY`/`DTTRG` handshake in
> `ares/md/mcd/cdc.cpp`.

**Repro / next step:** device `49016109` (Retroid Pocket 6, Adreno 740), SamSho RPG disc. Clear logcat, press
Start, `adb -s 49016109 logcat -d | grep -E "NGCD|CDD|CDC|DMA"`. Then wire `type1` dispatch (priority over 75 Hz
`type2`) + the `$FF0061` byte DMA trigger + microcode routing; iterate until the game boots past boot state 3
(code reaches `0x100000`); confirm no AES/MVS regression. See
[implementation-plan.md](implementation-plan.md), the
[archived Task NGCD-M2 record](implementation-history.md#task-ngcd-m2), and
the [Neo Geo CD consolidated reference](implementation-history.md#neo-geo-cd-consolidated-reference).

**Neo Geo (Task #10c, UNCOMMITTED state):** Un-gated for diagnosis via `if (false && identifiedSystem == "Neo Geo")` in PhobosRunner.cpp. kof2003.zip loads (MIA: AES; core reports "Neo Geo MVS", MVS BIOS sp-e.sp1 attached; "VFS: Failed to attach static.rom" = benign warning), **runs 59.2-60.1 FPS sustained** (old black-screen/0-FPS hang GONE). Streams registered: FM (ch=2, 500kHz) + SSG (ch=1, 500kHz). **BUT audio ring stays 0/12000 (0%) and no sound** — multi-stream lockstep (`PhobosRunner.cpp` audio() ~1384-1417: emit only when EVERY stream pending, bounded 8192) or mute path suspect. Video presentation unverified — every adb loader load ran HEADLESS (no navigation → no SurfaceView). FIXES LANDED UNCOMMITTED: (1) debug loader now navigates to the emulator screen (mirrors SystemDetailScreen flow); (2) swap-screen feature (below). After build+deploy: verify NG video via loader, then investigate the 0% audio ring, then finalize gate state + verify MVS/AES + commit.

**Swap-Screen feature (VERIFIED 2026-08-18 on-device, commit pending):** Hotkey to leave a running game to the library/console/settings — emulation PAUSES on leave, UNPAUSES on return; the game is unloaded ONLY via the quit dialog. Implementation: `MainViewModel.navigateTo(route)` SharedFlow navEvents + `emulatorScreenVisible` StateFlow + `swapToLibrary()`/`swapBackToGame()`; `MainScaffold` collects navEvents (special-case library/console/settings with popUpTo(start)+launchSingleTop); EmulatorScreen hotkey `"library"` → swapToLibrary, pause menu gains "Library" button, `onDispose` NO LONGER unloads (kept GameInputState.reset + emulatorScreenVisible=false), quit dialog Quit button calls `unloadSystem()` then onBack; MainActivity debug loader NOW navigates to the emulator screen after loadRom (mirrors SystemDetailScreen load-then-navigate — fixes ALL previous adb loads running headless behind the library); `SettingsStore` defaults mirrored from the developer's device config (Z-button based: 101=Z + A/B/X/Y/L1/R1/L2/R2/DPAD/SELECT/START/C etc.; library = Z+C = [101,98]; ff_hold unbound; analog_toggle = C alone [98]); HotkeyMappingScreen label "Swap to Library". KEY INSIGHT: the content-view OnKeyListener only fires when NO focused view consumes the key — on the library screen Compose absorbs them — so the RETURN hotkey is intercepted at window level via a `Window.Callback` wrapper (ComponentActivity restricts overriding dispatchKeyEvent itself). Swap away+back verified round-trip on-device; quit dialog unload verified. Testing caveat: adb `input keyevent` cannot hold combos — hotkeys must be tested with the real controller.

**All cores verified WORKING** including Neo Geo MVS/AES (input, BIOS dialog, and tall-sprite/background rendering all FIXED and verified).

**Neo Geo — 2026-08-24 VERIFIED (vflip tile-ordering regression REVERTED, commit `pending`):** `e92486f72`'s undocumented "vflip tile ordering" hunk in `ares/ng/lspc/render.cpp` (LLM guess, contradicts MAME `neogeo_spr.cpp` — which does per-tile fetch + `row ^= 0xf` XOR and never reorders tiles) broke tall-sprite rendering (`kof2003` title = half-tiles/misaligned rows). Reverted ONLY that hunk to the exact `b1c0a9fe1` code; the rest of `e92486f72` (SMA P-ROM BE decrypt, Z80 audio banking, coin polling) is intact — **do NOT `git revert e92486f72`** (bundled 24 files). Verified clean on-device `49016109` via `see_image` screencaps: **`kof2003` title (was garbled), `kof98` (PROGSF1) title, `kof99` (SMA) title**; `ssideki4` title/how-to-play/championship screens clean. `ssideki4` **in-match** field garble persists = the SEPARATE pre-existing zoom-path issue (was NOT introduced by this revert). `AudioDiag ring=0/12000` still logs (known separate issue; KOF2003 audio user-confirmed working). **VERIFICATION GOTCHA:** hot ROM reload via `am start --activity-single-top` can leave the SurfaceView showing the PREVIOUS core's stale frame (kof98 load once displayed ssideki4's soccer screen — core loaded fine per logcat); if a post-reload screen looks like the wrong game, `am force-stop` + cold `am start` before judging.

**Neo Geo — 2026-08-23 VERIFIED (all 288 titles boot):** SMA boot **FIXED & VERIFIED** on-device for `kof99`/`kof2000`/`garou`/`garouh`/`mslug3`/`mslug3a` (was `69K` grid, now `414K`/`263K`/`582K`/`415K` titles `@59.2 FPS`). Root cause was **two-fold**: (1) hardcoded `p[0..7]=0x10f300` override, **plus** (2) endianness bug: SMA decrypt used `u16*` little-endian cast while `prom` is `readm(2L)` big-endian and `BML` was `load16_word_swap` — `P` double-swapped → garbage. Fixed `BML` to keep `load16_word_swap` for `ka.neo-sma`/`251-p1` etc. (`262144` for `ka`) and changed decrypt to `readBE`/`writeBE` (`p[off]<<8|p[off+1]`) matching `prom` BE. Verified vectors `0010f300` `NEO-GEO` and `SMA prom` `0010f300`. `kof98` `PROGSF1` also verified, `kof94`/`kof2001` standard titles verified. **REMAINING 2026-08-23:** (1) **Sound** — `samsho`/`samsho2` silent, `AudioDiag ring 0/12000` (log shows `ring 0` for all Neo Geo, but `KOF2003` user-confirmed audio works; suspected log formatting vs real fault — investigating `PhobosRunner` multi-stream lockstep and `OPNB`/`APU`); (2) **Coin** — `SELECT`/`START` coin works sometimes but not always (pollCoin every-frame via `LSPC` broke audio, reverted to poll only on input reads; need reliable `REG_STATUS_A` coin without per-frame `platform->input`); (3) **ssideki4** — field graphics still garbled in gameplay (hscale `zoom_x_tables` already applied, but `ssideki4` uses different zoom path — investigate `render.cpp` tall-sprite/hscale for this title).
All three are UNCOMMITTED. The `kof99`/`kof2000` `NEO-SMA` "Still ✗" line in the 2026-08-21 compat paragraph is now SUPERSEDED by fix #1.

**Neo Geo compat pass (in progress 2026-08-21 — `wiki.neogeodev.org` as primary ref):** on-device results tracked in `docs/neo-geo-compatibility.md`. **MVS warning-screen stall — ROOT CAUSE found & FIXED for 1994-95 stub:** the 1994-95 boot stub (`tst.b $10FD82; beq pass; tst.w $D00100; beq pass` at `$38D6E` etc.) always failed under `ares` because `$10FD82` (WRAM `$10FD82`, zero after BIOS init on real MVS) is non-zero on a cold boot, so it fell into the green/red `WARNING` hang (`move.b d0,$300001; bra.s *` at `$276`/`$29A`/`$4E2`/`$506` via `tst.b $10FD82`). Earlier `coin`/`freeplay` diagnosis was wrong — `kof95` (standard, no decrypt) was hanging at the same `WARNING` via the same `$10FD82` check, not at the BIOS coin wait. **FIX APPLIED** in `mia/medium/neo-geo.cpp:166` — generic `beq→bra` (`67`→`60`) + `13C0 00300001 60F8`→`4E75` (`rts`) patch (covers all titles sharing the 1994-95 stub, standard and `K2K2`). Verified `kof95`→KOF95 title, `samsho3`→SamSho3 title, `samsho4`→SamSho4 title, `samsho5`→SamSho5 title, `kof94`/`kof96`/`kof97`/`kof2001`/`kof2002` titles @59.2 FPS. **FIXED 2026-08-21:** `kof98` (**PROGSF1** `ALTERA EPM7128` `242-p1` 2M scrambled → `mia/medium/neo-geo.cpp:374` `sec[]`/`pos[]` offline `gngeo:c:kof98_decrypt_68k` + `ares/ng/cartridge/board/progsf1.cpp:19` `ProgSF1` `bit(0,3)` `59.2 FPS` title verified `/tmp/kof98_test.png`, `header @100: 4e 45 4f 2d` `vectors 0010f300`, `kof98h` `PROGBK1` bypass). **Still `✗`:** `kof99`/`kof2000` (`NEO-SMA` `ka.neo-sma`/`neo-sma` `9M` `P` `QFP144` `mame:prot_sma.cpp` `bitswap<16/10/19>` `SMA::kof99_bank_base`/`kof2000_bank_base` wired in `mia/medium/neo-geo.cpp:405,423` + `ares/ng/cartridge/board/sma.cpp:50` `49016109` but `header @100: c1 e4` `c1=0` grid `37K`/`162K` still — `loadRoms` `0xC0000` hole vs `0x700000` relocate audit pending) + `garou`/`mslug3` (`green.neo-sma`/`kf.neo-sma` staged from `/home/garyb/Mounts/Emulation/Emulation/APKs/` to `/storage/EBFF-F6C0/ROMs/arcade/` now `SMA` `decryptGarouSma`/`Mslug3Sma` wired). `kof98umh`/`kofnw`/`kofxi` are **not MVS** (IGS PGM `ig-d3_*` / Atomiswave `ax220*`/`ax320*`) and correctly show the MVS BIOS popup. Palette-bank `8746d3f56` and `ssideki4` `hscale`/`wrap-around` fixes already verified.

**RECENT (2026-08-19 → 2026-08-20, COMMITTED & PUSHED in v1.0.0):**
- **Neo Geo Palette Banking Inversion (FIXED):** In `ares/ng/cpu/memory.cpp`, write handling for `$3A000E` (`REG_PALBANK0`) and `$3A001E` (`REG_PALBANK1`) was inverted (`$3A000E` was setting `pramBank = 1` and `$3A001E` setting `pramBank = 0`). Fixed so `$3A000E` selects bank 0 and `$3A001E` selects bank 1, resolving wrong palette banks during gameplay (e.g. blue grass in *The Ultimate 11*).
- **GPU Driver Downloader (QoL, DONE):** `DriverManagerScreen.kt` — download/install/delete + scrollable "Active Driver" selector (System Default + all installed `*.so`, identity-deduped so no `_2` dupes). Manual installs refresh the list via `_driverSuccessEvent`. Red Delete button (`Color(0xFFD32F2F)`). Active driver set via `setCustomDriverPath`.
- **Neo Geo BIOS dialog is now truthful:** only shows "Neo Geo BIOS Required" when `neogeo.zip` is truly absent; otherwise "Neo Geo ROM Failed to Load" (e.g. 1941 is a CPS-1 Capcom title, NOT Neo Geo — it is absent from MIA's Neo Geo DB, so loading it as Neo Geo always fails and the old popup wrongly blamed the BIOS). Native BIOS-attach branch now also matches a bare `"Neo Geo"` nodeName. Memory `bugfix/neogeo-bios-dialog`.
- **Neo Geo INPUT FIX:** the Neo Geo `ControllerPort` connects an "Arcade Stick" (correct — `connectDevices` already routes Neo Geo → `"Arcade Stick"` at `PhobosRunner.cpp:2088`; my `port.cpp` edit accepting `"Gamepad"` is a harmless no-op). The actual dead input was a FRONT-END bug: many controllers report the D-pad as a HAT axis, which `GameInputState.updateHotkeyDpad` only routed into `hotkeyKeys` (hotkey combos), never into gameplay button bits. Fixed by latching the hat D-pad into `hwButtons` bits 0–3 (`GameInputState.kt`). A/B/C/D always worked (they arrive as keycodes); analog sticks are digital-only for Neo Geo (expected, NOT a bug). Memory `bugfix/neo-geo-input-gamepad`.
- **Neo Geo Background Graphical Glitch (FIXED & VERIFIED on-device):** Samurai Shodown (and other games using 32-tile tall background sprites) had a massive black horizontal band across the middle of the screen. Root cause in `ares/ng/lspc/render.cpp`: `tile` was declared as `n4` (4-bit integer, 0..15). For lines `ry >= 256` (lower half of 32-tile sprites), `tile ^= 0x1f` truncated to 4 bits (`tile ^= 0x0f`), wrapping back to tiles 0..15 instead of accessing tiles 16..31 in VRAM. Changed `tile` to `n5` (5-bit integer, 0..31). Also safeguarded `cartridge.cromMask()` tile wrapping. Verified on-device via screencap: center screen black rows dropped from hundreds to 0, background renders continuously.

**Historical next priority:** Task #49 (N64 save import/export UI), followed
by committing the then-uncommitted Neo Geo and driver-downloader work. This
old sequencing is retained for history; the current priority and dispositions
are in [implementation-plan.md](implementation-plan.md).

## NEO GEO CD (future task — reference)
ares has a dedicated Neo Geo CD fork branch by Luke Usher: `https://github.com/ares-emulator/ares/tree/neogeo-cd`. Closely linked hardware to AES/MVS — once AES/MVS work, pull ONLY the files needed for the NG CD core (CD-ROM hardware + CD audio paths; do NOT pull the whole branch) and port them into `ares/`. Track as a new task after Task #10c lands.

## ALWAYS-READ REFERENCES
- **Canonical roadmap and current dispositions:** [implementation-plan.md](implementation-plan.md).
- **Detailed historical task archive:** [implementation-history.md](implementation-history.md).
- **Performance evidence and ranked investigations:** [performance-audit.md](performance-audit.md).
- **Matched native comparison protocol:** [mario-tennis-benchmark.md](mario-tennis-benchmark.md).
- **Development/change gate:** [development-process.md](development-process.md).
- **README.md** (repo root) — full systems matrix, feature list, build requirements.
- **Prior huge conversation (grep for context):** `/home/garyb/Desktop/agent-mode-conversation.json`.

## Repo Layout & Key Files
- `/home/garyb/LLM-Projects/phobos/android` — Android app Gradle project.
  - `app/src/main/cpp/PhobosRunner.cpp` — Platform bridge: emulation thread, AndroidPlatform (input/video/audio/pak), save import/export, settings sync.
  - `PhobosRunner.hpp`, `PhobosJNI.cpp`, `PhobosCore.kt`, `MainViewModel.kt`, `EmulatorScreen.kt`, `SettingsStore.kt`, `PathSettingsScreen.kt`, `MainActivity.kt`, `TouchControls.kt`, `GameInputState.kt`.
- `/home/garyb/LLM-Projects/phobos/ares` — Native core fork (N64 in `ares/n64/`, parallel-RDP Vulkan in `n64/vulkan/`).
- `/home/garyb/LLM-Projects/phobos/thirdparty/` — parallel-rdp, sljit, adrenotools, volk, etc.

## Historical Build / Deploy / Test Notes

The commands in this section are retained as reported history, not current
instructions. Use [development-process.md](development-process.md) and the
requested flavor's real production path before any future build or device
validation.
- Historical build: `gradle app:assembleDebug`. Historical deploy: IDE deploy module `:app`, `DEFAULT_ACTIVITY`, RUN.
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

## Current Priority Queue

The former queue is superseded. See the authoritative
[open-task inventory](implementation-plan.md#open-task-inventory-and-disposition),
the [ranked roadmap](implementation-plan.md#ranked-current-roadmap), and the
[historical archive](implementation-history.md). In brief: establish the
v336/Phobos APK identities and matched traces first; then measure presentation,
SyncFull waits, pacing, and attribution without changing synchronization.
NGCD-M3 and broad Neo Geo compatibility remain bounded residuals. Task #49,
controller QoL, perf metrics, save/UI work, and the non-N64 performance tasks
remain inventoried/deferred rather than silently dropped.

## Current Working Agreements & Policies

- Read [development-process.md](development-process.md) before changes; its
  exact-snapshot, independent-review, staging, and publication gates supersede
  the historical recipes below.
- Measure first, preserve timing and synchronization defaults, and report
  unknowns explicitly. No blind recompiler, renderer, cache, RSP, or audio
  replacement.
- The abandon path must never take `vulkan.mutex`; preserve save, signing,
  lifecycle, and multi-system behavior.
- Update both [handoff.md](handoff.md) and
  [implementation-plan.md](implementation-plan.md) with evidence, checks,
  limitations, and the next eligible experiment.
- Task #4 audio-buffer reuse is cancelled and is not authorized as the next
  automatic change.

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
