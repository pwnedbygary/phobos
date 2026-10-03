<img src="https://github.com/pwnedbygary/phobos/blob/master/ares/ares/resource/logo%402x.png" width="350"/>

**Phobos** is a multi-system emulator for **Android**, forked from [ares](https://github.com/ares-emulator/ares) (which began development on October 14th, 2004 as a descendant of [higan](https://github.com/higan-emu/higan) and [bsnes](https://github.com/bsnes-emu/bsnes/)). It focuses on accuracy and preservation, but with Android-specific engineering layered on top: ARM64 work on ares's N64 recompilers and its Vulkan/parallel-RDP renderer, custom Turnip/Adreno driver loading, and a Jetpack Compose UI.

> ares deliberately trades some speed for code clarity (state machines and bitmasks are avoided where possible). Phobos keeps that philosophy for the cores but adds performance-oriented backends around them, so the clarity remains while the hot paths run fast.

---

## Phobos vs. ares — what changed, technically

Phobos is not a UI reskin: it carries substantial core and platform engineering. The main differences:

### 1. JIT recompilers (CPU + RSP)

- **N64 CPU (VR4300):** ares compiles N64 code with an sljit-based recompiler (`ares/n64/cpu/recompiler*.cpp`); Phobos tunes it for ARM64. Blocks link straight to each other (jumps within and across sections, and not-taken branch edges), loops stay inside their block instead of returning to the dispatcher, an exact idle-loop skip runs while fast-forwarding, and in-block self-modifying code is invalidated through the cache dirty-line mechanism. The CPU Recompiler switch is in the N64 settings and the pause menu.
- **RSP:** the RSP recompiler emits the multiply and multiply-accumulate vector instructions as inline NEON, keeping the accumulator in NEON registers between them; the other vector instructions go through `sse2neon`.
- **Synchronization cadence:** the N64 core uses ares' synchronous model (`CPU::synchronize()` drives VI/AI/RSP/RDP directly; the ares co-routine Scheduler is unused by N64, as upstream). Count/Compare include the clocks run since the last sync, so a game's timer can't fall behind Count and wait a full wrap.

### 2. N64 rendering: Vulkan + parallel-RDP

- ares renders the N64 with **parallel-RDP on Vulkan** (`ares/n64/vulkan/`, vendored `parallel-rdp/`). Phobos adds:
  - A **command ring + timeline worker + pipeline-compile threads**.
  - **Pipeline cache persistence** (user-configurable path, copy-on-change) so shader compilation doesn't repeat every launch.
  - **Internal upscaling** (1x–4x), **VI post-processing** bypass toggle, **supersample scanout**, and **weave deinterlacing** options.
  - An opt-in **Asynchronous RDP** (N64 Experimental; the default stays synchronous), up to 256 render contexts in flight, and binary fences on Turnip, whose emulated timeline semaphores made GPU submits wait.
  - **Non-fatal RDP validation:** a malformed command (e.g. a 4-bit VRAM pointer from a save-state load) is logged and skipped instead of crashing the RDP (upstream aborts). This fixed a hard freeze on some save-state restores.
- The Vulkan `VkDevice` is kept alive across soft resets (the fragile destroy/recreate path is avoided), and bounded waits were added to `CommandRing::drain`, `wait_for_timeline`, and the scanout fence.

### 3. GPU driver loading (libadrenotools / Turnip)

- Phobos can load a **custom Mesa/Turnip Vulkan driver** (e.g. `libvulkan_freedreno.so`) via [libadrenotools](https://github.com/bylaws/libadrenotools) (`thirdparty/libadrenotools`), which the parallel-RDP pipeline can't always use the stock Adreno driver for. The driver is user-selectable per install and applies at app start.

### 4. 64DD support

- The N64 core's **64DD** path is wired end-to-end: the 64DD IPL has Japan, US and development firmware slots, a secondary `.ndd` medium can be mounted (JNI `loadSecondaryRom` → native disk mount on the Floppy Disk port), and the pause menu's Disk action changes disks. Verified with F-Zero X Expansion Kit at 60fps.

### 5. Input, paks, and controller features

- **Rumble Pak / Controller Pak** (Player 1 N64): selectable from the pause menu, hot-swappable, with `save.pak` persistence and Android vibration (polled via JNI).
- **PS1 DualShock** analog toggle (DualShock ↔ Digital Gamepad) at runtime; rumble routed to the Android vibrator with a 150ms latch so short pulses are felt.
- **N64 C-buttons** are reachable from the right stick; stick-axis bindings latch through digital hysteresis matching ares' InputAnalog qualifiers.
- A per-core input cache (`inputButtonCache`/`inputAxisCache`) is keyed by raw node pointers and invalidated whenever the controller node tree is rebuilt (toggle/disk-mount/reload) — fixing stale-binding regressions.

### 6. Platform / save handling

- **Saves Path, Vulkan Cache Path, States/Screenshots** are user-configurable via SAF; internal-storage fallbacks keep saves safe when no path is set.
- Save import runs **before** the cartridge port connects (the cores read their save files from the medium pak at connect time) — a fix that restores GBA SRAM/EEPROM/Flash/RTC state on every load instead of starting blank.
- **GBA RTC detection** was broadened: ares detects RTC by scanning for the literal `SIIRTC_V`; ROM hacks like Pokemon Unbound split the driver marker (`SII\0RTC_V0018\0`), so Phobos also matches the `RTC_V001` prefix (mGBA-style heuristic). Verified: Unbound passes its RTC check.
- **GBA RTC clock is host-seeded:** a fresh S3511A RTC is initialized with the host date/time (not the 2000 epoch), and legacy saves whose RTC year is behind the host year are auto-reseeded on load. Verified: Pokemon Unbound's in-game clock matches the host.
- **Auto-Save State / Auto-Load State:** an always-available "Auto" save-state slot (also reachable from the slot cycler after slot 9, and manually save/load/delete-able) is saved automatically on quit and restored on load when enabled. Both toggles live in Settings and the in-game pause menu, for all cores.
- **N64 save import and export:** the pause menu's Save Data section imports a game's battery save from Mupen64Plus (`.eep`, `.sra`, `.fla`, `.mpk`), RetroArch (`.srm`, compressed or not) or a Phobos backup, and exports it in any of those formats. Mupen64Plus and RetroArch store SRAM and FlashRAM with every 32-bit word reversed, which the conversion undoes. An import first moves the save it replaces, and the game's auto-save state (which would restore the old save), to a Backups folder beside the save, then restarts the game.
- **Multi-disc games:** a game's discs are one Library entry with a disc chooser, and on the PlayStation the pause menu's Disc action hot-swaps the disc tray (disconnect → allocate → connect) with the game paused, re-reading the new disc's `cd.rom` + TOC without reloading the console.
- **Games load from where they are:** a game the app can read by path is read straight from storage (CD images included), with no copy in the app's cache; Phobos keeps saves in its own folder, never beside the ROM.
- **Firmware is matched by content:** Settings → Firmware's Scan Folder identifies each BIOS by the SHA-256 of what the core reads (ares's firmware list, the copies Phobos bundles, MAME's Neo Geo CD BIOSes), whatever the file is called, and each slot shows Verified, Unrecognized, Unreadable or Built-in.

### 7. N64 timing/QoL knobs (Mupen64Plus-FZ style)

- **VI Overclock** (1x–2x): the VI runs at a genuinely higher frame rate; game logic executes faster.
- **Count Per Operation** (1/2/3) and **R4300 Overclocking Factor** (0–5 = 2^f): CP0 Count scaling and peripheral-cycle division, each behind a "use default" checkbox.
- **N64 Debug Logging** toggle: gates the per-second `N64 PC:`/`N64 STALL/HANG` diagnostics (off by default so logs stay clean and no per-frame core-state reads cost CPU).
- Profile counters are compiled out of release builds (`#if !defined(NDEBUG)`).

### 8. CI / distribution

- GitHub Actions builds **release-only** APKs (legacy + modern flavors) on every commit. A version tag (`v1.2.3`) publishes a GitHub Release, and every push to `master` publishes a nightly pre-release; both carry `update.json` for the in-app updater (Settings → About), which installs the newer build of the installed flavor through Android's installer. Debug builds are never published.
- Every build is numbered from git history, locally and on CI alike: version code 100000 + the commit count, version name the tag on a tagged commit and `<tag>-<commits since>-g<hash>` otherwise.

---

## Systems

| System | Status |
|---|---|
| **Verified working on-device** | |
| Nintendo 64 | ✅ JIT CPU + RSP, Vulkan/parallel-RDP, upscaling, 64DD, Rumble/Controller Pak, save states + battery saves confirmed |
| Game Boy Advance | ✅ RTC clock matches host (Pokemon Unbound verified), battery saves + save states |
| Game Boy / Color | ✅ |
| Super Famicom / Famicom | ✅ |
| Mega Drive / Game Gear | ✅ |
| Master System | ✅ |
| PlayStation | ✅ DualShock + analog toggle, memcards, save states, multi-disc swap (MGS verified), Ape Escape opening cinematic verified |
| Neo Geo Pocket / Color | ✅ BIOS settings (language/date) persist |
| WonderSwan / Color | ✅ |
| MSX / MSX2 | ✅ On-screen MSX keyboard (tapes not supported yet) |
| Atari 2600, ColecoVision | ✅ |
| ZX Spectrum (48K and 128K) | ✅ Tape loading with automatic tape control and a faster-loading option, 48K keyboard, control schemes (Kempston, Sinclair, Cursor, QAOP and more) remembered per game, save states |
| SG-1000 | ✅ Verified 2026-08-14 |
| Mega CD | ✅ Audio fixed (lockstep multi-stream mixer, user-verified) |
| Mega 32X | ✅ Six games verified (Knuckles' Chaotix, Virtua Racing Deluxe, NBA Jam TE and others) |
| PC Engine (HuCard) | ✅ |
| PC Engine CD | ✅ Boots through System Card 3.0, read straight from the SD card (Rondo of Blood verified @60 FPS) |
| SuperGrafx | ✅ |
| Neo Geo (MVS/AES) | ✅ Graphics, controls and audio (KOF2003 verified @59.2 FPS); per-title compat matrix → [docs/neo-geo-compatibility.md](docs/neo-geo-compatibility.md) |
| Neo Geo CD | ✅ Boots, renders and has sound (Samurai Shodown verified), with the CDZ's double-speed drive and an optional faster loading speed |
| **Added, not yet tried with a game on-device** | |
| Mega CD 32X, Super Game Boy, Arcade (Aleck64, SG-1000A) | Load paths, firmware slots and touch layouts are in |
| LaserActive (Mega LD, PC Engine LD) | `.mmi` discs read straight from the SD card; both PAC BIOSes boot (60 FPS on the RP6), and Side in the pause menu turns the disc over |
| Pocket Challenge V2 | `.pc2` games with the bundled boot ROM, its own buttons (Circle, Clear, Pass, View, Escape) and touch layout; a test cartridge runs at 75 FPS |

> Sega Saturn is not listed: upstream ares never completed the core (empty System::run stub,
> kept in the tree for future work).

### Known issues / not yet functional

- **MSX tapes** — the MSX's tape deck isn't connected yet, so cassette games don't load.

---

## Neo Geo (MVS/AES) emulation

Phobos targets **perfect compatibility** for the Neo Geo MVS/AES library. Notes for users:

- **Scope:** strictly **Neo Geo MVS/AES** for the cartridge core — it is **not** a general arcade core (the Arcade system runs the other boards ares has, Aleck64 and SG-1000A). Neo Geo CD is a separate core (see the systems table).
- **Ported from MAME:** cartridge decryption and banking for the protected sets (CMC/CMC42/CMC50/SMA/PCM2/PVC, the kof2k2 family), the LSPC zoom table and the RTC protocol come from MAME (BSD-3-Clause, notice in [LICENSE](LICENSE)) — other arcade boards are not emulated by this core.
- **Status:** graphics (sprite zoom tables + vflip/zoom decode, mirroring MAME), controls and audio work (KOF2003 confirmed on-device); player 2 needs a second controller on a handheld.
- **Per-title compatibility matrix:** [docs/neo-geo-compatibility.md](docs/neo-geo-compatibility.md) — 288 titles, categorized by protection (PVC/K2K2/CMC42/CMC50/PCM2/SMA/bootleg/standard) and ranked hardest-first, with Boot/Gfx/Audio/Ctrl status columns.
- **Controls:** Neo Geo default mapping (Xbox-layout reference) — **X→A, Y→B, A→C, B→D, R1→A+B, R2→C+D, L1→B+C, L2→A+B+C, R3→B+C+D** (multi-bit combos). Buttons can be remapped for one game, a console or all consoles from the pause menu's Controller section or Settings → Inputs & Hotkeys.

---

## Starting games from a frontend

ES-DE and Daijisho can start games in Phobos, and Argosy will once its maintainers accept
Phobos's registry entry ([rommapp/argosy-launcher#471](https://github.com/rommapp/argosy-launcher/pull/471));
[docs/frontends.md](docs/frontends.md) has the setup for each. The Retroid Pocket's
own launcher has a fixed emulator list and can't.

---

## Building Phobos

Requires the Android SDK (platform 37, build-tools 36, NDK 28.x, CMake 3.22.1),
a JDK 17+, and the `thirdparty/libadrenotools` submodule
(`git submodule update --init --recursive`). From the `android/` directory:

```sh
./gradlew assembleRelease        # release APKs: app-legacy-release.apk + app-modern-release.apk
./gradlew assembleDebug          # debug APKs for both flavors
```

APKs land in `app/build/outputs/apk/<flavor>/<type>/`.

High-level Components
---------------------

* __ares__:       Phobos' emulator cores and component implementations (ares fork)
* __android__:    main GUI implementation written in Kotlin and C++ featuring a JNI bridge to the Phobos cores.
* __nall__:       Near's alternative to the C++ standard library
* __mia__:        internal ROM database and ROM/image loader
* __libco__:      cooperative multithreading library
* __thirdparty__: parallel-rdp, sljit, libadrenotools, volk

Contributing
------------

Please join my discord [[HERE]](https://discord.gg/EkSNHnmYma) if you have any questions/wish to contribute.
