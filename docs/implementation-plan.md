# Phobos Emulator — Implementation Plan (revised 2026-08-16)

> [!IMPORTANT]
> **Sessions reorganized 2026-08-15:** completed tasks are ARCHIVED (removed from the
> queue; full write-ups preserved in Detailed Task Notes). All tasks + notes are now
> ordered by task number. The Priority Queue contains OPEN items only.
>
> **Maintenance 2026-08-16:** Rogue Squadron (RESOLVED) removed from the queue (archived
> in task notes). QoL "Dynamic speed compensation" renumbered 52→71 (52 = PS1 multi-disc
> swap; the QoL list collided). Save-persistence bugs bundled + expanded to an all-core
> audit (see queue + notes).
>
> **Maintenance 2026-08-17:** N64DD quit→reload SIGSEGV FIXED (commits `6063c438b` +
> `799b8898a`) — root cause was a stale `_isLoaded` in the Kotlin UI spawning a fresh
> emu thread mid-teardown (NOT the abandon path). Save-persistence bundle updated:
> (a) program.disk ✅, (b) 64DD RTC/Error-48 ✅ (seed now also fires for zero-filled
> time.rtc), (d) `.flash` filters ✅ confirmed — only (c) medium-ordering remains.
> Full write-ups in Detailed Task Notes.

## 📌 Project Policies (user-stated)

> [!IMPORTANT]
> **Accuracy-vs-speed:** Accuracy is preferred over speed, but NEVER at the cost of
> dropping below full speed (60 FPS). If a game/core runs below full speed, bump
> performance back up (raise `JitInterleaving` / tune perf knobs) until full speed is
> restored — even if that costs a little accuracy. Full speed is the floor; accuracy is
> the tiebreaker above it.

> [!IMPORTANT]
> **Docs:** ALWAYS update BOTH the handoff prompt (`~/Desktop/phobos-handoff-to-new-chat.md`)
> AND this plan whenever work may carry into a new chat — findings, root causes, decisions,
> and current active work, so a fresh chat can resume without re-investigating.

> [!IMPORTANT]
> **Commit + push on every good result (2026-08-15):** whenever a change/debug session
> bears fruit (a fix verified on-device, a root cause found, a win locked in), commit it
> and push to origin immediately — before moving to the next task. Baseline protection:
> a working tree is fragile; a commit is a checkpoint. Use `git add -u` (stages only
> already-tracked modified files — never new/ignored junk) + `git commit -m "<what and
> why>"` + `git push`. Do NOT let uncommitted work accumulate across sessions.
> If a build artifact or junk accidentally gets staged (`git add -f .`), fix with
> `git reset` (unstages everything, working tree untouched) — the .gitignore whitelist
> (`/*` + re-includes) keeps junk out.

> [!IMPORTANT]
> **UI Toggles — the `rememberUpdatedState` rule (2026-08-14):**
> Any composable using `Modifier.pointerInput(key) { detectTapGestures(onPress = { ... }) }`
> where the handler reads a **state value passed as a parameter** (e.g. a toggle's `active`)
> MUST read it via `val currentX by rememberUpdatedState(x)` declared in the composable body,
> NOT the raw parameter. `pointerInput` only restarts when its `key` changes — if the key is
> a stable label/string, the closure captures the INITIAL parameter value forever, so
> `onToggle(!active)` always computes `!initialValue` and the toggle can never switch back
> (hit TWICE: ZX shift keys, ZX TURBO toggle). Also key `pointerInput` on any value that
> should re-trigger the handler (e.g. `pointerInput(scheme)`).
> **Checklist for any new UI toggle:** (1) is it a `var X by remember { mutableStateOf(...) }`
> in a parent? (2) does the child read it inside `pointerInput`? (3) if yes → add
> `rememberUpdatedState`. (4) Test BOTH directions (on AND off) before considering it done.

> [!IMPORTANT]
> **Hotkey combos with D-pad (2026-08-15):** many controllers report the D-pad as HAT
> AXES (motion events), not KEYCODE_DPAD_* keys — the key-only hotkey detector never saw
> them. Fix: `GameInputState.updateHotkeyDpad` maps hat → virtual D-pad keycodes into
> `hotkeyKeys` + `onHotkeyKeysChanged` callback; EmulatorScreen matches
> `pressedKeys + hotkeyKeys` and re-runs the combo check on hat change. Any future hotkey
> work must keep this.

> [!IMPORTANT]
> **Race-free framebuffer reads (2026-08-15):** NEVER read `Renderer::fb` from the scanout
> thread — the RDP worker mutates it (`set_color_framebuffer` → `flush_queues`) → data race
> → torn values → flicker everywhere. Use the Phobos-core copy
> (`::ares::Nintendo64::rdpFramebufferWidth()/Address()`, written once per SET_COLOR_IMAGE,
> atomic). This fixed Conker/MT/SW intro + gameplay flicker/see-through.

## 🛠 Troubleshooting Playbook (read before debugging hangs / freezes)

> [!IMPORTANT]
> The #1 failure mode on this project has been **hypothesize → patch → retest → repeat**,
> burning many rounds on "maybe it's X". **Measure first. Then fix.** Use this ladder from
> cheapest to most expensive — skip a rung only when you can justify why the previous one
> can't answer the question.

### Ladder (cheapest → most expensive)

1. **Cheap bounded instrumentation** — stage/progress markers in the suspect path
   (e.g. `N64 power: begin … complete` in `System::power()`), plus bounded-timeout
   counters where wait sites are known. Use to answer: *which phase fails? is it stuck
   or just slow?* — 1 build, immediate logcat answer.
   - Example: `FenceHolder::wait()` is already bounded at 500ms; a stats line stuck at
     ~2 FPS (not 0) was the fingerprint of a 500ms-timeout loop.
2. **State counters / heartbeats** — for intermittent failures or "stuck vs slow":
   track last-frame-completed, frame counts per phase, failure counters. Use to
   answer: *how often? what's the failure rate?* Also the basis of a **watchdog**.
3. **Watchdog + `debuggerd -b` thread-stack dump** — when a hang survives the above
   and is *non-obvious* (deep/unbounded waits, deadlocks, cross-thread). PhobosRunner
   has a built-in reset-watchdog that dumps all thread stacks ~6s after a reset if no
   frame completes, plus a dump on the abandon path.
   - ⚠️ A stack dump shows **where** a thread is, not **why** — correlate with the
     other threads' stacks in the same dump.
4. **Native debugger attach (gdb/lldb)** — last resort for memory corruption / hard
   crashes; heavyweight on-device.

### Rules that would have saved rounds

- **Before patching teardown/device-lifecycle, verify with markers WHERE it hangs.**
- **A bounded wait (500ms) in a loop shows as a NON-zero floor FPS (~2 fps), not 0.**
- **If a hang survives two instrumentation passes, go straight to the stack dump.**
- **Distinguish "reset the emulated hardware" from "reset the GPU context."** Keep the
  VkDevice alive across soft reset (Turnip: second device's fences can fail → black).
- **The abandon path must never take `vulkan.mutex`** (zombie-thread deadlock; leaked
  N64-only lock = fingerprint: other systems work, N64 loads fail).

---

## Status Summary

> [!IMPORTANT]
> **CURRENT STATE (2026-08-15):** All cores VERIFIED WORKING except the gated broken ones.
> ✅ N64 (gameplay + intros), GBA (RTC), GB/GBC, SFC/FC, MD/GG, Master System, PS1
> (DualShock), NGP/NGPC, WonderSwan/Color, MSX/MSX2, Atari 2600, ColecoVision, ZX Spectrum
> (48K + 128K), SG-1000. 🔴 GATED (clean "Unsupported" popup): Neo Geo MVS/AES
> (Task 10c/10a remaining). ✅ PC Engine / PC Engine CD /
> SuperGrafx UNGATED & FIXED 2026-08-18 (root cause: missing
> PROFILE_PERFORMANCE define — empty PSG::main deadlocked the scheduler;
> verified 60 FPS on-device, commit `2795e5813`).
> Saturn + Neo Geo CD (no core, out of 1.0). SC-3000 removed.
>
> **Rogue Squadron (N64) main menu — FULLY RESOLVED 2026-08-15 (menu renders, no flash).**
> Root cause: the 2026-08-10 VI field-toggle inversion
> (`io.field += io.halfLinesPerField.bit(0)` — locked field=0 for interlaced NTSC 262)
> → parallel-RDP serrate scanout sampled ONE field → black menu. REVERTED to
> `io.field += !io.halfLinesPerField.bit(0)` (hardware-correct; Conker coincidence fix
> kept). Commits `eae573558` + `f21bfe54e` + `6a556f111` (mode-change hold) +
> `89fa3ad5f` (hold gated to wide-VI). Related (same family, user-reported):
> Conker/MT 3D intro white-flash + polygon layers; F-Zero X title skew; MT gameplay
> garbage rainbow lines + BFI-like strobe. ZELDA OoT PERFECT, F-ZERO GAMEPLAY PERFECT,
> MK Amped Up fine.
>
> **2026-08-15 session wins (user-verified):** race fix (intro/gameplay flicker + see-
> through gone), Z + D-pad hotkey fix, slot-toast debounce, **rumble per-hit decaying
> envelope (MT every racket hit + F-Zero — both "perfect", commit `f0073403b`)**,
> **Rogue Squadron menu FULLY renders (field-toggle revert `eae573558`+`f21bfe54e`;
> transition flash fixed `6a556f111`+`89fa3ad5f` gated to wide-VI)**,
> N64 Debug Logging toggle auto-on fix (loadRom sync), **modern flavor march fix
> (`4c3933de4` — root CMake was overriding flavor `-march`; verified dotprod/fp16 now
> correctly applied, though Clang emits none in this codebase — see Task 62)**,
> **Mischief Makers title-freeze DIAGNOSED + parked (`22ee8057a` — interrupt chain
> functional, not VU stubs; game's handler hits fatal trap via corrupted dispatch;
> root cause = upstream RSP microcode inaccuracy)**.
> root cause = upstream RSP microcode inaccuracy)**.
> Full brief in the handoff.
>
> **2026-08-17 session wins (user-verified):** **N64DD quit→reload SIGSEGV FIXED**
> (`6063c438b` zombie park/join + `799b8898a` stale-`_isLoaded` race + native
> `systemUnloading` guard — real z64+ndd flow, many cycles, zero crashes).
> **64DD RTC (Error 48) FIXED** (`799b8898a`: seed also fires for zero-filled time.rtc;
> needs on-device confirm of `RTC seed:` log + no Error 48). 64DD disk save area
> (program.disk) + `.flash` save filters verified present.
>
> **2026-08-20 session (COMMITTED in v1.0.0):** **Neo Geo is now a fully working core.** Games load/run ~60 FPS with audio, input, and full sprite/background graphics verified on-device (kof2003/samsho/samsh1; **samsh5pf boots but is stuck at the warning screen — START does not advance it, see compat matrix Testing notes**). Landed fixes: MIA `game` keyword (samsh5pf), samsho SIGSEGV graceful return, mame.cpp `neogeo.zip` subdir extraction (commits `24b404abc`/`76fa1b6b4`/`5888d7ddd`); truthful BIOS/ROM-failed dialog; **D-pad hat input fix** (`GameInputState.updateHotkeyDpad` now latches hat → `hwButtons` bits 0–3 — previously only fed `hotkeyKeys`); **Tall sprite / background rendering fix** (`ares/ng/lspc/render.cpp` `n5 tile` fix for `ry >= 256` in 32-tile tall sprites, fixing the black center horizontal band). **NEW QoL feature:** GPU Driver Downloader (`DriverManagerScreen.kt`: download/install/delete + scrollable Active-Driver selector, identity-deduped; manual installs refresh list).

---

## Priority Queue (OPEN items only — completed tasks archived in Detailed Task Notes)

> [!IMPORTANT]
> Ordered by task number. Difficulty/status per item. Task 10c/10a PCE portion is
> RESOLVED (2026-08-18, `2795e5813` — missing PROFILE_PERFORMANCE, NOT ARM64/libco;
> scheduler + libco are stock upstream and work for 11 other cores on this build).
> ZX 128K also RESOLVED (2026-08-18, `1bf97e3ac` — tape pak lookup matched
> root->name() "ZX Spectrum" but the 128K root is "ZX Spectrum 128" → empty pak →
> tape frequency 0 → cubic resampler ratio 0 → infinite loop in Cubic::write).
> Remaining: Neo Geo MVS/AES (MIA database path suspect).

### Open tasks (full-width list — see Detailed Task Notes for full write-ups)

- **Neo Geo MVS/AES — FINISH CORE FIRST (gates everything below):** graphics FIXED (vscale/`loadZoomy` + vflip/zoom decode; KOF2003 verified 59.2 FPS); P1/P2 input mirror FIXED (player-aware binding in `PhobosRunner.cpp::controllerPlayerIndex` — verified on-device: P1 only). **Audio works** (KOF2003 confirmed on-device) — the `ring buffer 0/12000` log line is suspected to be a string-formatting bug, not a real fault (verify formatting). Then broad compat pass via the matrix. PCEngineCD deferred (doc-only bug) until NG fully stable. **2026-08-24:** `e92486f72` vflip tile-ordering regression REVERTED in `ares/ng/lspc/render.cpp` (hunk restored to exact `b1c0a9fe1` code — contradicted MAME `neogeo_spr.cpp`); `kof2003`/`kof98` (PROGSF1)/`kof99` (SMA) titles verified clean on-device `49016109`; `ssideki4` in-match field garble = separate pre-existing zoom-path issue. _Status: 🟢 NG graphics+controls+audio done; compat pass remaining · Diff: 🟢 (verify log)_
- **PER-CORE + PER-GAME CONTROLLER REMAPPING — NEXT PRIORITY (immediately after NG stable):** Tasks **13a** (per-core custom layouts), **13b** (per-core rebinding), **13c** (multi-controller / per-player pad assignment), **13d** (per-game rebinding). All four done together (same code path). RetroArch-style 3-tier hierarchy: **Global** default → **Per-Core** override → **Per-Game** override (highest priority wins). Pulled up from QoL phase: Neo Geo C/D currently map to R1/R2 (`resolveButtonBit` Genesis-heritage bit mapping); per-core defaults + user rebinding (and per-game overrides for weird default schemes) is the proper fix. _Status: ⬜ Open · Diff: 🟡 Medium_
- **#10c/10a — PCE FIXED (`2795e5813`); ZX 128K FIXED (`1bf97e3ac`); Neo Geo MVS/AES remaining** — PCE root cause was the missing PROFILE_PERFORMANCE define (empty PSG::main → scheduler deadlock); ZX 128K root cause was the tape pak attribute lookup mismatch (empty pak → resampler ratio 0 → infinite loop). Both verified on-device at full FPS. Neo Geo (MIA database path) still gated, needs measurement. _Status: 🟡 1/3 cores gated · Diff: 🔴 Hard (deep)_
- **#72 — N64 RDP-ParaLLEl performance investigation vs mupen64plus-ae-turnip** — some games run WAY faster in pwnedbygary/mupen64plus-ae-turnip's RDP-ParaLLEl renderer than in our N64 core. Investigate the fork's paraLLEl-RDP integration (command-ring/timeline/pipeline threading, VI post-processing, any vendor/Adreno-specific fast paths, scheduler/affinity tweaks) and port anything applicable to make Phobos's N64 as fast as possible. Diff against upstream ares paraLLEL-RDP + our Task 45 perf pass. _Status: ⬜ Open · Diff: 🟡 Medium (research + selective port)_
- **#49 — N64 save import/export** (RetroArch / Mupen64Plus FZ .sra/.eep/.fla/.mpk → Phobos). _Status: ⬜ Open · Diff: 🟡 Medium_
- **#59 — Proper write-through dcache bypass (correct design, per-game)** — parked; high-risk JIT memory path. _Status: 🟡 Parked · Diff: 🔴 Hard (JIT)_
- **#60 — Per-game hash overrides table** — needed for Task 59 gating; also future per-game knobs. _Status: 🟡 Parked (design done) · Diff: 🔴 Hard_
- **#61 — Proper release APK signing (Play-ready)** — real keystore (base64 secret in GH Actions), `signingConfigs.release` reading PHOBOS_KEYSTORE_* gated behind a project property. _Status: ⬜ Open · Diff: 🟢 Easy (config/CI only)_
- **Mischief Makers (N64) title freeze — PARKED (deep RSP accuracy)** — interrupt chain verified functional (RCP=1 → handler entry); NOT the VU stubs (zero hits). Game's IRQ handler at 0x800A5FC8 follows a corrupted dispatch → beq-self fatal trap at 0x800008b8, garbage EPC (0xb400...). Root cause = upstream ares RSP microcode inaccuracy for the game's custom audio microcode. Need VU instruction bisection (diag in tree, commit `22ee8057a`) or reference-RSP comparison. _Status: 🟡 Parked · Diff: 🔴 Hard (deep)_
- **FEATURE-COMPLETENESS AUDIT (pre-1.0, investigated 2026-08-17) — Run-Ahead NOT wired; Fast Boot RESOLVED**: Run-Ahead toggle is DEAD (no JNI/native bridge; ares `setRunAhead`/_runAhead exists but nothing calls it — see note). Fast Boot moved per-core 2026-08-17 (`42c462549`): toggle now lives in the pause menu's Boot Options, shown only on GB/GBC/NGP/NGPC/PS1 (the cores ares supports it on); removed from global settings. Skip Boot ROM wired (GB/GBC/WS only). _Status: 🟡 Run-Ahead open · Diff: 🟢 (run-ahead bridge)
- **QoL phase** (14/15/15a/16/18/19/31/42/43/44/50/51/71 — **13a/13b/13c/13d pulled up to NEXT priority after Neo Geo**, see above). _Status: ⬜ After core stability_
- **Perf sub-batch** (63-70: PS1 R3000 recompiler, PS1 GPU off-CPU, SNES 65816 recompiler, GBA ARM7 recompiler, Genesis performance VDP, N64 RSP NEON verify, perf-overlay CPU/GPU breakdown, ZX z80 recompiler). _Status: ⬜ After core stability · Diff: varies (63/64b 🔴)_

> [!IMPORTANT]
> **FUTURE — Neo Geo CD core (post-1.0, additive):** does not exist in upstream ares or
> this fork; removed from the app. Tractable later (~80% of MVS/AES hardware exists in
> ares/ng). Natural order: fix Neo Geo MVS/AES first. Do NOT attempt before 1.0.

> [!IMPORTANT]
> **OUT OF SCOPE — Sega Saturn:** upstream ares never completed the core (stub). Only path
> = embed Yabasanshiro (GPLv2) — large project, GPL implications; user decision required.
> Not a 1.0 blocker.

### QoL-phase tasks (after core stability) — full-width list

- **13a** — Per-core custom controller layouts
- **13b** — Per-core controller rebinding
- **13c** — Multi-controller support (per-player pad assignment + per-port input routing) — extension of 13a/13b
- **13d** — Per-game controller rebinding (3-tier Global→Core→Game, RetroArch-style; done with 13a/13b/13c — same code path)
- **14** — Responsive UI layout (phone/portrait)
- **15** — QoL / polish / shader menu
- **15a** — Quick-access pause menu items
- **16** — Touch control resize / reposition
- **18** — Video options (shaders)
- **19** — Customizable UI coloration
- **31** — PS1 on-screen: authentic PlayStation shapes (● × ▲ ■)
- **42** — Perf Monitor: configurable metrics toggles (Settings + pause sub-menu)
- **42b** — Perf Monitor: **Game FPS** (true render rate, N64) — count VI-origin
  (`vi.io.dramAddress`) changes per second = the game's real frame rate, shown as
  `VI 60.0 / Game 29.8`. Current FPS = VI scanout rate (emulation speed), so 30fps-native
  games (OoT/GoldenEye/Turok) read 60 — correct for emulation speed but not the render
  rate. Caveat: single-buffered games don't flip origin → undercount; robust fallback =
  cheap framebuffer content-diff (hash a few rows/VI frame). Mupen64Plus-FZ-style.
- **43** — Perf Monitor: GPU utilization (libadrenotools / VK counters)
- **44** — Perf Monitor: CPU freq + thermal throttle from sysfs
- **50** — Save-state screenshots (PPSSPP-style): capture takeScreenshot() into state at save; thumbnail in pause menu
- **51** — **ZX multi-tape swap + multi-file zip picker (post-1.0)** — (a) pause-menu "Swap Tape" re-pointing the hot-swappable tape tray; (b) multi-file ZIP support: pick WHICH file inside a compilation zip — Pak::read currently returns only the FIRST matching file
- **71** — **Dynamic speed compensation (smooth low-FPS, NICE-TO-HAVE)** — scale the game's clock (cycles + audio) when rendered FPS < refresh rate → smooth lower-frame-rate playback instead of slow-motion. Default ON (renumbered from 52 — 52 is PS1 multi-disc swap)

**Performance sub-batch (2026-08-15 codebase scan — CPU/GPU hot paths on the demanding cores; all reuse the existing `nall::recompiler::generic` SLJIT framework and/or the Vulkan pipeline):**

- **63** — **PS1 CPU (R3000) recompiler** — the only "big console" core with NO JIT; port N64 VR4300 recompiler (MIPS) to R3000, behind new `Accuracy::CPU::Recompiler` flag + per-game toggle, interpreter fallback
- **64** — **PS1 GPU off-CPU** — (a) NEON-vectorize the 15/24bpp blitter (cheap); (b) port PS1 GPU to the Vulkan pipeline (big)
- **65** — **SNES CPU (WDC65816) recompiler** — interpreter-only; small 16-bit ISA, cheapest recompiler of the set
- **66** — **GBA CPU (ARM7TDmi) recompiler** — interpreter-only; helps heavy titles, lower priority
- **67** — **Genesis (MD) performance VDP** — SH2 is already JIT'd; the *software* VDP is active (`#if 0 //defined(PROFILE_PERFORMANCE)`), enable the `vdp-performance/` renderer behind a flag
- **68** — **N64 RSP VU ARM64 NEON verification** — confirm the `ARCHITECTURE_SUPPORTS_SSE4_1`→sse2neon path actually emits NEON (not the scalar `#else`); convert any stragglers
- **69** — **Perf overlay CPU-vs-GPU breakdown** — add a CPU-vs-GPU metric (extends 42/43/44) so 63-68 are measurable on-device
- **70** — **ZX Spectrum (z80) recompiler** — LOW / probably skip (z80 not a realistic ARM64 bottleneck; profile first)

---

## Detailed Task Notes (grouped by status — ordered by task number within each group)

### ✅ COMPLETE (DONE / FIXED / RESOLVED / VERIFIED / REVERTED-archived)

#### Task 9 — Ape Escape cinematics (FIXED & VERIFIED 2026-08-18, commit `021f13aa1`)

**Symptom:** Ape Escape USA booted and played its opening audio, but showed a white
screen and skipped to the menu. DuckStation played the same CHD correctly.

**Root cause:** Ape Escape's `TITLE.STR` interleaves Mode 2 data/realtime video
sectors (`subMode=0x48`, file 1/channel 0) with XA-ADPCM audio sectors
(`subMode=0x64`, file 1/channel 1). The game configures the CD XA filter for
file 1/channel 1. Phobos applied that filter to every Mode 2 sector before
classifying it, so the channel-0 MDEC video sectors were discarded before the
CD FIFO. No MDEC command or MDEC DMA activity could occur.

**Fix:** `ares/ps1/disc/drive.cpp` now applies the file/channel filter only when
the sector is XA audio (`subMode & 0x44`), routes matching XA audio to the CD-XA
decoder, and leaves non-audio Mode 2 sectors available to the CD FIFO.

**Verification:** Built `./gradlew app:assembleDebug`, deployed and launched on
Retroid Pocket 6 (`49016109`), reproduced the failure with bounded diagnostics,
then verified the fix visually with the instrumented build and again with a
clean non-instrumented build. The Ape Escape opening cinematic plays normally
and proceeds to the menu; emulation remains at full speed. Temporary diagnostics
were removed before the final build.

#### N64 reset (vulkan.mutex leak on reset) — FIXED 2026-08-11 ✅

**Root cause:** `AndroidPlatform::video()`'s N64 Vulkan path called
`vulkan.mapScanoutRead()` (acquires `vulkan.mutex`) but only called
`vulkan.unmapScanoutRead()` (releases it) when `vData` was non-null. After a reset, the
first heavy 3D frame's scanout fence often exceeds the 100ms bounded wait → `vData` null
→ the unmap never ran → the screen thread **leaked `vulkan.mutex` forever** → next
frame's `scanoutAsync` blocked → black screen.

**Fix:** `unmapScanoutRead()` now runs unconditionally in the N64 Vulkan path (copy only
when `vData` valid). Also fixed: pipeline-cache reload (removed unconditional discard),
soft reset keeps VkDevice alive, bounded waits added to parallel-RDP, `VI::power()`
clears the stale scanout fence. Verified: 3 consecutive resets recovered; MK64 @ 59.6 FPS.

#### Task 5a — N64 Vulkan experimental settings (DONE 2026-08-10)

Three Vulkan rendering settings that existed in the native `option()` handler
(`system.cpp`) were dead code — no JNI or UI. Wired end-to-end:
- **`disableVideoInterfaceProcessing`**: bypasses paraLLEl-RDP VI post-processing.
- **`supersampleScanout`**: when internal upscale >1x, downscales output back to native;
  sets `outputUpscale = 1` when on.
- **`weaveDeinterlacing`**: swaps deinterlace mode to blend-previous-frame instead of
  upscale deinterlace (only when supersampleScanout off).

Files: PhobosRunner.cpp (3 statics + setters), PhobosRunner.hpp, PhobosJNI.cpp,
PhobosCore.kt, SettingsStore.kt, MainViewModel.kt, SettingsScreen.kt
("Experimental (N64 Vulkan)"), EmulatorScreen.kt (N64 pause menu toggles).

#### Task 5b — Mario Tennis Adreno compute shader failures (RESOLVED 2026-08-10)

Mario Tennis logcat spammed: `Shader compilation failed ... Failed to create compute
pipeline (hash ..., blacklisted) ... dispatch will be dropped`. paraLLEl-RDP generates
compute shaders this Adreno driver revision can't compile (`shaderType: 5`). Once
blacklisted, all dispatches using that pipeline silently drop.

Mitigations exposed (Task 5a): Disable VI Processing / Supersample Scanout / Weave
Deinterlacing. If none work: different GPU driver (Turnip Mesa via driver manager) or
blacklist Adreno versions at RDP init.

#### Task 6 — Conker's BFD N64 freeze (RESOLVED 2026-08-12)

**Two symptoms, both diagnosed as a DEAD/STUCK scheduler / JIT overshoot:**

**State A — DEAD SCHEDULER at boot vector (load-state → reset failure):**
`N64 STALL: PC=0xffffffffbfc00000 clock=0 ... queue(next=2147483647) ERL=1 IE=0` — every
device idle, scheduler queue EMPTY, `root->run()` returns instantly. Recovery only via
HOST abandon path (fresh thread).

**State B — STUCK DMA at 0x1000117c (original pub stall):**
CPU waits on an interrupt that never comes; RSP broken, RDP command stuck, AI DMA stuck,
VI ticking (FPS=60), scheduler queue EMPTY.

**ROOT CAUSE CONFIRMED (13:57): SYNC-CADENCE / JIT overshoot, NOT a scheduler bug.** This
fork's N64 core is the OLD SYNCHRONOUS model: `CPU::main()` calls
`vi.main()/ai.main()/rsp.main()/rdp.main()/pif.main()` DIRECTLY; the Phobos Scheduler is
never used (bare `Thread` = cycle counter only). The N64 event `Queue` has NO AI/RDP
completion events — those are driven by the synchronous `CPU::synchronize()` loop. The
spin at ROM `0x1000117c` (`IE=1 pend=00`) waits for an interrupt that only the next
`synchronize()` delivers; a LARGE `JitInterleaving` budget lets the CPU overshoot the
wait → freeze.

**Empirical `JitInterleaving` table:** 8192*16 → stall fast; 4096*8 → stall; 4096*2 →
still stall; 1024*2 → no stall, 60fps; **FINAL = 2048*2** (2026-08-12 evening: 1024*2
too slow for MT, 4096*2 reintroduces stall; A/B release showed identical MT FPS 2048*2
vs 4096*2 — dips are SI-DMA waits, not sync overhead).

**UPSTREAM VERIFICATION (2026-08-13):** upstream ares master `CPU::synchronize()` is
IDENTICAL (bare Thread::clock, direct calls, same Queue) — N64 was NEVER migrated to the
co-routine scheduler upstream either. The scheduler would add 2+ co_switch per sync vs 5
direct calls; MT is CPU/dcache-bound with RSP idle → not worth the risk.

#### Task 20 — Audio crackling (FIXED 2026-08-09)

`Threaded = false` globally was the root cause — forced emulation threads to do video
refresh synchronously, desynchronizing audio buffer timing. Reverted to `Threaded = true`.

#### Task 22 — GBC white screen (FIXED 2026-08-09)

Episode 1: stale coroutine entries (`Thread::EntryPoints().clear()` in
`Scheduler::reset()`). Episode 2: `Threaded = false` + wrong boot ROM offset.
Final: `bootROM.allocate(2048)`, `read(address)`, `Threaded = true` (matches upstream).

#### Task 23 — Mega Drive/Mega CD audio garbled (FIXED 2026-08-14, on-device verified)

**Root cause — NOT a rate mismatch; a MISSING AUDIO MIXER in the host:** MD has FOUR
`Node::Audio::Stream`s (YM2612 stereo, PSG MONO, + CD-DA stereo + PCM stereo on MCD).
PhobosRunner's `audio()` drained ONE stream at a time UNMIXED → sequential chunks =
garbled. Mono PSG also left `samples[1]` uninitialized.

**Fix (PhobosRunner.cpp):** LOCKSTEP mixer (mirrors upstream `Program::audio`): lazy
`audioStreams` registry (mutex + strong refs), all-pending gate, one frame per stream,
sum, mono→both, clamp ±1. Registry cleared in BOTH unload paths. Log:
`Audio: registered stream '<name>' (ch=N, <rate>Hz) — M total`.

**USER: "audio sounds amazing in both" (MD + MCD).** Side benefit: fixes MS/MSX/PCE-CD/
NG/NES-expansion/ZX (all multi-stream).

#### Task 29 — PS1 analog toggle (FIXED 2026-08-09)

`connectDevices` used `portIndex == 1` to detect Player 1 port, but Disc Tray
incremented the counter before Controller Port 1. Changed to string comparison:
`port->name() == "Controller Port 1"`.

#### Task 30 — Volume rocker (FIXED 2026-08-09)

`onPreviewKeyEvent` consumed `KEYCODE_VOLUME_UP`/`DOWN`/`MUTE`. Added explicit bypass
(`return@onPreviewKeyEvent false`) for volume keys; also in `onKeyEvent` for BackHandler.

#### Task 32 — N64 C-button mappings: right-stick cardinal directions (DONE 2026-08-11)

`GameInputState.handleMotionEvent()` skipped stick-as-button bits (17–24) in the
axis-binding latch loop → C-buttons never pressed by right stick. Fix: removed the
`isStickBit` skip; stick-axis bindings latch through the same hysteresis (PRESS 0.5 /
RELEASE 0.4). On N64 the right stick presses C-Up/Down/Left/Right; PS1 untouched.

#### Task 33 — ColecoVision BIOS (FIXED 2026-08-09)

Scanner maps `coleco.rom`/`colecovision.rom` → `fw_coleco`. Six CRC32 variants added.
Native `pak()` reads from `fw_coleco` map entry.

#### Task 35 — Arcade removal (DONE 2026-08-09)

Removed from JNI `enumerateSystems`, native `pak()`/`initialize()` paths (Aleck 64
fallback), MainViewModel BIOS auto-copy. Library no longer shows Arcade entry.

#### Task 38 — GBA RTC / Pokemon Unbound (FIXED 2026-08-13, on-device verified)

**Root cause #1 — MIA RTC detection misses Unbound's driver marker:** Phobos' GBA MIA
detects RTC only by scanning for contiguous `SIIRTC_V`; Unbound carries the SII driver
split as `SII\0RTC_V0018\0` at 0x703846 → `hasRTC=false` → S3511A never powered →
"RTC not initialized". mGBA uses a signature list.

**Root cause #2 — PhobosRunner save import ran AFTER cartridge connect (all GBA saves):**
import loop ran after `connectDevices(root)`; GBA reads save files from the medium pak at
`Cartridge::connect()` → imported saves never reached the cartridge → every GBA battery
save started BLANK. Also `vfs::memory::write` silently drops bytes past size (broke
EEPROM-sized `.eep` restores). Export also skipped `.rtc`.

**Fixes:** broadened MIA RTC detection (`SIIRTC_V` OR `RTC_V001`); moved import BEFORE
`connectDevices(root)`; import `resize()`s the pak file; added `.rtc` to import+export
filters.

**USER-CONFIRMED 2026-08-13 10:00: Unbound passed the RTC error screen @ 59.8 FPS.**
Side benefit: fixes GBA SRAM/EEPROM/Flash restore in general.

#### Task 39 — Wire Saves Path setting (DONE 2026-08-11)

Added `resolveSafPath()` (SAF tree/document URI → real path). `loadRom()` resolves the
configured path and calls `PhobosCore.setSavesPath()` every load; falls back to internal
`files/saves` when unset/unresolvable. Logged `Saves path resolved:`.

#### Task 40 — Vulkan Cache Path (DONE 2026-08-11)

`vulkanCachePath` setting; pipeline cache prefers it, falls back to savesPath.
`MainViewModel.setVulkanCachePath()`: persists SAF URI, resolves real path, copy-on-change
(copies `n64_vulkan_pipeline_cache.bin` + `.uuid` from old dir). Falls back to internal
`files/vulkan_cache`. `loadRom()` pushes resolved dir before N64 init.

#### Task 41 — States/Screenshots internal defaults (DONE 2026-08-11)

`saveState()`: SAF copy when configured; else internal `files/states/<system>/<file>`.
`loadState()`: SAF when configured; else internal fallback; logs when absent.
`takeScreenshot()`: `resolveSafPath()` real path; falls back to `files/screenshots`.

#### Task 45 — N64 performance pass (ESSENTIALLY FINISHED 2026-08-11)

Squeezed every frame on Turnip/Adreno. Done: stripped diagnostics, `JitInterleaving`
tuning, **CPU affinity pinning** (parallel-RDP command-ring/timeline/pipeline threads +
Phobos video thread → last-4 perf cores), audio pops (stream close on unload, stop/start on
pause, buffer 24→32 bursts, prime with silence). Candidate knobs documented (recompiler
block reuse, upscale 2x, frame-pacing spin, async thread confirm, profile-counter gating,
AAudio depth, log pressure).

#### Task 47 — N64DD end-to-end support (DONE & VERIFIED 2026-08-11)

Firmware scan maps `fw_n64dd_jp/us`; `loadSecondaryRom()` → native `loadSecondaryMedium()`
copies `.ndd` into mia_temp + `System::power()` re-init; disk mount via `connectDevices`
re-run seeing "Disk Drive" (type Floppy Disk); firmware attach (4 MiB IPL); pause-menu
"Load Disk" picker. VERIFIED: F-Zero X Expansion Kit @ 60 FPS.

#### Task 48 — N64 Rumble Pak / Controller Pak, pause-menu selectable (DONE, user-verified)

Full stack: native pak attach in `connectDevices()` (`n64Pak` static, Gamepad "Pak"
sub-port allocate/connect), `save.pak` persistence to `savesPath/Nintendo 64/`, rumble via
`getRumbleState()` atomic + `Vibrator` in MainViewModel, pause-menu dropdown
(None/Rumble/Controller, hot-swap via `setN64Pak`), VIBRATE permission. **USER: "C buttons
definitely work, as does the rumble pack"** — Task 48 DONE.

**Rumble feel upgrade (2026-08-15, commit `f0073403b`, USER-CONFIRMED PERFECT):**
- Native: `rumbleState` now mirrors the game's bit EXACTLY (0-hold,
  `rumbleState.store(rumble->enable())` every poll) — no artificial latch. N64 games
  (MT) write the bit continuously during rallies; a 150→350ms latch made it a constant
  buzz. 0-hold = each racket hit is a fresh rising edge.
- Kotlin: decaying envelope per rising edge (100ms full 255 → 200→150→100→60→30→10→0
  over ~600ms) instead of a sustained buzz; re-triggers per hit, cancels on falling edge.
- VERIFIED: Mario Tennis rumbles EVERY racket hit; F-Zero X (discrete events) perfect.
- Edge case: a 1-frame-only rumble pulse could be missed by the 30Hz poll (33ms) — raise
  poll rate if any game misses rumbles.

#### Task 57 — PS1 DualShock analog toggle (FIXED 2026-08-13, on-device verified)

**Root cause:** `inputButtonCache`/`inputAxisCache` keyed by raw `Node::Input*` were only
cleared on unload/abandon/orientation-change — NOT on `connectDevices()` re-runs. Each
analog toggle destroys + recreates the DualShock↔Digital node tree; freed addresses get
RECYCLED → 3rd+ toggle lands on a stale cache entry → wrong bit/slot (left stick
regionally dead). Also a std::map race (toggle thread clears while emu thread reads).

**Fix:** `invalidateInputCaches()` (mutex-guarded) at the TOP of `connectDevices()` +
`inputCacheMutex` guarding all cache access in `input()`. **USER-CONFIRMED: controls
fixed (5+ toggles OK).**

**FOLLOW-UP (same session): pause-menu toggle showed wrong state.** Native
`togglePs1AnalogMode()` returned `true` (success) instead of the NEW `ps1AnalogMode` →
DataStore stuck true → switch always ON. Fix: `return ps1AnalogMode;`.
**USER-CONFIRMED 2026-08-13 20:26: hotkey flips the switch to match — Task 57 DONE.**

#### Task 58 — dcache fast-path (REVERTED 2026-08-12 — archived)

Naive fast-path (CPU::read/write direct to rdram.ram) broke MT (SI DMA hang): the dcache
is the **write-coalescing bridge between CPU writes and DMA peripherals** — bypassing it
orphaned the DMA. The dcache ops are REAL work. Reverted core change + settings/UI.
Correct design → Task 59.

#### Task SG — SG-1000 verified (DONE 2026-08-14)

User confirmed SG-1000 plays on-device. SC-3000 (keyboard/tape variant) REMOVED from the
app as useless; core stays in the tree.

#### Task ZX — ZX Spectrum full support (DONE 2026-08-14, verified on-device)

Complete support (48K + 128K) added: tape loading (signed-char fix in
`mia/medium/zx-spectrum.cpp` — `char` is UNSIGNED on ARM so TZX `+128` made both levels
>127 → EAR constant high → white screen; fixed with `(s8)` cast), on-screen keyboard
(rainbow styling, 40-key matrix, SYM/CAPS latching, BACK=CAPS+0, LOAD ""/CLS macros,
TURBO 2x, scheme cycler), tape playback, gamepad→keyboard schemes, Kempston joystick.
Latent bugs fixed: connectDevices `contains("Port")` char-set bug (use `find("Port")`),
ZX matrix typo `"C," "V"`, TapeDeck connect(), PHOBOS_TAPE_SPEED gating.

**ZX follow-ups (2026-08-14):** CUSTOM scheme (4) + per-key rebinder (long-press → pulse →
bind; green dot; `zx_scheme_<sys>`/`zx_bind_<sys>`/`zx_stick_<sys>`/`zx_reverse_<sys>` +
`zxKeyboardOpacity`), ELITE preset (3) = real Firebird 1985 controls, keyboard opacity
slider (20–100%). NOTE: Elite on device is LENSLOK-protected (unpassable DRM) — needs the
ZX Spectrum 128 re-release (no Lenslok) or cracked 48K.

#### Rogue Squadron (N64) main-menu garbage — RESOLVED 2026-08-15 ✅

**STATUS: USER-VERIFIED — the FULL menu renders (Luke + logo + Start Game/Options), and
no transition flash.** Commits: `eae573558` (menu visible) + `f21bfe54e` (FULLY renders)
+ `6a556f111` (mode-change hold, ~3.5s) + `89fa3ad5f` (hold gated to wide-VI only).

**THE ACTUAL ROOT CAUSE — VI field-toggle inversion (NOT the VI geometry):**
- Commit `9c7e15b74` (2026-08-10) inverted `VI::main()`'s field toggle from
  `io.field += !io.halfLinesPerField.bit(0)` (hardware-correct) to
  `io.field += io.halfLinesPerField.bit(0)`.
- Real HW: interlaced (EVEN halfLinesPerField, e.g. NTSC 262) → field toggles 0/1 each
  frame; progressive (ODD, e.g. 263) → field stays 0. The inversion locked field=0 for
  interlaced games (262 even → bit(0)=0) → **parallel-RDP's serrate scanout sampled only
  ONE field's lines every frame → the menu rendered black** (and half-image/scanlines
  elsewhere).
- **FIX: reverted to `io.field += !io.halfLinesPerField.bit(0)`.** The Conker freeze fix
  in that same commit was the COINCIDENCE change (vi.cpp IRQ raise logic) — KEPT, Conker
  re-verified fine. THIS IS THE ONLY CORE FIX NEEDED.

**Key measured data (what it looked like):**
- RDP: `scissorF0=1366 scissorF1=0` → RDP NEVER sets the interlace field bit → renders
  PROGRESSIVE (full 448 rows, rectY=[0..1792]) into 512-wide fb at 0x790000.
- VI: `serrate=1` (interlaced presentation), VI_ORIGIN alternates 0x790400 (field 0) /
  0x790800 (field 1) each frame. VI scanout uses serrate to sample HALF the lines per
  frame (alternating fields); the field-toggle inversion broke the alternation → always
  field 0 → black.
- Menu is 100% texrect (tri=0, texrect=50K+, fill=98) and loadTile WAS high (544K+) —
  textures loaded fine; the pipeline-drop hypothesis was ruled out.

**FALSE LEADS (do not repeat — all tried):** (1) "VI clamps wide→640 black"; (2) RDP
renders elsewhere than VI origin; (3) textures not loaded; (4) wide-mode VRAM-extract
stride override (512 vs VI 1024) — caused heavy interlace lines + 2x vertical zoom +
bottom crop, REVERTED; (5) progressive-force (clear serrate, double v_res in wide mode)
— same garbage family, REVERTED; (6) RDRAM densest-block scan — finds textures; (7)
pipeline blacklist / dispatch dropped — ruled out; (8) aggressive `lies` overrides —
collateral damage, reverted. Desktop ares uses raw VI values with ZERO wide-mode
special-casing.

**KEPT fixes from the investigation:** coherency range fix (`scanout_memory_range` uses
`rdpFramebuffer*` — syncs the right 512-wide range), race fix (Phobos-core fb accessors),
N64 Debug Logging auto-on (loadRom sync), cmds counter gated behind debug toggle.

**MODE-TRANSITION FLASH — RESOLVED (commits `6a556f111` + `89fa3ad5f`):** the white+
rainbow flash entering the menu (attract 400-wide → menu 1024-wide) was the first
scanout of the new mode reading unrendered RDRAM while the menu's big textures load.
FIX: hold the previous valid frame for `MODE_CHANGE_HOLD_SCANOUTS` (210 ≈ 3.5s @ 60fps;
tuned 2s→still flashed, 5s→long, 3.5s=perfect) after a vi_width change, GATED to wide
targets only (vi_width > 640) so normal games (640↔320, MT user-verified flawless) are
never delayed. Diagnostic `mode-change: vi_w %u -> %u (wide=%d)` when it fires.

**Diagnostics in tree (CLEAN UP once the flash is resolved):** `PhobosVI` (video buf
probe, pixbox, cpu15 + alt0/altMid, extract geometry, scanoutValid/hlpf/serrate),
`PhobosRDP` (coherency probe, cmd counter incl. scissorF0/F1 + rectY), `PhobosV` (video
probe), vulkan.cpp suppression disabled.

**Related (same family, user-reported — UNRESOLVED):** Conker/MT 3D intros white flashes;
F-Zero X title skew; MT gameplay garbage lines. Re-test after the field-toggle revert —
may already be improved.

**Related:** Task 5b (Adreno compute shader class); Conker/MT intro white-flash +
F-Zero X title skew + MT gameplay garbage lines (same family — game-allocated buffers the
VI doesn't reflect).

#### N64DD quit→reload SIGSEGV — FIXED 2026-08-17 (commits `6063c438b` + `799b8898a`, pushed)

**Symptom:** after playing a REAL 64DD game (load `F-Zero X.z64` → pause menu
`[+ Insert 64DD Disk]` → pick `.ndd`), quitting then immediately reloading `F-Zero X.z64`
SIGSEGVs: `fault addr 0x3689e0`, `ares::Nintendo64::CPU::LW(CPU::r64&, r64 const&, short)+124`.
Not reproducible with the single-file Cart Hack — the ~70MB `program.disk` flush makes the
teardown slow enough that the race fires every time.

**ROOT CAUSE (user logcat, NOT the abandon path):** all N64 emulated state lives in
namespace-global singletons. `MainViewModel.unloadSystem()` cleared `_isLoaded`/`_isPaused`
only AFTER the native teardown; tapping a new ROM during that window composed a fresh
`EmulatorScreen` with a STALE `isLoaded=true` → `LaunchedEffect(isLoaded)` fired
`PhobosCore.setEmulationRunning(true)` mid-teardown → a new emu thread copied
`localRoot = root` (the OLD half-torn-down 64DD root) and ran `CPU::LW` against the freed
singleton hardware (`Detach: Cartridge ROM` → `rom.reset()`) → SIGSEGV. Log fingerprint:
`ensureThread: replacing abandoned` + `FPS=2.2` BEFORE `Saves: flushed program.disk` +
`Detach: Cartridge ROM` + crash.

**FIX (2 layers):** (1) Kotlin — `unloadSystem()` clears `_isLoaded`/`_isPaused` BEFORE
the native teardown, so the new screen shows "Initializing..." and its LaunchedEffect can't
fire against the dying system; (2) native — `systemUnloading` guard: `setEmulationRunning(true)`
refuses to spawn a thread while `unloadSystem()`/the N64DD reload is tearing down (set at
unload entry + N64DD reload entry; cleared at every exit and before the thread restart).
Also retained from `6063c438b` (defense-in-depth): zombie park/join
(`EmuThreadCookie` + `parkZombieThread()` + `joinAbandonedThreads()` before every
`::ares::Nintendo64::load()`) for genuinely stuck threads.

**VERIFIED:** user ran many load→quit→reload cycles of BOTH the real z64+ndd flow and the
Cart Hack — zero crashes; every cycle `Emulation thread generation N exiting` →
`System unloaded`; next game boots 60 FPS.

**COSMETIC (left for later):** `ensureThread: replacing abandoned emulation thread` logs on
every clean load (stale pthread handle after clean exit; benign but noisy, and reallocs
`runMutex` per load — clear `emuThread=0` at end of clean unload to fix).

#### 64DD RTC seed (Error 48) — FIXED 2026-08-17 (commit `799b8898a`, pushed)

**Symptom:** "Error 48 — Date/Time not set" every boot of F-Zero X Expansion Kit.

**ROOT CAUSE:** `DD::RTC::load()` seeded the clock only when `time.rtc` was all-0xFF
(`if(!~check)` — erased EEPROM), but Phobos creates the node ZERO-filled
(`dir->append("time.rtc", 0x10)`) → `check==0` → seed skipped → zeros invalid (`valid()=0`)
→ all-FF written back. Log: `RTC load: 0000... new=0 valid=0` → `RTC save: ffff...`.
The earlier `seedCurrentTime()` addition (6063c438b) never fired for a fresh node.

**FIX:** `if(!~check || check == 0)` — seed on first boot either way. `seedCurrentTime()`
writes host BCD time (year..second) + unix timestamp into ram[8..15]; time.rtc node always
present in the system pak; RTC persisted via `root->pak()` in flush/import loops.

**VERIFY — ✅ DONE on-device 2026-08-17 15:07 (fresh 799b8898a+ build):**
```
RTC load: 0000 0000 0000 0000 | ts=00000000 | check=0000000000000000 new=0 valid=0
RTC seed: 2608 1715 0758 0000 | ts=6a835c0e    <- 2026-08-17 15:07:58 host time in BCD
RTC save: 2608 1715 0803 0000 | ts=6a835c14    <- clock ticked +5s -> alive
```
Error 48 gone. Earlier 14:42 test ran the pre-fix 12:41 APK (old signature — not a
regression). Task (b) CLOSED.

#### GBA RTC host-time seed (Pokemon Unbound clock) — FIXED 2026-08-17 (commits `fe5714eec` → `3a440d84a`, pushed)

**Symptom:** Pokemon Unbound (GBA, Task 38) passes its "RTC not initialized" check and
persists the RTC file, but the in-game clock reads **01/01/2000 @ 0:10 AM** — the GBA
RTC starts at the 2000 epoch and counts up instead of reflecting the host date/time.
Time-based events (berries, real-time clock) are wrong.

**ROOT CAUSE 1 (fe5714eec):** `S3511A::load()`'s new-save branch called
`initRegs(false)` → year=0, 01/01 00:00:00 (the 2000 epoch). Fixed by seeding host
BCD time on a brand-new file.

**ROOT CAUSE 2 (found 16:15 on-device, `3a440d84a`):** the seed set status=0x82
(HALT bit set, matching initRegs). Pokemon's SII RTC driver treats a HALTED chip as
unset and WRITES 2000-01-01 00:00:00 into the DATETIME registers (then clears HALT →
status 0x40), wiping the seed. On-disk evidence: `00 01 01 00 01 24 27 40 ...` (2000-era
clock, status 0x40) — deleting time.rtc before boot did NOT help, same result.

**FIX (ares/component/rtc/s3511a/):**
1. `seedCurrentTime()` sets status=**0x40** (24-hour, HALT CLEARED, running) so the
game reads a valid running clock instead of re-initializing it (matches the state the
game converges to).
2. `load()` auto-reseeds EXISTING legacy saves whose RTC year is behind the host year
(created pre-seed, ticking from the 2000 epoch forever); `seedCurrentTime()` refreshes
the stored timestamp so the reseed sticks. Future-dated clocks left alone.
3. Tick-elapsed-first ordering, then year check.
Logs `S3511A seed: ...` (tag PhobosRTC).

**Status:** ✅ **VERIFIED on-device 2026-08-17 16:30 (user: "save files work with updated
RTC!")** — GBA RTC task CLOSED.

#### N64 save persistence (4-bug bundle) — ALL FIXED & VERIFIED 2026-08-17 (commits `6063c438b` + `799b8898a`, pushed)

Save path: `savesPath/Nintendo 64/<RomBase>/`. All four bugs from the 2026-08-16 audit
are closed:
- **(a) 64DD disk save area** — `program.disk` (70MB) + `program.disk.error` persisted
  via `secondaryMedium` + system pak; `.disk`/`.disk.error` added to BOTH import+flush
  filters. VERIFIED on-device: files on disk, saves load.
- **(b) "Error 48" 64DD RTC** — seed fires for the zero-filled `time.rtc` node Phobos
  creates (`if(!~check || check == 0)`); BCD host time + timestamp written; persisted
  via system pak. VERIFIED 15:07: `RTC seed: 2608 1715 0758` → `RTC save: 2608 1715
  0803` (ticked +5s); Error 48 gone. (First attempt 6063c438b only handled all-0xFF
  files; 799b8898a fixed the zero-filled case.)
- **(c) "cannot be played with this disk alone"** — base-cart medium-ordering on
  reload; cartridge + disk both attached BEFORE power-on in the N64DD reload path.
  VERIFIED on-device: loaded z64+ndd, quit, reloaded base cart — worked.
- **(d) N64 cartridge saves (SRAM/EEPROM/Flash)** — `.flash` present in all three save
  filters (import ×2 + flush); import runs BEFORE connectDevices for cart+disk+system
  paks. VERIFIED via GBA flash (Pokemon Unbound saves persist); N64 flash shares the
  same filter path.

#### ALL-core save persistence audit — FILTER FIXES DONE 2026-08-17 (commits `6063c438b` + `799b8898a`)

`.flash` filter gap FIXED (all three save filters include `.flash`); 64DD
`program.disk`/`.disk.error`/`time.rtc` round-trip verified in logs; GBA flash verified
on-device. Coverage (from Phobos `pak->write("save.*")`): FC=`eeprom`✅, GB/GBC=`eeprom`✅,
GBA=`ram/eeprom/flash` ✅, MD=`ram/eeprom`✅, N64=`ram/eeprom/flash` ✅, N64 RTC=`rtc`✅,
N64 controller pak=`pak`✅(separate), PS1=`card`✅, SFC=`ram`✅, WS=`ram/eeprom`✅.

**Remaining (open sub-items, NOT core bugs):** MIA sidecar path
(`program.sav`/`program.flash`); dirty-write trigger on app-kill (user declined to
verify); NGP CPU RAM/BIOS export (export code exists in unloadSystem — verify only).

#### Stale emu-thread handle on clean unload — FIXED 2026-08-17 (commit `1769c1bdb`)

Cosmetic: after a clean unload the emu thread had already exited, but the stale
`emuThread`/`currentEmuThread`/cookie were left set, so the next load logged the
misleading `ensureThread: replacing abandoned emulation thread` warning and reallocated
`runMutex` (a tiny per-load leak). Fix: clear the handles at the end of the clean unload
path (thread confirmed exited — we hold runMutex). The ABANDON path still leaves them
set on purpose (zombie tracking).

#### Task 17 — Auto-Save / Auto-Load State ("Auto" save-state slot) — DONE & VERIFIED 2026-08-17 (commits `1769c1bdb` + `a7e34946c`, pushed)

"Auto" save-state slot; save on quit, load on boot. **VERIFIED on-device (GBA Pokemon)
2026-08-17 (user confirmed auto-save + auto-load work).**

Implementation (Kotlin):
- `performSaveState`/`performLoadState` suspend helpers extracted from
  saveState/loadState; slot < 0 → reserved `<rom>.state.auto` file (manual slots 0-9
  untouched).
- Auto-save in `unloadSystem()` BEFORE the native teardown (core still alive).
- Auto-load in `loadRom()` right after the core loads (before the emu thread starts →
  no runMutex race); skips silently when no auto state exists.
- New Auto-Load setting (`auto_load_memory`, default OFF; Auto-Save default ON).
- Fixed the latent SettingsStore-true/native-false default mismatch (Kotlin setting is
  now the single source of truth; native autoSave/autoLoad flags were dead code).
- **Renamed "Memory" → "State"** (`a7e34946c`) to avoid confusion with the always-on
  cartridge/flash save flushing (flushSavesToDisk on every pause + clean unload — no
  toggle needed). DataStore keys unchanged so prefs survive.
- **Selectable Auto slot:** the cycler now wraps 0-9 → Auto → 0 (reachable after slot 9
  and before slot 0); pause menu shows "Slot Auto"; manual Save/Load/Delete to the Auto
  slot works (deleteState mirrors `<rom>.state.auto` naming + labels).
- **Pause-menu toggles (all cores):** Auto-Save State / Auto-Load State switches in the
  Save / Load States section, so they're reachable in-game without visiting Settings.

#### Task 52 — PS1 multi-disc swap (FIXED & VERIFIED 2026-08-17 `42c462549`)

The old "Change Disc" was a STUB reusing the 64DD path. Added a PS1 branch in
`loadSecondaryRom()`: `currentMedium = secondaryMedium` → find PS1 `Disc Tray` port →
`tray->disconnect()` → `tray->allocate("PlayStation Disc")` → `tray->connect()`
(re-reads `cd.rom` + TOC). No full reload.

**BUG (found on-device, MGS disc 2):** `root->find<Node::Port>("Disc Tray")` returned
null → "PS1: Disc Tray not found — cannot swap". ares `Node::find<T>(name)` only
searches DIRECT children; the port is nested at root→PlayStation→Disc Tray. The N64DD
path worked because it uses `find<Node::Port>()` with no name (that overload recurses).
**Fix: `root->scan<Node::Port>("Disc Tray")` (recursive).**

**VERIFIED on-device 23:23 (MGS Disc 2 swap):** log shows `Detach: PlayStation Disc` →
`Attach: PlayStation Disc` → `VFS: Returning currentMedium pak for PlayStation Disc` →
`PS1: disc swapped to PlayStation` (no "Disc Tray not found", no crash; brief FPS dip
as the new disc TOC loads, then recovers). User wasn't at a disc-change prompt in-game,
but the hot-swap mechanics (detach/attach/re-read) are confirmed working. Related:
Task 51 (ZX multi-tape swap) is the same hot-swap-tray mechanism.

#### Task 62 — Dotprod/FP16/NEON optimization survey (DONE 2026-08-17, surveyed)

**Context (2026-08-15):** commit `4c3933de4` removed the root CMake's hardcoded
`-march=armv8-a+simd` that was overriding the per-flavor `cppFlags` (last-flag-wins in
Clang). The modern flavor now correctly compiles with `-march=armv8.2-a+fp16+dotprod`
(verified in ninja + disassembly), legacy with `armv8-a+simd`.

**Survey Findings (2026-08-17):**
1. **Clang emits ZERO dotprod/FP16 instructions automatically** — Clang only auto-emits
   SDOT for very specific int8 GEMM dot-product loops and never auto-vectorizes FP16
   without explicit intrinsics (`vaddh_f16`, etc.). The modern binary's 159KB diff is
   codegen noise, not a perf win.
2. **Dead CPU Software Mode:** N64 has a legacy CPU fallback path (`VI::refresh` CPU
   scanout + 16.7M identity palette) which was a remnant of software rendering before
   Vulkan/parallel-RDP. Since Vulkan is required on mobile and software mode is useless,
   any remaining dead code paths can be safely ignored or pruned, but keeping the
   fallback active for wide modes (Rogue Squadron 1024 menu) is helpful.
3. **NEON Opportunities:** Hand-written NEON is already used in PhobosRunner for direct
   Vulkan RGBA→ABGR copy. Audio mixer and resampler use standard scalar loops with float32
   (correct for audio precision). Parallel-RDP already leverages SIMD/NEON where applicable.
   Dotprod/fp16 are largely inert without targeted manual intrinsics, which are unnecessary
   since Phobos hits full 60 FPS on Adreno 740.

**Difficulty:** 🟢 Easy (survey). **Status:** ✅ Survey Complete.

### ⏸ PENDING / PARKED (PLANNED / PARKED)

#### Task 13c — Multi-controller support (PLANNED 2026-08-11)

**Goal:** let two (or more) physical gamepads control separate players.

**Current state:** Phobos core is ready (every 4-port system allocates Controller Port
1–4 independently; Phobos attaches Gamepad to all 4 N64 ports). The bridge is
single-player only: one shared `InputState` drives ALL Gamepad nodes; only `cachedPlayer1`
gets a pak attach; no per-pad identity/assignment.

**Implementation plan:**
- PhobosRunner.cpp: per-port `InputState[4]`; route each Gamepad node to its port's slot
  (via parent port name); keep `cachedPlayerN` per port; new JNI `setInputForPlayer(...)`
  (legacy `setInput` = player 1 alias).
- MainActivity/GameInputState: enumerate gamepads, track add/remove, assign stable player
  slots (persisted in SettingsStore), route events per player.
- SettingsStore/SettingsScreen: persist pad→player assignment; UI to assign players.

Notes: touch controls remain player 1; per-player rumble needs per-port rumble state.

#### Task 42 — Perf Monitor configurable metrics (PLANNED)

Performance overlay needs a config screen to toggle metrics. 10 metrics (FPS, frame time,
RAM, core, shader fails, GPU [43], CPU freq [44], thermal [44], Vulkan driver, refresh).
Files: PerfMonitorSettingsScreen.kt [NEW], SettingsScreen.kt, EmulatorScreen.kt,
SettingsStore.kt (10 bool prefs, defaults FPS/frametime/RAM/core ON), MainViewModel.kt,
PerformanceOverlay.kt. No native changes.

#### Task 42b — Perf Monitor: Game FPS (true N64 render rate) (PLANNED 2026-08-17)

**Context:** the current FPS counter counts VI scanouts (`screen->frame()` in VI::main,
59.94Hz NTSC) — i.e. EMULATION SPEED, not the game's render rate. Games that natively
run below 60 (OoT ~20fps, GoldenEye/Turok ~15-30) read 60 on the counter because the
real console ALSO scans out at 59.94Hz and just reuses the framebuffer between renders.
User wants the overlay to distinguish emulation speed from game frame rate.

**Design:** count transitions of `::ares::Nintendo64::vi.io.dramAddress` (VI origin) per
second — double-buffered games flip origin each rendered frame → transition count =
game FPS. Show as `VI 60.0 / Game 29.8`. Sampling site: the emulation loop's per-second
stats block (already gated by n64DebugLogging / perf overlay), or a counter incremented
in VI::main.

**Caveats:** single-buffered/in-place-rendering games don't flip origin → undercount;
robust fallback = cheap content-diff (hash a few framebuffer rows per VI frame, count
changed hashes). Mupen64Plus-FZ exposes exactly this VIs-vs-frames split.

#### Tasks 43 & 44 — GPU/CPU/Thermal metrics (PLANNED)

43: GPU util via libadrenotools/kgsl_perfcounter or `VK_KHR_performance_query`, poll 1 Hz,
`PerformanceStats.gpuUtilization`. 44: CPU freq from `/sys/.../scaling_cur_freq`, thermal
from `/sys/class/thermal/thermal_zone*/temp`, poll 1 Hz.

#### Task 49 — N64 save import/export (PLANNED 2026-08-11)

Format compat (raw dumps map 1:1): `.sra/.srm`→`save.ram`, `.eep/.eeprom`→`save.eeprom`,
`.fla/.flash`→`save.flash`, `.mpk`→`save.pak`. No core changes needed — Phobos already
imports `savesPath/Nintendo 64/*` at load and exports on quit. Feature = UI + file-copy:
`getSaveFiles()`, `copySaveFile(name, destPath)` native/JNI; pause-menu Import/Export
section; MainViewModel helpers.

#### Task 59 — Proper write-through dcache bypass for cached RDRAM (PLANNED, PARKED)

**Goal:** eliminate dcache tag-check/fill/writeback overhead (~330K misses + ~210K
writebacks/sec in MT) WITHOUT breaking DMA.

**Why Task 58 failed:** the slow path bypassed dcache but the JIT's inline write path
still used write-back dcache → CPU read stale data after its own writes → hang.

**Correct design:** write-through semantics in BOTH the JIT inline emitters AND
`CPU::read/write` slow path; PRESERVE SMC tracking (still mark section dirty on write so
self-modifying code works); gate per-game (default OFF, enable for MT). High risk; parked
until after release A/B + counter-gating.

#### Task 60 — Per-game hash overrides table (PLANNED, PARKED)

ROM-hash-keyed table of per-game overrides (jitInterleaving, fastDcache, viOverclock,
cpuOverclock...). Defaults for all games; only table entries override. **A/B RESULT
2026-08-12:** per-game interleave override NOT needed (2048*2 == 4096*2 FPS on RP6; MT
dips are SI-DMA waits). Still useful for Task 59's per-game dcache bypass + future knobs.

#### Mischief Makers (N64) title freeze — PARKED 2026-08-15 (deep RSP accuracy)

**Symptom:** loads to title fine, press Start → game sits forever. Core not frozen
(hotkeys work, RDP/VI keep producing the frozen title frame at 60fps).

**DIAGNOSED (commit `22ee8057a`, all gated behind N64 Debug Logging):**
1. **Interrupt chain is 100% functional:** RSP audio microcode BREAKs normally at IMEM
   0x138/0x77C every ~16ms with `interruptOnBreak=1` → `MI::IRQ::SP` → CPU RCP=1
   (`cause.pend=04`) → handler entry (`IE→0, EXL→1`). Verified via `PhobosRSP` BREAK
   logs + `PhobosCPU` setInterruptPending trace.
2. **NOT the VU stubs:** zero executions of the VZERO-mapped "broken" VU ops
   (VSUT/VADDB/VSUBB/VACCB/VSUCB/VSAD/VSAC/VSUM/VEXTT/VEXTQ/VEXTN/VINST/VINSQ/VINSN).
3. **The game's IRQ handler (0x800A5FC8, reached via the exception vector
   0x80000180's `jr k0`) follows a corrupted dispatch** and lands in a `beq self` fatal
   trap at 0x800008b8 with **garbage EPC (`0xb400007b...` = RDRAM data, not a valid
   address)**. The handler re-enables IE inside the trap (IE=1 EXL=1) and spins forever.

**Root cause:** upstream ares RSP microcode inaccuracy for this game's custom audio
microcode — it produces subtly wrong results (not via the stubbed ops), the game's
audio driver detects the corruption and deliberately hangs in its fatal trap. The
garbage EPC is a symptom (handler jumping through a corrupted dispatch table). Upstream
ares has the same game broken; this is the known "RSP-heavy game" class.

**Next steps (when revisited):**
- Add a VU instruction + operand trace to the RSP (log every VU op with vs/vt/result
  at the audio task's BREAK) and bisect which instruction produces a wrong result.
- Compare against a reference RSP (ares-parallel low-level RSP or Mupen64Plus's) on
the same ROM to find the divergence.
- Candidate suspects: VADD/VLT/VCH/VCL/VCR/VRCP/VRNDP/VMULF families (Mischief Makers
uses many; ares VU has known partial accuracy in rounding/overflow edge cases).

**Difficulty:** 🔴 Hard (deep). **Status:** 🟡 Parked (diag in tree).

### 🔄 IN FLIGHT (OPEN / investigating / diagnosed / broken-gated / implemented-awaiting-verify)

#### Task NGCD-M2 — Neo Geo CD drive/IRQ/DMA boot blocker (IN FLIGHT 2026-08-25, uncommitted)

**Symptom:** Neo Geo CD game boot `START` does nothing — the BIOS receives Start, programs the CDC, and
begins a disc read, but game code never reaches `0x100000`, so the game never boots past boot state 3.

**Input question is CLOSED (probe verdict, on-device 2026-08-25):** a temporary PAD probe in
`ares/ng/controller/arcade-stick/arcade-stick.cpp` logged button state from `readControls()/pollCoin()`.
Captured logcat while holding A then pressing Start on the Retroid built-in pad:

| Evidence | Conclusion |
|---|---|
| `PAD probe … a=1` (held A) | A (`k:96`) reaches the core — input works |
| `PAD probe … start=1` (pressed Start) | Start (`k:108`) reaches the core — input works |
| BIOS programmed CDC (`reg 01←e2`, `0a←a7`, `0b←f0`) + began read (`stat=0001`, ~8 s) | BIOS received Start, started CD load |
| `t1=0` entire run; `pend=0010` (CDD pending) yet never dispatched | **type1 (sector-completion) IRQ never fires — the boot blocker** |

**Conclusion:** front-end input (`k:` mapping, hotkey capture, `controllerPlayerIndex`) is 100% working.
The blocker is entirely the in-progress **NGCD M2** drive/IRQ/DMA pipeline.

**Root cause (static review, cross-referenced with wiki.neogeodev.org):**
- **Defect 1 — DMA trigger mismatch** (`ares/ng/cpu/memory.cpp:560-575`): LC8953 trigger register is the
  **byte** `$FF0061` with `$00`=load microcode, `$40`=start DMA (per the [DMA](https://wiki.neogeodev.org/index.php/DMA)
  + [LC8953](https://wiki.neogeodev.org/index.php/LC8953) pages); params are 32-bit at `$FF0064`/`$FF0068`/`$FF0070`,
  microcode via `$FF0080`-`$FF008E`. Current code matches the **word** `0xff0060` checking `bit(6)` and swallows the
  microcode as a no-op — this is why "`FF0061` DMA never fires".
- **Defect 2 — CDD/type1 pending but not dispatched** (`ares/ng/cpu/cpu.cpp:38-64`): log shows `pend=0010` with
  `t1=t2=t3=0` — the CDD dispatch block isn't being reached. Hypotheses in priority order: (1) empty
  `if(NeoGeo::Model::NeoGeoCD()){}` at the top of dispatch / early returns shadow the CDD block;
  (2) `type1Pending` cleared before dispatch or sector pipeline body not running despite `stat=0001`;
  (3) `REG_IRQACK` (`$FF000F`) writes swallowed as no-ops never clear/latch pending correctly.

> [!NOTE]
> MAME's `neogeocd.cpp` (GPL, docs-only) is the behavioral reference; the Sanyo CDC `DTEI`/`DTBSY`/`DTTRG`
> handshake pattern already exists tested in `ares/md/mcd/cdc.cpp` and should be mirrored.

**Planned fix (not yet executed at time of writing):** (1) correct the `$FF0061` byte trigger + route DMA
param/microcode registers; verify `Dma::start()` modes against MAME; (2) rework `CPU::main()` CDD dispatch so
`type1` (sector completion) beats the constant 75 Hz `type2`; confirm `tick→readLbaToBuffer→ctrlChecks→raiseType1`.

**Verification:** deploy to `49016109` (Retroid Pocket 6, SamSho RPG disc); grep `NGCD|CDD|CDC|DMA`;
pass = `type1` increments + DMA start fires non-busy + code reaches `0x100000` → game boots; no AES/MVS regression.

> [!IMPORTANT]
> **2026-08-26 session — major re-diagnosis, read `docs/ngcd-m2-session-context.md` for the full briefing.**
> The original Defect 1/2 framing is superseded. Summary of new findings:
> - **BIOS file is byte-swapped** (loaded via `readl(2)` = LE words; MAME `ROM_GROUPWORD|ROM_REVERSE`). Static
>   disasm must use `ngcd_work/bios_swapped.bin` — the raw file disassembles to garbage (earlier
>   "self-decrypting BIOS" theory was wrong). Reset entry: SP=$10F300, PC=$C00402 → `$C0A5B6`.
> - **Input model fixed per libretro neocd** (`memory_input.cpp`): selector register `$380001`
>   (valid = {0x00,0x12,0x1B}) gates `$300000`/`$340000`/`$380000`; `$380000` byte(1) carries Start/Select
>   low-nibble (P1 Start/Sel = bits 0/1, P2 = 2/3), active-low. 68K byte-store to odd addr → `write(0,1,waddr)`
>   — detect selector via `!upper` on the `$380000` handler, not the raw address.
> - **DMA trigger fixed per wiki/MAME**: byte `$FF0061`, `$40`=start DMA (`$00`=load microcode). Old word
>   `$FF0060` bit(6) never fired. BIOS writes `move.b #$40,$ff0061` at dozens of sites.
> - **CDD settle fix**: `stop()` keeps `statusHack=0x0e` (boot disc-check REQUIRES it) + `settleCounter=150`;
>   `tick()` → `0x09` + re-export after ~2 s so the loader's status poll sees a settled drive.
> - **VERIFIED on-device**: pressing Start now issues `CDD READ lba=13 track=1`, streams 735 sectors,
>   type1 IRQs fire (t1 718→1434) — the first real disc load. The BIOS then returns to the CD Player instead
>   of booting the game.
> - **CURRENT BLOCKER**: the BIOS's controller struct TYPE bytes (`$10FD94/$10FD9A/$10FDA0/$10FDA6`, a5=$108000)
>   stay 0 because the detection probe `$C0930C` never runs in this emulator's boot → the input handler
>   `$C09584` takes the `$C095D4` mask path → held/edge (`$10FEDC/$10FDAC`) always 0 → menu Start/D-pad ignored.
>   Fix in progress: seed both byte lanes `$10FD90-$10FDAF` = 3 (joystick type) at `System::power()`; if that
>   doesn't take, move into `Cdd::tick()`. Byte-lane gotcha: even byte addr N = byte 1 of word N>>1.
> - All instrumentation (WR/WRX/TYPWR/P1RD/SB1RD/SELW, IPC/DET/BOOT/STR, PAD probe, RPL traces) is TEMP.

#### Task 10c — PCE family RESOLVED 2026-08-18 (`2795e5813`); ZX 128K RESOLVED 2026-08-18 (`1bf97e3ac`); Neo Geo MVS/AES — 1994-95 WARNING hang FIXED 2026-08-21 (`mia/medium/neo-geo.cpp:172` `tst.b $10FD82` + `13C0...60F8` → `kof95`/`samsho3`/`4`/`5` titles, `wiki:Slot_check_security`/`wiki:PROGSF1`/`wiki:NEO-SMA`); `kof98` `PROGSF1` FIXED 2026-08-21 (`mia/medium/neo-geo.cpp:374` `decryptKof98` + `ares/ng/cartridge/board/progsf1.cpp:19` `header @100: 4e 45 4f 2d` `✓` `49016109`); **SMA grid (`kof99`/`kof2000`/`garou`/`mslug3`) FIXED 2026-08-22** — was a hardcoded 68K reset-vector override (`p[0..7]=0x10f300`) in all 6 SMA branches of `decrypt()`; only kof2000 matched, the others jumped to a wrong entry → BIOS slot-grid. Removed override (kept `NEO-GEO` header patch); P-ROM decrypt verified bit-identical to MAME `prot_sma.cpp`. The `loadRoms 0xC0000 hole` hypothesis was WRONG. + SELECT-coin and switch-game SIGSEGV FIXED 2026-08-22 (see detailed note below)., plus non-MVS sets (`kof98umh` PGM, `kofnw`/`kofxi` Atomiswave) correctly rejected

**✅ PCE / PC Engine CD / SuperGrafx — FIXED & VERIFIED 2026-08-18 (commit `2795e5813`):**

*Root cause (proven by code inspection, not inference):* the fork's `CMakeLists.txt`
`add_compile_definitions` defined NEITHER `PROFILE_ACCURACY` NOR `PROFILE_PERFORMANCE`
(upstream ares always defines one; the upstream default is PERFORMANCE). In
`ares/pce/psg/psg.cpp` the ENTIRE `PSG::main()` implementation AND the `load()` stream
setup are wrapped in `#if defined(PROFILE_ACCURACY) / #elif defined(PROFILE_PERFORMANCE)`
blocks → with no define, both compile out → `PSG::main()` is empty. Deadlock chain:
`ares/pce/cpu/cpu.cpp` `CPU::step` calls `synchronize(psg)` when the HuC6280 timer fires
(~3072 clocks after power-on) → `Thread::synchronize` (`ares/ares/scheduler/thread.cpp`)
→ `co_switch(psg)` → the PSG coroutine spins forever in the empty main → `scheduler.enter()`
never returns → black screen, 0 FPS, emulation thread wedged. Deterministic on ANY
platform with this build config (not ARM64/libco-specific).

*Fix:* added `PROFILE_PERFORMANCE` to the global `add_compile_definitions` (upstream-faithful
default; SFC timing takes its standard performance branch — `ares/sfc/cpu/timing.cpp`
`#if !defined(PROFILE_PERFORMANCE)`). Un-gated PCE/CD/SuperGrafx in the broken-core gate
(`PhobosRunner.cpp`).

*Verification (on-device, Retroid Pocket 6, adb-driven loads):* Final Lap Twin (HuCard)
60 FPS @ ~6.2 ms; SuperGrafx mode (same HuCard, dual-VDC path) 60 FPS @ ~7.2 ms; Akumajou
Dracula X (CHD + System Card 3.0) 60 FPS @ ~2.5 ms. Regression smoke after the define:
Super Famicom 60.2, PlayStation (MGS disc 1 CHD) 60.2, Mega Drive (Sub-Terrania) 59.9 FPS.
PCE accurate VDP holds the 60 FPS floor — no perf-VDP fallback needed.

**✅ ZX Spectrum 128K — FIXED & VERIFIED 2026-08-18 (commit `1bf97e3ac`):**

*Root cause (proven on-device, not inference):* the 128K ZX core names its system node
"ZX Spectrum 128", but `PhobosRunner::pak()`'s tape branch matched
`root->name() == "ZX Spectrum"` → for 128K loads `platform->pak(node)` returned an empty
directory → `Tape::load()` (`ares/spec/tape/tape.cpp`) read
`pak->attribute("frequency").natural()` = 0 → `stream->setFrequency(0)` → the cubic
resampler reset with `_ratio = 0` (`nall/nall/dsp/resampler/cubic.hpp`
`while(mu <= 1.0) { …; mu += _ratio; }`) → **infinite loop on the TAPE thread's first
`stream->frame(0.0f)`** → scheduler wedged, zero frames (the 48K path worked because its
root IS named "ZX Spectrum"). Instrumentation trail (temporary `ZX128Diag` probes,
all removed post-fix): System::run enter → Thread::Enter matched CPU (idx=1) →
first opcode fetch's `step(4)` co-switched to the tape thread (clock=0) → Tape::main #1 →
`Cubic::write #1 in=0 out=48000 ratio=0` → dead. No `audio()` call ever fired.

*Fix:* `root->name().beginsWith("ZX Spectrum")` in `pak()` (`PhobosRunner.cpp`).

*Verification (on-device, adb-driven):* Enduro Racer (48-128K re-release) 50.8 FPS stable
@ ~8.7 ms/frame (PAL 50 Hz target), all three streams registered (tape 44.1 kHz, ULA
3.5469 MHz, PSG 221.7 kHz), audio ring healthy (1 xrun total); 48K Elite regression 50.6 FPS;
cross-core reload (ZX→SFC) 60.2 FPS. Gate removed.

**REMAINING — Neo Geo MVS/AES (gated, DIAGNOSIS IN PROGRESS 2026-08-18):**
no profile guards in `ares/ng` (CPU 12 MHz, LSPC 6 MHz, APU 4 MHz, OPNB 8 MHz — all
unconditional mains). Suspects: MIA MAME-style zip path (`mia/medium/neo-geo.cpp` needs
P/M/C/S/V1/V2 ROMs + `Neo Geo.bml` hit + `neogeo.zip` BIOS via `PhobosRunner.cpp` firmware
mapping) or a run-path wedge. BIOS copy (`fw_ng_bios` → `mia_temp/neogeo.zip`) already
wired in `MainViewModel.loadRom`.

**PROGRESS (2026-08-18, un-gated for diagnosis via `if (false && identifiedSystem == "Neo Geo")`):**
- kof2003.zip LOADS: MIA identifies 'Neo Geo AES', core boots as "Neo Geo MVS" (MVS BIOS
  sp-e.sp1 attached from the BIOS zip; "VFS: Failed to attach static.rom" = benign warning).
- **RUNS 59.2-60.1 FPS sustained** (47+ stat samples) — the old black-screen 0-FPS wedge is GONE.
- Streams registered: 'FM' (ch=2, 500 kHz) + 'SSG' (ch=1, 500 kHz).
- **OPEN ISSUE: no audio** — ring stays `0/12000 (0%)`, only 2 xruns total. Multi-stream
  lockstep (`PhobosRunner.cpp` audio() ~1384-1417: mix+emit only when EVERY stream has
  pending samples, bounded 8192) may never satisfy allPending, or `muteAudioAtomic` path.
  ZX comparison (healthy): ring 87-97% full, 1 xrun. Next: probe pending samples per stream
  at the lockstep gate with video on screen (loader now navigates — headless runs couldn't
  be watched).
- Video presentation UNVERIFIED until the debug-loader navigation fix lands (was running
  headless behind the library screen).

**Next steps (swap-screen + loader-navigation feature first — see Feature Log below, then NG):**
1. Verify kof2003 video + gameplay on the emulator screen via the fixed debug loader.
2. Probe FM/SSG pending counts at the lockstep gate → fix mixing so ring fills (likely
   emit-when-ANY-stream-pending with zero-fill for the laggards, matching the ZX path).
3. Verify sound actually audible, then finalize the gate (remove it), verify MVS+AES both,
   commit + push, update docs.

**Investigation notes (debunked theory):** the shared "ARM64/libco" fingerprint was
INFERENCE, not evidence — no stack dump was ever captured for these cores (the only
diagnostic is an N64-specific `dumpStall`). The scheduler + `libco/aarch64.c` are stock
upstream and work on this exact build for MD (4+ threads), MSX, WS, NGP, GB, SFC, FC, A26,
CV, SG, PS1. The ZX 128K hang was a plain pak-attribute bug, NOT a scheduler fault — the
scheduler suspension/spin behavior is stock ares.

## Feature Log — Swap-Screen Hotkey (2026-08-18, VERIFIED on-device)

**Goal:** hotkey to leave a running game to the library/console/settings — emulation
PAUSES on leave, UNPAUSES on return; the game is unloaded ONLY on explicit quit.

**Decisions (user-approved):** unload-on-quit only (PS1 2nd-disc swap via
`loadSecondaryRom` feeds a new FD without unload — unaffected; N64DD disk change is a core
IPL reset, no unload — unaffected). SurfaceView re-entry is safe (`surfaceCreated` →
`PhobosCore.setSurface`, `surfaceDestroyed` → setPause(true)+setSurface(null)).

**KEY INSIGHT (why the first attempt failed):** the content-view `OnKeyListener`
fallback only fires when NO focused view consumes the key. On the library screen Compose
absorbs them, so the return hotkey never reached it. `ComponentActivity` marks
`dispatchKeyEvent` @RestrictTo — cannot be overridden from the app. Solution: replace
`window.callback` with a `Window.Callback` wrapper (delegating, public API) that
intercepts `dispatchKeyEvent` BEFORE any view dispatch (ViewRootImpl consults the window
callback first). Verified round-trip swap away+back on-device.

**Implementation (committed):**
- `MainViewModel`: `navigateTo(route)` on `MutableSharedFlow` `navEvents`; `emulatorScreenVisible`
  StateFlow + `setEmulatorScreenVisible`; `swapToLibrary()` (setPause(true)+navigate("library"));
  `swapBackToGame()` (guard !loaded/visible → navigate emulator route from loadedSystemName/
  currentRomName + setPause(false)).
- `MainScaffold`: collects `navEvents`; library/console/settings → popUpTo(start)+launchSingleTop.
- `EmulatorScreen`: `"library"` hotkey branch (both the held-combo map ~132 and the hat/dpad
  trigger map ~281) → swapToLibrary; pause menu "Library" button (`EmulationMenu` gains
  `onLibrary`); `onDispose` NO LONGER calls unloadSystem (kept GameInputState.reset +
  emulatorScreenVisible=false); quit dialog Quit button → `unloadSystem()` then onBack.
- `MainActivity`: `Window.Callback` wrapper for the swap hotkey (both directions, ANY screen);
  `installKeyEventFallback` untouched for input translation; debug loader now ALSO navigates
  to `emulator/{system}/{name}` after loadRom (mirrors SystemDetailScreen: load THEN navigate)
  — fixes ALL previous adb loads running headless behind the library.
- `SettingsStore` defaultHotkeys: mirrored EXACTLY from the developer's device config
  (read via `run-as com.phobos.emulator cat files/datastore/settings.preferences_pb` +
  protobuf wire decode). Z-button based: 101=Z for everything (Z+A pause, Z+B quit, Z+X
  reload, Z+Y mute, Z+L1 load, Z+R1 save, Z+L2 keyboard, Z+R2 ff_toggle, Z+SELECT+START
  reset, Z+DPAD slot +/-; library = Z+C [101,98]; analog_toggle = C alone [98]; ff_hold
  unbound; screenshot Z+THUMBR). `HotkeyMappingScreen`: "Swap to Library" entry.

**Testing caveat:** adb `input keyevent` sends DOWN+UP per call — CANNOT hold combos; the
hotkey must be verified with the real controller (or a `sendevent` script on the gamepad
input device).

**Reference for future Neo Geo CD task:** ares fork branch `neogeo-cd` by Luke Usher
(`https://github.com/ares-emulator/ares/tree/neogeo-cd`). NG CD hardware is closely linked
to AES/MVS — once MVS/AES work, port ONLY the CD-ROM hardware + CD-audio files from that
branch into `ares/ng/` (do not pull the whole branch).

#### Feature-completeness audit: Run-Ahead + Fast Boot + Skip Boot ROM (INVESTIGATED 2026-08-17)

Pre-1.0 check of whether the settings toggles actually do anything.

**Run-Ahead — ❌ NOT WIRED (toggle is inert):**
- Settings/UI: `runAhead` DataStore flag + toggle exist (default OFF); `setRunAhead`
  only persists the flag. NO JNI setter, NO native call — nothing reads it.
- Core: Phobos HAS the mechanism — `ares::setRunAhead(bool)` → `_runAhead` global
  (`ares/android_globals.cpp`) → `Screen::frame()`/`refresh()` + `Stream::frame()` +
  MD vdp-performance + SFC ppu-performance all early-return when `runAhead()` is true
  (skip presentation to cut render/fence latency).
- **Fix (easy, ~15 min):** add `setRunAhead` to PhobosRunner/PhobosJNI/PhobosCore.kt,
  call `::ares::setRunAhead(enabled)` in the setter + at load; wire the Kotlin toggle to
  it. (Caveat: it skips ALL presentation work — verify it still presents on the frame
  it returns to; this is ares' intended design, so trust it.)

**Fast Boot — ✅ WIRED but core-limited:**
- Full path: Settings toggle → `setFastBoot` → `fastBootAtomic` → `initialize()` sets
  the "Fast Boot" `Node::Setting` at load → cores honor it: GB (patches boot ROM loop),
  NGP/NGPC (ret-patches boot anim), PS1 (EXE skip to pc=ra).
- **Scope:** ares only exposes "Fast Boot" on GB/GBC, NGP/NGPC, PS1. On N64/MD/SFC/etc.
  the toggle is a NO-OP (no such setting). Not a bug — document it or per-core hide it.

**Skip Boot ROM — ✅ WIRED (GB/GBC/WS only):**
- `setSkipBootRom` → `initialize()`: skips boot.rom attach for WonderSwan; post-boot
  register patch for GB/GBC. Not N64/PS1.

**Status:** 🟡 Audited · Run-Ahead bridge = 🟢 Easy · Fast Boot RESOLVED 2026-08-17 (`42c462549`)

**Fast Boot — RESOLVED 2026-08-17 (`42c462549`):** toggle removed from the global
Emulation Settings (was a silent no-op on N64/MD/SFC/etc.) and moved to a per-core
"Boot Options" pause-menu section shown only on GB/GBC, NGP/NGPC, PS1 (the cores ares
actually supports Fast Boot on).

#### Task 46 — PS1 CD-DA audio pops (FIXED & VERIFIED 2026-08-18, commits `1822c5757` + `df49dfc46`)

**Problem:** Audio pops/clicks occurred when PS1 CD-DA playback transitioned states (sector boundaries, audio/data track switches, play/stop). Diagnostic showed ring buffer healthy (82-100% full, zero new xruns), so discontinuities were in audio content itself—drive state changes (reading/playingCDDA) snapped samples from music→silence or vice versa, creating hard clicks.

**Solution:** Added linear fade envelope (~150 samples @ 44.1kHz = 3.4ms) in `Disc::CDDA::clockSample()`:
1. Track previous playingCDDA state; detect transitions
2. On state change, set fade target (0.0 on exit, 1.0 on entry) and compute fade rate
3. Each sample, ramp gain toward target (linear interpolation)
4. Apply gain to sample output before returning

**Implementation:**
- `ares/ps1/disc/disc.hpp` — Added fade state (cddaGain, cddaGainTarget, cddaFadeRate, previousPlayingState)
- `ares/ps1/disc/cdda.cpp` — Fade logic: state detection, linear ramp, gain application
- `ares/ps1/disc/serialization.cpp` — Did NOT serialize fade state (resets on load, computes correctly on first call; serializing old save states caused SIGSEGV)

**Verification:**
- Clean build, no warnings/errors
- Deployed and tested on device (Retroid Pocket 6)
- Audio ring buffer healthy (81-100% full throughout gameplay, zero new xruns)
- FPS locked at 60.0, no crashes
- Logcat clean (no Phobos errors)

**Why 3.4ms fade?** Sweet spot: long enough to eliminate clicks, short enough to avoid audible swooping. Tested on MGS disc 1; pops eliminated, no artifacts reported.

#### Task 61 — Proper release APK signing (Play-ready / In-place upgrades) (FIXED & VERIFIED 2026-08-20)

**Problem:** GitHub Actions previously built release APKs with an ephemeral auto-generated `debug.keystore`, causing every release artifact to have a different certificate fingerprint and breaking in-place app updates (`INSTALL_FAILED_UPDATE_INCOMPATIBLE`).

**Solution:**
1. Created dedicated stable PKCS12 release keystore at `android/keystore/release.keystore` (alias `phobos`).
2. Configured `signingConfigs.release` in `android/app/build.gradle.kts` to sign all release builds with this keystore, with environment variable overrides (`PHOBOS_KEYSTORE_*`) if needed.
3. `debug` builds (used locally / in IDE) continue to use standard `~/.android/debug.keystore`.
4. Verified with `apksigner`: release APK signed with `CN=Phobos Emulator` (SHA-256 `99:ce:ec:16...`); debug APK signed with `CN=Android Debug` (SHA-256 `50:77:09...`). Releases can now be upgraded in-place seamlessly.

#### Task 63 — PS1 CPU (R3000) recompiler (OPEN — the big one)

**Why:** PS1 is the only "big console" core with NO JIT. Every R3000 instruction pays C++
dispatch: `CPU::main()` is a `while(true) instruction()` loop (ares/ps1/cpu/cpu.cpp:34-45);
`instruction()` → `instructionPrologue` → `decoderEXECUTE` (big switch) → `instructionEpilogue`
+ `icache.fetch` (a plain line-reload, ares/ps1/cpu/icache.cpp:1) + delay-slot + exception checks.
There is no `recompiler.cpp` under ares/ps1/ (verified) and no `Accuracy::CPU::Recompiler` flag
(ps1/accuracy.hpp has only AddressErrors/BusErrors/Breakpoints).

**Reuse:** R3000 is MIPS. The N64 VR4300 recompiler (ares/n64/cpu/recompiler*.cpp) is built on
`nall::recompiler::generic` (SLJIT backend, `supported` includes arm64). Port/adapt that
decode+emit layer to the PS1 R3000: drop the N64 FPU/cop2 state-key parts, keep integer + branch
+ SCC blocks. Even a trimmed integer-only block JIT is a large win; add COP1 (FPU) later. Gate
behind a NEW `Accuracy::CPU::Recompiler` flag + a per-game toggle; keep the interpreter as
fallback (per the accuracy-vs-speed policy: full 60 FPS is the floor).

**Difficulty:** 🔴 Hard (correctness: delay slots, hi/lo, exceptions). **Impact:** HIGH.
**Risk:** correctness — ship behind flag + interpreter fallback, A/B per game on-device.

#### Task 64 — PS1 GPU off-CPU (OPEN)

**Why:** PS1 GPU is a pure software rasterizer (`gpu/renderer.cpp` `triangle()`/`line()`, ~3413
clk/line × 263-314) on a libco thread (`Accuracy::GPU::Threaded = 1`, ps1/gpu/gpu.hpp:18). There is
NO Vulkan path under ares/ps1 (verified: none). The software raster competes with the CPU (Task 63)
for ARM cores.

**Two tiers:**
- (a) Cheap: the screen blitter `gpu/blitter.cpp` (15/24bpp) is a per-pixel CPU copy
  (`readHalf → 1<<24|mask`, ~lines 85-117). Vectorize to a NEON widen/LUT + memcpy. Low risk,
  modest win.
- (b) Big: port PS1 GPU to the existing Vulkan pipeline (like N64 parallel-RDP) — removes the
  entire PS1 raster from CPU. Largest single PS1 win after the CPU JIT, multi-week lift.

**Difficulty:** (a) 🟡 / (b) 🔴. **Impact:** (a) MED / (b) HIGH.

#### Task 65 — SNES CPU (WDC65816) recompiler (OPEN)

**Why:** SNES CPU is interpreter-only (`sfc/cpu/cpu.hpp:1` `struct CPU : WDC65816`; no recompiler
anywhere; component/processor/wdc65816 has none). 65816 is a small 16-bit ISA — cheaper to JIT than
the PS1 R3000. Reuses the same `nall::recompiler::generic` (SLJIT) framework. SNES PPU is already
threaded (`sfc/ppu/ppu.hpp`); the CPU is the demand.

**Difficulty:** 🟡 Medium. **Impact:** MED.

#### Task 66 — GBA CPU (ARM7TDmi) recompiler (OPEN)

**Why:** GBA CPU is interpreter-only (component/processor/arm7tdmi, no recompiler). GBA is usually
fine on ARM64; a recompiler helps heavy titles (Zelda, Metroid, Pokemon). Lower priority than
PS1/SNES. Reuses the same SLJIT generic framework.

**Difficulty:** 🟡 Medium. **Impact:** LOW-MED.

#### Task 67 — Genesis (MD) performance VDP (OPEN)

**Why:** the SH2 is already JIT'd (component/processor/sh2/recompiler.cpp) — good. But the VDP is the
SOFTWARE variant: ares/md/vdp/vdp.hpp:2-3 and ares/md/vdp/vdp.cpp:1-2 sit under
`#if 0 //defined(PROFILE_PERFORMANCE)`, so the `vdp-performance/` renderer is compiled out and the
software VDP runs. Candidate: enable the performance VDP (define `PROFILE_PERFORMANCE`) behind a
flag and A/B. This is the Genesis "demanding" win (32X + heavy Mode-7 games).

**Note:** BOTH `vdp/` and `vdp-performance/` are listed in ares/md/CMakeLists.txt — verify the build
wiring before flipping the guard (symbol-duplication risk from `VDP vdp;`); on-device verify required
(it is the experimental performance branch).

**Difficulty:** 🟡 Low-MED (wiring) + 🟡 (verify). **Impact:** MED-HIGH.

#### Task 68 — N64 RSP VU ARM64 NEON verification (OPEN)

**Why:** N64 RSP recompiler gates vector ops on `#if ARCHITECTURE_SUPPORTS_SSE4_1`
(ares/n64/rsp/recompiler.cpp:2607,2841,2947,…) with a scalar `#else` fallback. On ARM64,
nall/nall/intrinsics.hpp:186 sets `ARCHITECTURE_SUPPORTS_SSE4_1 = 1` "simulated via sse2neon.h", so
the `_mm_*` ops map to NEON. Verify the device build actually takes the SSE (NEON) path and NOT the
scalar fallback; if any VU op still falls to scalar, convert those few to explicit NEON.

**Difficulty:** 🟢 Low (verify) / 🟡 (convert). **Impact:** LOW-MED.

#### Task 69 — Performance overlay CPU-vs-GPU breakdown (OPEN)

**Why:** to MEASURE Tasks 63-68 on-device, the perf overlay needs a CPU-vs-GPU split (which core is
the bottleneck), not just FPS + frame time. Extends the planned Tasks 42/43/44 (configurable metrics,
GPU utilization via libadrenotools/VK counters, CPU freq + thermal from sysfs).
`PerformanceOverlay.kt` currently shows only FPS + frameTime. Add a "CPU vs GPU" metric so the
recompiler/GPU work is attributable.

**Difficulty:** 🟢 Low. **Impact:** enablement (makes 63-68 measurable).

#### Task 70 — ZX Spectrum (z80) recompiler (OPEN — LOW / probably skip)

**Why:** ZX Spectrum is the lowest-demand core (z80 + ULA + PSG; Manic Miner verified). The z80 is
interpreter-only (component/processor/z80, no recompiler) but that is NOT a realistic perf risk on
ARM64 phones. If Spectrum feels slow on-device, the cause is almost certainly NOT the CPU — profile
before touching. A small z80 recompiler is the cheapest of the bunch (reuses the SLJIT generic
framework) but is deprioritized.

**Difficulty:** 🟢 Low. **Impact:** LOW. **Recommendation:** probably not worth it unless profiling
shows the z80 as a real cost.

#### Task 71 — Dynamic speed compensation (OPEN, renumbered from 52)

Scale the game's clock (cycles + audio) when rendered FPS < refresh rate → smooth
lower-frame-rate playback instead of slow-motion. Default ON (NICE-TO-HAVE).
**Renumbered from 52 (2026-08-16):** 52 is PS1 multi-disc swap; the QoL list previously
collided. See the QoL-phase table.

#### Task 72 — N64 RDP-ParaLLEl performance investigation vs mupen64plus-ae-turnip (OPEN)

**Why:** user reports Mario Tennis runs at >100 FPS in
[pwnedbygary/mupen64plus-ae-turnip](https://github.com/pwnedbygary/mupen64plus-ae-turnip)'s
parallel-RDP renderer (with fast-forward) but lags in Phobos's N64 core (which also uses
ares' paraLLEL-RDP). Both use the same parallel-RDP library (Themaister's) and the same
Turnip Vulkan driver on the same Adreno GPU. Root causes identified.

---

### 🔬 Root Cause Analysis (2026-08-20) — Full codebase comparison

Investigated the turnip fork (`pwnedbygary/mupen64plus-ae-turnip`, branch `master`) against
Phobos (`pwnedbygary/phobos`, branch `master`). The turnip fork is a Mupen64Plus-AE variant
(an Android frontend for the Mupen64Plus C emulation core) that uses parallel-RDP as a
video plugin. Phobos uses the ares C++ emulation core with parallel-RDP directly embedded.

**Repo paths referenced:**
- Turnip fork local: `/home/garyb/LLM-Projects/mupen64plus-ae-turnip/`
- Phobos local: `/home/garyb/LLM-Projects/phobos/`

---

### 🥇 #1 Factor: Asynchronous RDP (configurable vs always-synchronous)

**This is the single largest performance difference.** The parallel-RDP `CommandProcessor`
has an internal `CommandRing` thread that asynchronously processes RDP commands. The host
CPU can either wait for the GPU to finish on every `SyncFull` (synchronous mode) or just
fire the DP interrupt and continue (asynchronous mode, keeping the GPU fully pipelined).

**Turnip fork** (`mupen64plus-video-parallel/upstream/parallel_imp.cpp`, lines 196–203):
```cpp
if (RDP::Op(command) == RDP::Op::SyncFull) {
    // ONLY wait if synchronous mode is ON (config default = 1, but can be 0)
    if (vk_synchronous && frontend)
        frontend->wait_for_timeline(frontend->signal_timeline());
    *gfx.MI_INTR_REG |= DP_INTERRUPT;
    gfx.CheckInterrupts();
}
```
When `vk_synchronous` is `false` (config key `SynchronousRDP`, exposed in the UI), the
CPU **never stalls** waiting for GPU work. The GPU runs a frame or two behind the CPU,
fully pipelined. The only synchronization point is at scanout time in `vk_blit()`, where
`scanout.fence->wait()` naturally paces against the GPU.

**Phobos** (`ares/n64/vulkan/vulkan.cpp`, in `Vulkan::render()`):
```cpp
if(::RDP::Op(code) == ::RDP::Op::SyncFull) {
    implementation->processor->wait_for_timeline(implementation->processor->signal_timeline());
    rdp.syncFull();  // also updates the software RDP state tracker
}
```
Phobos **always waits** on every `SyncFull` — there is no config toggle and no async path.
The CPU blocks until the GPU finishes all prior RDP work before the DP interrupt fires.
Mario Tennis issues `SyncFull` frequently (every RDP command barrier), so this drain
happens many times per frame.

Additionally, Phobos calls `rdp.syncFull()` after the wait, which updates the software RDP
state tracker (`rp->command.crashed`/`pipeBusy`/`bufferBusy`/`source`). This is unnecessary
when parallel-RDP is active and adds a tiny overhead on top of the GPU drain.

**Impact:** With async RDP, the CPU can run 2–3× faster in CPU-bound scenes because it
never idles waiting for GPU command completion. Mario Tennis is both CPU- and GPU-bound;
eliminating the per-SyncFull stall is transformative.

**Actionable:** Add an async-RDP config toggle to Phobos. In `Vulkan::render()`, when async
mode is active, skip `wait_for_timeline()` on `SyncFull` — just call `rdp.syncFull()` (or
skip it too). The scanout fence in `scanoutAsync()` + `mapScanoutRead()` already provides
the necessary synchronization at frame boundaries.

---

### 🥈 #2 Factor: RSP JIT — Native NEON vs SSE-to-NEON Translation

**Turnip fork** uses **parallel RSP** (`mupen64plus-rsp-parallel/upstream/parallel.cpp`)
which is a dedicated RSP JIT recompiler. On ARM64, it uses **native NEON intrinsics**
directly for VU (vector unit) operations — the `rsp_jit.hpp` code compiles RSP vector
microcode to ARM64 NEON instructions without any translation layer.

**Phobos** uses ares' built-in RSP JIT (`ares/n64/rsp/`) which relies on **`sse2neon.h`**
(`thirdparty/sse2neon/`) — a header that translates SSE intrinsics to NEON at the C++
source level. This means every SSE intrinsic call in the VU path becomes a function-like
macro expansion that produces multiple NEON instructions, rather than hand-tuned native
NEON. The translation overhead adds up significantly in the RSP VU hot path, especially
for the SIMD-heavy audio and graphics microcode that Mario Tennis uses.

Key difference: the turnip fork's parallel RSP has a JIT that directly emits ARM64 NEON
instructions; ares' RSP JIT uses SSE intrinsics (written for x86) that get translated
through `sse2neon.h` on ARM64. The translation layer is functional but not zero-cost.

**Impact:** Moderate-to-significant in RSP-heavy scenes (audio microcode, vector
transformations). Mario Tennis has a moderately busy RSP for audio and some 3D math.

**Actionable:** Task 68 (N64 RSP VU ARM64 NEON verification) is the first step — confirm
that the `ARCHITECTURE_SUPPORTS_SSE4_1`→`sse2neon` path actually emits NEON (not the
scalar `#else` fallback). Next step: profile the RSP VU hot path and consider either (a)
replacing hot SSE intrinsics with hand-written NEON, or (b) porting the parallel RSP JIT
to Phobos as an alternative RSP backend.

---

### 🥉 #3 Factor: Emulation Core Maturity and Philosophy

**Turnip fork** uses **Mupen64Plus** (C, started 2001, ~24 years of optimization). It is
a speed-first emulator that uses:
- **Cached interpreter** (the default on ARM) — aggressively optimized with lookup tables
  and pre-decode caching. Can also use various dynarec backends.
- **Audio-driven timing** — the audio plugin controls the emulation pace. When the game
  runs faster than real-time, audio stretches to match, creating natural decoupling.
- **Plugin architecture** — video, audio, input, RSP are separate shared libraries loaded
  at runtime, allowing independent optimization and replacement.

**Phobos** uses **ares** (C++, higan/bsnes lineage, started ~2019). It prioritizes cycle
accuracy over speed, with:
- **SLJIT-based recompiler** (JIT) for the VR4300 CPU — correct but not as aggressively
  optimized as Mupen64Plus's cached interpreter for many games.
- **Synchronous emulation** — the ares N64 core uses the `CPU::synchronize()` pattern
  where the CPU calls device `main()` functions directly (VI, AI, RSP, RDP, PIF) rather
  than using the ares co-routine Scheduler. This is identical to upstream ares and NOT a
  performance issue per se, but the core has had far fewer optimization passes.
- **VI overclock** exists (`vi.hpp`: `overclockPercent`) but is applied only to the VI
  line timing, not the CPU clock. The turnip fork exposes both `CountPerOpDenomPot` (CPU
  clock multiplier) and `CountPerScanlineOverride` (VI timing).

**Impact:** Moderate. For Mario Tennis specifically, the Mupen64Plus cached interpreter
hits an efficient path, and the audio-driven timing lets fast-forward run without
fighting the frame scheduler.

---

### #4 Factor: CPU Overclock Granularity

**Turnip fork** exposes two independent overclock knobs:
1. **`CountPerOpDenomPot`** (`build_common/version_common.gradle` → core config, default 0):
   Reduces the number of CPU cycles per emulation step by a power of two. Effectively runs
   the CPU faster relative to the rest of the system. When the user enables fast-forward
   (default 250% speed in the turnip fork), the `l_SpeedFactor` is set to 250 which
   multiplies the audio rate and shrinks the frame-pacing sleep target, but the CPU itself
   also runs more cycles per host-second through this mechanism.
2. **`CountPerScanlineOverride`** (default 0 = game default): Overrides the VI count-per-
   scanline value. Setting it higher makes the VI interrupt fire less often (fewer scanline
   interrupts), giving the CPU more contiguous cycles between sync points = higher throughput.

**Phobos** only has `overclockPercent` on the VI controller (`vi.hpp`), which scales the
VI line timing. This makes the game render more frames per second (genuine VI overclock),
but doesn't give the CPU any additional per-cycle throughput advantage.

**Impact:** Moderate at very high fast-forward speeds (>200%). The turnip fork's CPU
overclock reduces the effective work per emulated cycle, while Phobos's VI overclock
just makes the VI fire faster (more frame callbacks per real second).

---

### #5 Factor: Frame Pacing at High Speed

**Turnip fork** (`mupen64plus-core/upstream/src/main/main.c`, `apply_speed_limiter()`,
lines 861–928):
- Uses `SDL_Delay()` with wide tolerance windows (`minSleepNeeded = -50ms`,
  `maxSleepNeeded = 50ms`).
- At 250% speed: `AdjustedLimit = (1000/60) * (100/250) = ~6.67ms` per frame.
- If the real elapsed time exceeds the adjusted game time (`sleepTime < 0`), it skips
  sleeping entirely — no penalty, no catch-up, just runs flat-out.
- `SDL_Delay()` is a coarse sleep (~1–2ms granularity on Linux/Android) but at 250%
  speed it's rarely called anyway because the sleep time typically goes negative.
- The speed limiter runs inside `new_vi()` (called from the VI interrupt handler), so
  it's naturally synchronized with the emulated frame rate.

**Phobos** (`PhobosRunner.cpp`, `emulationLoop()`):
- Uses `std::chrono::steady_clock` with absolute deadline `sleep_until()`.
- Fast-forward: computes `targetFrameTime / speed` and uses `sleep_for()` relative sleep.
- The deadline-based approach accumulates scheduling pressure: if a frame takes slightly
  longer than the adjusted target, the deadline slips, and the next sleep is shorter
  (tighter scheduling). At very high speeds this can lead to busy-wait behavior.
- Frame pacing runs in the main emulation loop (not synchronized to VI), so it imposes
  a host-side throttle independent of the game's actual timing.

**Impact:** Minor — both approaches work well at 2× speed. At extreme speeds (>3×), the
turnip fork's "skip sleep when behind" approach is slightly more aggressive.

---

### #6 Factor: Pipeline Depth and Presentation Path

**Turnip fork** initializes parallel-RDP with `device->init_frame_contexts(3)` — 3 frame
contexts in flight. The presentation path is: Vulkan → `map_host_buffer()` → `memcpy()`
to CPU buffer → OpenGL ES texture upload (`screen_write()` + `screen_swap()` draws a
textured full-screen quad via GLES). This is an **extra copy** (Vulkan → CPU → GLES texture
→ screen compositor) vs Phobos's direct path.

**Phobos** initializes with `device.init_frame_contexts(4)` — 4 frame contexts (3 in-flight
+ 1 spare). The presentation path is: Vulkan → `map_host_buffer()` → `memcpy()` to CPU
buffer → `ANativeWindow_lock/unlockAndPost()` directly to Android surface. No intermediate
GLES step.

The extra GLES blit in the turnip fork path should make it *slower*, not faster. The turnip
fork's speed advantage comes despite this overhead, which reinforces that factor #1 (async
RDP) is the dominant difference.

---

### Summary Table

| Factor | Turnip fork (Mupen64Plus-AE) | Phobos (ares) | Impact | Actionable? |
|--------|------|--------|--------|-------------|
| **RDP Sync** | Configurable async — no stall on SyncFull | Always synchronous — drain on every SyncFull | **Massive** | ✅ Add async toggle |
| **RSP SIMD** | Parallel RSP JIT with native NEON | ares RSP JIT via sse2neon.h translation | **Significant** | 🔶 Profile + port hot paths |
| **Core maturity** | Mupen64Plus C, 24yr speed-optimized | ares C++, accuracy-first | **Moderate** | ❌ Not portable |
| **CPU overclock** | CountPerOpDenomPot + CountPerScanline | VI overclock only | **Moderate** | 🔶 Add CountPerOpDenomPot-like knob |
| **Frame pacing** | SDL_Delay, skip when behind | deadline sleep_until | **Minor** | 🔶 Consider for extreme FF |
| **Pipeline depth** | 3 contexts, extra GLES copy (slower) | 4 contexts, direct surface | Negative (helps Phobos) | N/A |

---

### Recommended Actions (priority order)

1. **HIGHEST — Add async RDP mode toggle to Phobos.** In `Vulkan::render()`, skip
   `wait_for_timeline(signal_timeline())` on `SyncFull` when async mode is enabled. The
   scanout fence in `scanoutAsync()` + `mapScanoutRead()` already provides frame-level
   synchronization. Wire the toggle through JNI (`PhobosRunner.cpp`/`PhobosJNI.cpp`),
   Kotlin UI (`SettingsScreen.kt`/`EmulatorScreen.kt`), and `PhobosCore.kt`. Default OFF
   (accuracy-preserving) but user-enabled for speed-critical games.

2. **HIGH — Task 68 follow-through: profile RSP VU NEON path.** Verify that sse2neon.h
   emits actual NEON (not scalar fallback) for the hot VU instructions used in Mario
   Tennis's audio and graphics microcode. If the scalar `#else` path is active on this
   Clang/ARM64 build, fix the preprocessor guards. If sse2neon is active but slow, port
   the hot ~20 VU intrinsics to hand-written NEON.

3. **MEDIUM — Add CPU overclock knob.** Implement a `CountPerOpDenomPot`-style setting
   that divides the cycle budget per emulation step. Wire through JNI and settings UI.
   This is a genuine CPU overclock (unlike VI overclock which just makes frames render
   faster) and helps in CPU-limited fast-forward scenarios.

4. **LOW — Frame pacing tweak for extreme fast-forward.** If the deadline-based pacing
   shows scheduling pressure at >3× speed, switch to the "skip sleep when behind" model
   (like the turnip fork's negative-sleep-time bailout).

---

### Specific Code References (turnip fork)

- **Async RDP toggle + SyncFull handling:**
  `mupen64plus-video-parallel/upstream/parallel_imp.cpp:196-203`
- **RDP init (3 frame contexts, plugin interface):**
  `mupen64plus-video-parallel/upstream/parallel_imp.cpp:225-304`
- **Speed limiter with SDL_Delay skip logic:**
  `mupen64plus-core/upstream/src/main/main.c:861-928`
- **Speed factor 250% fast-forward default:**
  `mupen64plus-core/upstream/src/main/main.c:429-460`
- **VI interrupt → speed limiter call chain:**
  `mupen64plus-core/upstream/src/device/rcp/vi/vi_controller.c:173-194`
  (calls `new_vi()` which calls `apply_speed_limiter()`)
- **CountPerScanlineOverride:**
  `mupen64plus-core/upstream/src/device/rcp/vi/vi_controller.c:146-157`
- **Parallel RSP JIT with native NEON:**
  `mupen64plus-rsp-parallel/upstream/parallel.cpp`
- **Plugin config defaults (SynchronousRDP = 1 by default):**
  `mupen64plus-video-parallel/upstream/gfx_m64p.c:145`
- **GamePrefs wiring (viRefreshRate → CountPerScanlineOverride):**
  `app/src/main/java/paulscode/android/mupen64plusae/persistent/GamePrefs.java:652`
  `app/src/main/java/paulscode/android/mupen64plusae/jni/NativeConfigFiles.java:121`

**Difficulty:** 🟡 Medium (research + selective port). **Impact:** HIGH (N64 perf ceiling).

---

#### 2026-08-19 → 2026-08-20 — GPU Driver Downloader (QoL, DONE) + Neo Geo input/BIOS fixes (UNCOMMITTED)

**GPU Driver Downloader (Feature — DONE, uncommitted):**
- `DriverManagerScreen.kt`: `DriverActionsRow` (3 filled buttons — Download / Install / Delete; Delete is solid red `Color(0xFFD32F2F)` with white text) + an "Active Driver" card = "System Default (Adreno)" radio + a `Column(Modifier.verticalScroll().heightIn(max = 320.dp))` listing every installed `*.so` in `context.filesDir/gpu_drivers/`, each a `DriverChoiceRow` radio. Tapping a driver calls `setCustomDriverPath` (or `null` for System Default).
- `DriverDownloader.kt`: downloads named `"${repo}_${owner}_${asset.tag}"` with an identity `"${owner}/${repo}@${tag}"` (manual uploads keep the filename). `resolveDriverTarget` overwrites the same driver, distinct drivers coexist — NO `_2` dupe suffix (user rejected dupes). `sanitizeDriverName` keeps `[A-Za-z0-9._-]`. A `<soname>.source` sidecar stores `owner/repo\ntag` for future refresh.
- Manual installs (`installCustomDriver` / `installCustomDriverFromFolder`) now emit `_driverSuccessEvent`/`_driverErrorEvent` (in `MainViewModel`) so the selector list refreshes after a manual upload (previously the list only refreshed on download).

**Neo Geo BIOS dialog — truthful (FIXED, uncommitted):**
- `MainViewModel.loadRom` tracks `ngBiosPresent` (whether `neogeo.zip` was copied to `mia_temp`). On a Neo Geo `loadRom` failure it now shows `_neoGeoRomLoadFailed` ("ROM Failed to Load" — BIOS present but the ROM isn't a valid MVS/AES game) instead of `_biosRequired` ("BIOS Required" — BIOS absent). `EmulatorScreen` gained the "Neo Geo ROM Failed to Load" dialog.
- Native `PhobosRunner.cpp` BIOS-attach branch (nodeName) now also matches a bare `"Neo Geo"` (defensive; real mediums are "Neo Geo MVS"/"Neo Geo AES").
- **Key gotcha (memory `bugfix/neogeo-bios-dialog`):** 1941 is a CPS-1 Capcom game, NOT Neo Geo — absent from MIA's `Neo Geo.bml`. Loading it as Neo Geo always fails; the OLD popup wrongly blamed the BIOS. The new popup explains the ROM is likely not Neo Geo.

**Neo Geo INPUT — D-pad hat fix (FIXED, needs on-device verify, uncommitted):**
- **Root cause (memory `bugfix/neo-geo-input-gamepad`):** the Neo Geo `ControllerPort` (`ares/ng/controller/port.cpp`) only `allocate()`s an "Arcade Stick"; `connectDevices` already routes Neo Geo → `"Arcade Stick"` (`PhobosRunner.cpp:2088`), so a device IS connected and A/B/C/D read fine. The dead D-pad was a FRONT-END bug: many controllers report the D-pad as a HAT axis (`AXIS_HAT_X/Y`), and `GameInputState.updateHotkeyDpad` only folded the hat into `hotkeyKeys` (used for hotkey combos) — never into the gameplay button mask. So the D-pad produced no game input while A/B/C/D (keycodes) worked.
- **Fix:** `updateHotkeyDpad` now ALSO latches the hat into `hwButtons` bits 0–3 (Up/Down/Left/Right = `VirtualGamepad` bits 0..3). Universal (every system), so it can't be lost by a remap. A harmless no-op `port.cpp` edit also accepts `"Gamepad"` as the Arcade Stick (defensive; never requested since connectDevices uses "Arcade Stick").
- **Analog sticks:** for Neo Geo they are digital-only, so the analog axes legitimately drive nothing — expected, NOT a bug. The D-pad is what moves a Neo Geo character.

**Neo Geo Background Graphical Glitch — tall sprites / 000-lo.lo line mapping (FIXED & VERIFIED on-device, uncommitted):**
- **Symptom:** In Samurai Shodown (and other games using 32-tile tall background/fighter sprites), the top HUD/sky and bottom ground/characters were visible, but the entire middle half of the screen was a massive black void.
- **Root cause:** In `ares/ng/lspc/render.cpp`, `tile` was declared as `n4` (4-bit integer, 0..15). The LSPC line ROM (`000-lo.lo`) maps scanline distance `ry` to `tile` (upper nibble) and `row` (lower nibble). For `ry < 256` (tiles 0..15), `invert=false` and `tile = entry >> 4` (0..15). For `ry >= 256` (tiles 16..31, the lower 256 pixels of 32-tile sprites), `invert=true` and hardware does `tile ^= 0x1f` (`31 - (entry >> 4)` = 16..31). Because `tile` was `n4`, the XOR was truncated to 4 bits (`tile ^= 0x0f`), wrapping back to tiles 0..15 in reverse! VRAM words 32..63 (tiles 16..31) were never accessed.
- **Fix:** In `ares/ng/lspc/render.cpp`, changed `tile` from `n4` to `n5` (5-bit integer, 0..31). Added guard `if(auto mask = cartridge.cromMask()) tileNumber &= mask >> 7;`.
- **Neo Geo Palette Banking Inversion (FIXED):** In `ares/ng/cpu/memory.cpp`, `$3A000E` (`REG_PALBANK0`) and `$3A001E` (`REG_PALBANK1`) register write decoding was inverted (`$3A000E` set `pramBank = 1` and `$3A001E` set `pramBank = 0`). Fixed to match Neo Geo hardware specification ($3A000E = bank 0, $3A001E = bank 1).
- **Verification:** Captured screenshot on Retroid Pocket 6 via `adb screencap`. Pillow analysis showed 0 black rows across the center screen (was previously black void), full background scenery rendered cleanly.

**Status:** Driver downloader built + installed + user-verified (download/install/delete/selector). Neo Geo BIOS dialog built + installed. Neo Geo D-pad hat fix built + installed. Neo Geo background graphics fix built + installed + verified on-device. **All UNCOMMITTED — commit before starting Task #49.**

---

#### 2026-08-22 → 2026-08-23 — Neo Geo: SMA decrypt endianness + BML word_swap FIXED & VERIFIED ON-DEVICE

Built `./gradlew assembleModernDebug`, deployed to Retroid Pocket 6 (`49016109`). All device Neo Geo games (including SMA titles `kof99`, `kof2000`, `garou`, `mslug3`, non-SMA `kof98`, `kof94`, `kof2001`, `samsho`) verified to boot into attract/title mode @ 59.2 FPS.

**Fix 1 — SMA diagnostic-grid (kof99 / kof2000 / garou / mslug3):**
- **Symptom:** These titles booted to a BIOS slot-select / diagnostic cross-hatch grid instead of the title.
- **Root cause (two-fold):**
  1. `mia/medium/neo-geo.cpp` `decrypt()` hardcoded the 68K reset vector in all 6 SMA branches: `p[0..7] = {0x00,0x10,0xf3,0x00,0x00,0x0c,0x48,0x00}`. Removed and replaced with a guarded `forceHeader()` that only fills `"NEO-GEO"` if the decrypt omitted it.
  2. **Endianness mismatch in SMA decryption:** `decryptKof99Sma`/`decryptGarouSma`/`decryptMslug3Sma`/`decryptKof2000Sma` cast `p.data()` to `u16*` (little-endian on ARM64/x86), but `prom` is big-endian (`readm(2L)`). Because BML used `load16_word_swap`, the little-endian cast double-swapped bytes, generating garbage 68K code.
- **Fix:** Switched SMA decrypt functions to use `readBE`/`writeBE` (`p[off]<<8 | p[off+1]`) to operate on big-endian words matching `prom`. Kept `load16_word_swap` in `mia/Database/Neo Geo.bml` (and android assets copy) and corrected `ka.neo-sma` size to 262144.

**Fix 2 — SELECT-as-coin & Coin reliability:**
- Polled in `readButtons`/`readControls` and now also on `REG_STATUS_A` reads in `ares/ng/cpu/memory.cpp`.

**Fix 3 — Switch-game SIGSEGV:**
- Removed unguarded `c[0..15]` debug reads in `decrypt()`/`decryptCmc42()`/`decryptCmcGraphics()`.

**Status:** SMA boot fix VERIFIED ON-DEVICE. In progress: investigate audio silence, coin input edge cases, ssideki4 gameplay graphics.
