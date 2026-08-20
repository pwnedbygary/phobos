<img src="https://github.com/pwnedbygary/phobos/blob/master/ares/ares/resource/logo%402x.png" width="350"/>

**Phobos** is a multi-system emulator for **Android**, forked from [ares](https://github.com/ares-emulator/ares) (which began development on October 14th, 2004 as a descendant of [higan](https://github.com/higan-emu/higan) and [bsnes](https://github.com/bsnes-emu/bsnes/)). It focuses on accuracy and preservation, but with Android-specific engineering layered on top: a JIT recompiler family, a Vulkan/parallel-RDP renderer for N64, custom Turnip/Adreno driver loading, and a Jetpack Compose UI.

> ares deliberately trades some speed for code clarity (state machines and bitmasks are avoided where possible). Phobos keeps that philosophy for the cores but adds performance-oriented backends around them, so the clarity remains while the hot paths run fast.

---

## Phobos vs. ares — what changed, technically

Phobos is not a UI reskin: it carries substantial core and platform engineering. The main differences:

### 1. JIT recompilers (CPU + RSP)

- **N64 CPU (VR4300):** ares desktop uses an interpreter-only CPU core. Phobos ships a **dynamic JIT recompiler** (`ares/n64/cpu/recompiler*.cpp`, based on the sljit backend) that compiles cached-RDRAM code blocks to native ARM64. It includes custom fixes such as **in-block self-modifying-code invalidation** (tracked via the data/instruction cache dirty-line mechanism) and is gated per-game by a "Recompiler" toggle.
- **RSP:** the RSP has its own recompiler plus an SSE4.1/AVX vector path for the vector unit; on ARM64 the vector ops use the scalar/SSE-style emission paths (`ARCHITECTURE_SUPPORTS_SSE4_1` gates the `__m128i` `r128` union). The RSP recompiler also pins the DMEM base in a callee-saved register so DMEM access folds to register-relative addressing.
- **Synchronization cadence:** the N64 core uses ares' synchronous model (`CPU::synchronize()` drives VI/AI/RSP/RDP directly, the ares co-routine Scheduler is unused by N64 — identical to upstream ares, which never migrated N64 to the scheduler). Phobos tunes `Accuracy::CPU::JitInterleaving` **per-device** (default `2048*2` on the Retroid Pocket 6) to balance sync overhead vs. the JIT's overshoot; upstream's `4096*2` stalls Conker's BFD on-device while `1024*2` costs frames in Mario Tennis.

### 2. N64 rendering: Vulkan + parallel-RDP

- ares' desktop N64 renderer is a software RDP. Phobos replaces it with a **Vulkan backend built on parallel-RDP** (`ares/n64/vulkan/`, vendored `parallel-rdp/`), with:
  - A **command ring + timeline worker + pipeline-compile threads**, pinned to the device's performance cores.
  - **Pipeline cache persistence** (user-configurable path, copy-on-change) so shader compilation doesn't repeat every launch.
  - **Internal upscaling** (1x–4x), **VI post-processing** bypass toggle, **supersample scanout**, and **weave deinterlacing** options.
  - **Non-fatal RDP validation:** a malformed command (e.g. a 4-bit VRAM pointer from a save-state load) is logged and skipped instead of crashing the RDP (upstream aborts). This fixed a hard freeze on some save-state restores.
- The Vulkan `VkDevice` is kept alive across soft resets (the fragile destroy/recreate path is avoided), and bounded waits were added to `CommandRing::drain`, `wait_for_timeline`, and the scanout fence.

### 3. GPU driver loading (libadrenotools / Turnip)

- Phobos can load a **custom Mesa/Turnip Vulkan driver** (e.g. `libvulkan_freedreno.so`) via [libadrenotools](https://github.com/bylaws/libadrenotools) (`thirdparty/libadrenotools`), which the parallel-RDP pipeline can't always use the stock Adreno driver for. The driver is user-selectable per install and applies at app start.

### 4. 64DD support

- The N64 core's **64DD** path is wired end-to-end: firmware scanning maps `fw_n64dd_jp`/`fw_n64dd_us`, a secondary `.ndd` medium can be mounted (JNI `loadSecondaryRom` → native disk mount on the Floppy Disk port), and the pause menu has a "Load Disk" picker. Verified with F-Zero X Expansion Kit at 60fps.

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
- **PS1 multi-disc swap:** the pause menu's Change Disc hot-swaps the disc tray (disconnect → allocate → connect), re-reading the new disc's `cd.rom` + TOC without reloading the console — no more restart for disc 2.

### 7. N64 timing/QoL knobs (Mupen64Plus-FZ style)

- **VI Overclock** (1x–2x): the VI runs at a genuinely higher frame rate; game logic executes faster.
- **Count Per Operation** (1/2/3) and **R4300 Overclocking Factor** (0–5 = 2^f): CP0 Count scaling and peripheral-cycle division, each behind a "use default" checkbox.
- **N64 Debug Logging** toggle: gates the per-second `N64 PC:`/`N64 STALL/HANG` diagnostics (off by default so logs stay clean and no per-frame core-state reads cost CPU).
- Profile counters are compiled out of release builds (`#if !defined(NDEBUG)`).

### 8. CI / distribution

- GitHub Actions builds **release-only** APKs (legacy + modern flavors) on every commit; a version tag (`v1.2.3`) maps to `versionName`/`versionCode` and publishes a GitHub Release. Debug builds are never published.

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
| MSX / MSX2 | ✅ (incl. tape) |
| Atari 2600, ColecoVision | ✅ |
| ZX Spectrum | ✅ Tape loading, on-screen keyboard, gamepad schemes (QAOP/ZXZX/Kempston) — Manic Miner verified |
| SG-1000 | ✅ Verified 2026-08-14 |
| Mega CD | ✅ Audio fixed (lockstep multi-stream mixer, user-verified) |
| PC Engine (HuCard) | ✅ |
| SuperGrafx | ✅ |
| Neo Geo (MVS/AES) | ✅ Graphics + controls fixed (KOF2003 verified @59.2 FPS); **audio works** (KOF2003 confirmed; `ring buffer 0/12000` log is a suspected formatting artifact); per-title compat matrix → [docs/neo-geo-compatibility.md](docs/neo-geo-compatibility.md) |
| **Known broken / under investigation** | |
| PC Engine CD | ❌ Does not boot (PCE HuCard + SuperGrafx work) |

> Sega Saturn and Neo Geo CD are not listed: neither has a usable core (Saturn is
> an ares stub with an empty System::run; Neo Geo CD does not exist in ares). The
> core stubs remain in the tree for future work, but the systems are removed from
> the app and not supported. (PC Engine CD is loaded by the PCE core but does not
> boot on-device — tracked in Known issues.)

### Known issues / not yet functional

- **Neo Geo MVS/AES — audio log artifact** — games boot and render correctly and **have audio** (KOF2003 confirmed on-device). The `ring buffer 0/12000` log line is suspected to be a **string-formatting bug**, not a real audio fault — verify the log formatting. Full per-title status in [docs/neo-geo-compatibility.md](docs/neo-geo-compatibility.md).
- **PCE-CD** — does not boot; PCE HuCard + SuperGrafx work.
- **ZX Spectrum 128K** — gated with a clean "Unsupported" popup (PSG co-routine / scheduler on ARM64, same class as PCE/Neo Geo); 48K works.
- **N64 load-state** — a stale-DMA exception-loop was seen on some titles after restore; RDP validation is now non-fatal so it degrades instead of freezing.

---

## Neo Geo (MVS/AES) emulation

Phobos targets **perfect compatibility** for the Neo Geo MVS/AES library. Notes for users:

- **Scope:** strictly **Neo Geo MVS/AES** — it is **not** a general arcade core. Neo Geo CD is a separate, future core (not in 1.0 scope).
- **"FBNeo + MAME hybrid":** only Neo Geo ROM/driver data and decryption routines are borrowed from MAME and FBNeo (CMC/CMC42/CMC50/SMA/PCM2/PVC, kof2k2-family) to maximize Neo Geo coverage — other arcade boards are not emulated.
- **Current status (2026-08-19):** graphics fixed (sprite zoom tables + vflip/zoom decode, mirroring MAME); P1/P2 input mirror fixed (P1 works; P2 needs a second controller on a handheld); **audio works** (KOF2003 confirmed on-device) — the `ring buffer 0/12000` log line is suspected to be a string-formatting bug, not a real fault.
- **Per-title compatibility matrix:** [docs/neo-geo-compatibility.md](docs/neo-geo-compatibility.md) — 288 titles, categorized by protection (PVC/K2K2/CMC42/CMC50/PCM2/SMA/bootleg/standard) and ranked hardest-first, with Boot/Gfx/Audio/Ctrl status columns.
- **Controls:** default mapping puts A/B on the face buttons and **C/D on R1/R2** (Genesis-heritage bit mapping in `resolveButtonBit`). Per-core + per-game controller rebinding (RetroArch-style 3-tier Global→Core→Game) is a planned task.

---

## Building Phobos

Requires the Android SDK (platform 37, build-tools 36, NDK 28.x, CMake 3.22.1)
and a JDK 17+. From the `android/` directory:

```sh
./gradlew assembleRelease        # release APKs: app-legacy-release.apk + app-modern-release.apk
./gradlew assembleDebug          # debug APKs (also builds all four variants)
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
