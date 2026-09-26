# Phobos implementation plan — canonical roadmap and status

**Last reconciled: 2026-09-25** (tasks 50 and 72 updated with device-measured N64
performance and aspect/preview work — see the
[performance audit](performance-audit.md#2026-09-25-device-profiling-retroid-pocket-6-and-changes);
task dispositions and safeguards from 2026-09-24 for the touch-controls/performance work
in the [handoff](handoff.md#touch-controls-overhaul-and-performance-scan--2026-09-24-in-progress)
and the rest of the 2026-09-15 reconciliation stand). This is the authoritative current roadmap.
It is deliberately compact: indexed dated evidence from the former 1,874-line
plan is preserved in [implementation-history.md](implementation-history.md),
whose final non-operative appendix contains the complete original snapshot
with former headings and task IDs intact. A historical report is not fresh
verification, and an open task is not authorization to change code.

## How to use this plan

1. Read [development-process.md](development-process.md) before any change.
2. Use this document for current status, priority, disposition, prerequisites,
   and stop conditions.
3. Use [performance-audit.md](performance-audit.md) for the evidence-ranked
   performance backlog and [mario-tennis-benchmark.md](mario-tennis-benchmark.md)
   for the matched native comparison protocol.
4. Use [implementation-history.md](implementation-history.md) for historical
   context. Links below target exact archival task anchors; do not reactivate
   an archived “next step” merely because it is quoted there.
5. Update this plan and [handoff.md](handoff.md) when an eligible experiment
   produces independently recorded evidence.

## Current evidence boundary

The 2026-09-15 reconciliation was documentation-only. The 2026-09-24 session changed
source at the user's explicit request (touch controls, behavior-preserving host
optimizations, opt-in Asynchronous RDP, the Rogue Squadron hold, save-state previews).
It compiles for both flavors and its host unit tests pass, but nothing has run on a
device or been measured; it changes no timing default or save format.
There are still zero fresh native performance runs, no matched settings, and no
comparative speedup. Historical device reports remain useful regression targets but are
not current measurements.

The user-supplied reference is:

- [mupen64plus-ae-turnip](https://github.com/pwnedbygary/mupen64plus-ae-turnip),
  stable tag `v336`, resolved read-only to
  `dc955483a97daa99cb1f9db06e2334464fa1664d`;
- debug/DD branches are excluded from this comparison;
- [upstream ares](https://github.com/ares-emulator/ares) is the upstream
  context, not a drop-in performance or behavior reference;
- the tag contents and the report that it is “stable” were supplied by the
  user and pinned read-only, not independently measured here;
- a source tag does not identify an installed or attested APK. Device, driver,
  ROM, settings, APK identity, and traces remain required before comparison.

## Non-negotiable safeguards

- Measure before changing a hot path. Do not claim a speedup from source
  inspection, FPS anecdotes, a different emulator, or an unpaced run.
- Preserve timing defaults and guest-cycle policy. The historical 60-FPS floor
  is a compatibility requirement, not permission to overclock or alter timing.
- **SyncFull is not eligible for a blind async bypass.** A wait observed in
  `ares/n64/vulkan/vulkan.cpp` and the reference command ring is only a
  candidate measurement site. Before any async proposal, audit DP interrupt
  ordering, live/hidden RDRAM visibility, framebuffer reads, readback barriers,
  reset, state save/load, quit/reload, unload/abandon, and timeout ownership.
  Keep the current wait until an independently reviewed ownership model and
  lifecycle/accuracy tests exist. **2026-09-24:** the user explicitly authorized an
  opt-in *Asynchronous RDP* setting (N64 Experimental, default off). The default
  stays synchronous; the opt-in path drains the GPU before state save/load and
  reset ([performance audit](performance-audit.md#2026-09-24-scan-and-behavior-preserving-changes)).
- **RSP SIMD is not a proven native-NEON advantage.** The ARM64
  `ARCHITECTURE_SUPPORTS_SSE4_1`/`sse2neon` path may generate NEON, but source
  intrinsics do not establish scalar fallback, native-NEON superiority, or a
  workload benefit. Inspect exact release objects and sample RSP share before
  proposing an instruction rewrite or backend replacement.
- **Presentation is not a proven reference advantage.** Both implementations
  have maps/copies/waits/locks and window operations. Measure map/fence wait,
  copy/format conversion, window lock/post, and presented-frame outcomes
  separately; never infer a speedup from a path diagram. (2026-09-24: the two
  discarded CPU frame passes on the N64 Vulkan path were removed as a
  behavior-preserving change; the effect is unmeasured.)
- Never acquire `vulkan.mutex` in an abandon path. Preserve race-free
  framebuffer ownership and DP interrupt order.
- Do not uninstall, clear app data, overwrite unbacked saves, or publish ROMs,
  firmware, keys, raw captures, or diagnostic binaries.
- Task #4, **audio-buffer drain reuse**, is cancelled. It is not authorized and
  must not become the next automatic change. Reopen only with explicit scope
  and measurements.

## Current state

### Stable or historically resolved, pending normal regression discipline

The former plan reported fixes for broad core loading, N64 rendering/reset,
N64DD support and persistence, GBA/ZX RTC/tape behavior, PS1 disc/audio
behavior, multi-stream audio, input/controller handling, save/state paths,
swap-screen navigation, driver selection, Neo Geo MVS/AES boot/rendering, and
Neo Geo CD boot/rendering. The dated evidence and exact task IDs are indexed in
the [archive](implementation-history.md).

This status is not a new verification pass. In particular, Task #61's former
signing report is historical; release/update safety is owned by the separate
Task #2, now cancelled and not authorized to resume. Do not represent old signing
evidence as a current APK identity.

### Explicit current residuals

- **NGCD-M3:** title-menu text fixed; on 2026-09-24 the user reported the
  SamSho CD title menu renders correctly (see
  [handoff](handoff.md#ngcd-m3-title-menu-text-fix--2026-09-24)). `0xe2dd` now
  writes the byte-swapped word first. FIX/PCM/Z80 DRAM receive the source
  bytes in order, as in Geolith, libretro NeoCD and MAME, and SPR DRAM
  matches Geolith and NeoCD. The user's screenshot is consistent with the
  FIX row-pair swap the old order causes; `tests/ngcd/run-tests.sh` covers it
  on the host. The historical `0xfc2d` phase candidate is ruled out. The CD
  sprite tile index (MSB field folded into bits 12..15) and the CD Z80
  program memory are separate issues recorded in the handoff. See
  [archived Task NGCD-M3](implementation-history.md#task-ngcd-m3).
- **Neo Geo compatibility:** broad matrix coverage and any remaining
  game-specific audio/input/graphics reports need a fresh, bounded matrix
  pass; the core-level fixes are historical evidence, not a blanket claim
  that every set is compatible.
- **Task #2 release APK update safety:** cancelled; compatibility remains
  unverified. This plan does not reopen signing or alter release configuration.
- **Performance comparison:** the v336 source pin is now identified, but the
  attested reference APK, Phobos APK identity, target/device/driver/settings,
  legal ROM hash, and traces are still missing.

## Ranked current roadmap

Ranks are eligibility order, not measured impact. No item below authorizes a
default change or an unmeasured optimization.

### P0 — establish evidence before optimization

1. **Matched Mario Tennis comparison and source-to-APK identity.** Use the
   benchmark protocol with Phobos and the user-supplied `v336` reference:
   same device, driver, ROM, scene, settings, warm/cold state, paced versus
   separately labelled throughput modes, and repeated traces. Stop on
   correctness, shader, lifecycle, thermal, or audio regressions.
2. **Low-overhead attribution.** First use external Perfetto/scheduler and
   simpleperf traces. If they cannot attribute a candidate, add only a
   separate reviewed, opt-in, fixed-size stage recorder with measured
   recorder overhead. Keep emulated timing unchanged.

### P1 — lowest-risk measurements from the audit

3. **Presentation and copies.** Measure `vi.cpp` map/copy work and
   `PhobosRunner.cpp` map/format/window operations as separate stages,
   including fence waits and presented-frame intervals. Candidate impact is
   unknown; do not redesign ownership from a diagram.
4. **SyncFull/queue waits.** Count wait occurrences and wall-time
   distributions, correlate worker scheduling and GPU timestamps, and keep
   DP/RDRAM/lifecycle safeguards. A wait histogram may justify a later design
   review; it does not authorize skipping the wait.
5. **Pacing/observability.** Separate sleeping, runnable, CPU-running, VI
   emulation, presented frames, and GPU/fence waits. The existing overlay is
   not GPU attribution and FPS alone is insufficient.
6. **Audio allocation/diagnostic cost.** Measure drain allocations, lock time,
   xruns, and synchronized occupancy before considering buffer reuse. This is
   a new experiment only if explicitly authorized; cancelled Task #4 remains
   cancelled.

### P2 — conditional code investigations

7. **Task 68 RSP code-generation verification.** Disassemble the exact ARM64
   release objects and sample RSP CPU share. Only then test a specific
   instruction sequence against independent semantic fixtures.
8. **VI wide-mode/fallback accounting.** Count mode transitions, fallback
   frames, and per-mode cost while retaining Mario Tennis and Rogue Squadron
   regressions.
9. **CPU/JIT/dcache accounting.** Sample block exits, invalidation, cache
   activity, and CPU share with overhead calibration. Do not revive Task 58's
   bypass; Task 59 remains parked pending a coherent write-through design.
10. **Other cores.** Profile PS1, SNES, GBA, MD, SFC, and other systems before
    considering a recompiler or renderer replacement. Protect threaded video,
    save/state, and multi-stream audio behavior.

### P3 — product/QoL work after evidence and core stability

Controller hierarchy, save import/export, performance metrics, tape swap,
dynamic speed compensation, Run-Ahead wiring, and other non-N64 features
remain inventoried below. They are not silently dropped merely because the
performance audit is N64-focused. Also retained: a full **UI theme system**
(IDE colorway presets; optional retrowave; preserve system card art) and a
**MangoHud-/GameNative-style performance overlay** overhaul — both requested
2026-09-25; see the inventory rows.

## Open-task inventory and disposition

This table is the canonical disposition of every open or parked item found in
the former plan. “Deferred” means retained, not deleted; “closed” means the
former record reported completion, not that this pass reverified it.

| Former ID / heading | Area | Current disposition |
|---|---|---|
| NGCD-M3 | Neo Geo CD title-menu text residual | **Resolved 2026-09-24 (user-reported title-menu check).** BIOS menu/HUD colour comparison not yet reported. Screenshot consistent with FIX row pairs swapped; `0xe2dd` byte-wide delivery matches three references (SPR matches Geolith/NeoCD), host harness `tests/ngcd/`; `0xfc2d` phase candidate ruled out; CD sprite tile index and CD Z80 program memory recorded in the handoff. No global fetch change. [Handoff](handoff.md#ngcd-m3-title-menu-text-fix--2026-09-24), [Archive](implementation-history.md#task-ngcd-m3) |
| 10c/10a remainder: Neo Geo MVS/AES compatibility | Neo Geo | **Open, bounded matrix pass.** PCE/ZX portions are historically resolved; Neo Geo core-level fixes are historical, so do not claim all sets verified. [10a](implementation-history.md#task-10a), [10c](implementation-history.md#task-10c) |
| Touch controls overhaul | Touch input/UI | **Implemented 2026-09-24; compiles, 41 host unit tests pass.** Per-family layouts, multi-touch engine, editor, settings, quick-tap delivery, native input-map fixes. Needs the device checklist. [Design and findings](touch-controls.md) |
| 13a | Per-core custom layouts | **Implemented for touch (2026-09-24, not device-tested):** per-family, per-orientation layouts and editor. Physical-controller layouts stay with 13b–13d. [Archive](implementation-history.md#task-13a) |
| 13b, 13c, 13d | Controller rebinding/multi-player | **Deferred QoL, retained.** Implement as one hierarchy (global → core → game) after core stability; [13b](implementation-history.md#task-13b), [13c](implementation-history.md#task-13c), and [13d](implementation-history.md#task-13d) are archived. |
| 15a, 16, 31 | Pause-menu quick actions, touch resize/reposition, PS1 shapes | **Implemented 2026-09-24, not device-tested:** quick-action row (save, load, screenshot, controls, reset); layout editor; drawn PlayStation symbols with the correct bits (the old overlay's labels were wrong). [Touch design](touch-controls.md) |
| 14, 15, 18, 19 | Responsive UI, polish/shader menu, video options, UI coloration | **Deferred QoL, retained.** Portrait picture placement changed with the touch work; no other instruction. [Archive index](implementation-history.md#task-14) |
| UI theme system | App chrome / Material theme | **Deferred QoL, retained (user request 2026-09-25).** Eventual overhaul: go big — clean modern aesthetic, rich selectable themes based on common IDE colorways (One Dark, Dracula, Solarized, GitHub, Nord, etc.), plus optional retrowave/synthwave inspired by the theme selector in the user's mupen64plus-ae-turnip fork. **Keep system library cards mostly as they are** (preserve the existing card art / look); everything else (settings, pause menu, chrome, typography, accents, motion) is fair game. Extends Task 19. Do not start until core stability and higher-priority open work allow. No implementation in the N64 perf branch. [Task 19](implementation-history.md#task-19) |
| 42, 42b, 43, 44, 69 | Perf monitor metrics + presentation | **Implemented 2026-09-25 on branch `feature/perf-hud-2026-09`; builds, 50 host tests pass, verified on the RP6** (every row populated with live values, including CPU/GPU temperatures, GPU load and clock; battery watts hidden while on USB power; no thermal headroom on this device). MangoHud-style HUD modeled on MangoHud and GameNative. It shows colour-coded rows: FPS with frame time and speed %, a frame-interval graph with a target line and stutter marks, emulation-thread CPU load with core and clock, and GPU load, clock and temperature. Also process memory with system RAM, battery % with watts and temperature, Android thermal status, system and resolution, and clock with time played (unpaused time since the game loaded, counted natively). Every value is read at run time and hidden when the device doesn't expose it (no fabricated counters). Presets are FPS only, Essential, Battery and Full; layout is vertical or horizontal, with size and opacity sliders and a live preview in settings. Drag to move; the old corner resize handle is gone. Two fixes over the old overlay: "RAM" was the JVM heap (now process RSS plus system RAM), and "Core" was sampled on the UI thread (now the emulation thread). Game FPS versus VI rate (42b) is still open. [42](implementation-history.md#task-42), [42b](implementation-history.md#task-42b), [43](implementation-history.md#task-43), [44](implementation-history.md#task-44), [69](implementation-history.md#task-69) |
| 49 | N64 save import/export | **Open, retained.** UI and format mapping require save-safe review; not a performance task. [Archive](implementation-history.md#task-49) |
| 50 | Save-state screenshots | **Implemented 2026-09-24; seen working on device 2026-09-25.** Each save also writes `<state>.thumb` (PNG data under a non-image extension, so gallery apps skip it) next to the state in SAF or internal storage; a failed capture drops only the preview. The pause menu shows the slot's preview and save time; delete removes both. Since 2026-09-25 the preview is stretched to the picture's display aspect (an N64 progressive scanout is 640×240 but shows at 4:3); previews saved earlier stay squashed until re-saved. [Archive](implementation-history.md#task-50) |
| 51 | ZX multi-tape swap and multi-file ZIP picker | **Deferred QoL, retained.** [Archive](implementation-history.md#task-51) |
| 59 | Write-through dcache bypass | **Parked high-risk.** Task 58's direct bypass broke DMA; no default/per-game enablement. [Archive](implementation-history.md#task-59) |
| 60 | Per-game hash overrides | **Parked.** Revisit only with a demonstrated, measured need and explicit timing review. [Archive](implementation-history.md#task-60) |
| 61 | Proper release APK signing / in-place upgrades | **Historical record claimed fixed, but current release/update compatibility is unverified; Task #2 is cancelled.** No current signing assertion or release change in this documentation pass. [Archive](implementation-history.md#task-61) |
| Mischief Makers | N64 RSP accuracy | **Revisit 2026-09-25: in-game on RP6 (title → save select → gameplay) at ~60 FPS after the Count/Compare JIT-budget wrap.** Previously parked as a fatal trap at `0x800008b8` after Start (corrupted IRQ dispatch from bad RSP audio results). Likely fixed as a side effect of restoring contiguous RSP slices (syncs ~3M/s → ~46k/s); not a VU guess patch. Keep the parked diag; do not claim ares RSP audio microcode is fully accurate. [Archive](implementation-history.md#mischief-makers) |
| 63 | PS1 R3000 recompiler | **Deferred, profile first.** Interpreter fallback and independent correctness fixtures required. [Archive](implementation-history.md#task-63) |
| 64 | PS1 GPU off-CPU / Vulkan | **Deferred, split into measured blitter versus high-risk renderer work.** [Archive](implementation-history.md#task-64) |
| 65 | SNES 65816 recompiler | **Deferred, profile first.** [Archive](implementation-history.md#task-65) |
| 66 | GBA ARM7TDMI recompiler | **Deferred, profile first.** [Archive](implementation-history.md#task-66) |
| 67 | Genesis performance VDP | **Deferred, flag/wiring and compatibility review first.** [Archive](implementation-history.md#task-67) |
| 68 | N64 RSP VU ARM64 SIMD | **P2 verification only.** No “native NEON” or speed claim. [Archive](implementation-history.md#task-68) |
| 69 | CPU/GPU performance overlay | **Deferred enablement work.** External traces precede extra instrumentation. [Archive](implementation-history.md#task-69) |
| 70 | ZX Z80 recompiler | **Deferred/likely skip.** Profile before touching. [Archive](implementation-history.md#task-70) |
| 71 | Dynamic speed compensation | **Deferred product/accuracy decision.** Not a generic speedup. [Archive](implementation-history.md#task-71) |
| 72 | N64 RDP-ParaLLEl comparison | **P0/P1 evidence investigation.** Use pinned v336 only; debug DD branches excluded; no code transplant. 2026-09-24: ranked gap hypotheses recorded; the user authorized an opt-in (default-off) Asynchronous RDP setting, and the redundant N64 presentation copies were removed (both compiled, not measured). 2026-09-25: measured on the Retroid Pocket 6 against the user's Mupen64Plus-AE `Parallel` profile (same Turnip driver) with simpleperf and a save-state benchmark; root causes fixed (a sync after every JIT block from a Count/Compare clamp, a futex per audio sample and per RDP command, PLT/GOT, state-key and block-lookup cost); Mario Tennis gameplay 50.6 → 58.4 FPS average (Mario vs Boo save state, 30 s × 2, Asynchronous RDP on; default is off). Same-day follow-up: same-section unconditional `J` linking (~18k links/s; emulation-thread CPU ~109% vs ~123% of one core at ~59.8 FPS with Async RDP on — first recorded as off, but the device setting was enabled). Second follow-up ([PR #4](https://github.com/pwnedbygary/phobos/pull/4), branch `feature/n64-accuracy-neutral-perf-2026-09`): cross-section `J` and not-taken-edge linking with runtime PC/state-key gates, RSP pipeline hash skip, `Screen::frame` condition variable; opt-in N64 Experimental speed hacks (faster CPU sync, skip cache timing, RSP task mode; default off). Measured one at a time: no clear FPS gain in Mario vs Boo, and Faster CPU sync stalls Conker's pub. Root cause (confirmed with a debug-gated log): Count advances only at sync, so Conker's ~25 µs libultra timer is written behind Count when a larger sync step intervenes, and the thread waits one full Count wrap (91.6 s). Fixed the same day: MFC0 Count includes clocks run since the last sync, MTC0 Count/Compare account for the sync cadence, and the timer check is wrap-safe. With Faster CPU sync on, Conker then ran 280 s stall-free. Default settings with the fix: Mario vs Boo 58.5 / 58.8 FPS mean, smoke titles clean. Next: decide whether to keep Faster CPU sync. [Audit](performance-audit.md#2026-09-25-device-profiling-retroid-pocket-6-and-changes), [Archive](implementation-history.md#task-72) |
| Rogue Squadron transition hold | N64 VI | **Refined 2026-09-24, compiled; needs device validation.** The fixed ~3.5 s hold also armed at boot for games starting in a wide VI mode (frozen black frame). It now arms only on a real transition and ends as soon as the RDP completes a frame in the displayed buffer (3.5 s cap kept). Scanout coherency range and CPU fallback returned to the upstream VI geometry. [Handoff](handoff.md#touch-controls-overhaul-and-performance-scan--2026-09-24-in-progress), [Archive](implementation-history.md#rogue-squadron) |
| Run-Ahead audit | Feature completeness | **Audited 2026-09-24: confirmed inert** (the setting was stored but never reached native code). The switch is hidden; the stored preference is kept. Implementing it needs per-frame serialize/run/restore with video and audio suppressed on the hidden frame; impractical on N64 (GPU-side RDRAM), a separately reviewed change elsewhere. [Archive](implementation-history.md#feature-completeness) |
| Task #2 | Release APK update safety | **Cancelled.** Not reopened here; update compatibility remains unverified. |
| Task #4 | Audio drain-buffer reuse | **Cancelled. Not authorized and not next automatic change.** |
| Saturn | Core/product scope | **Out of scope** absent an explicit product/licensing decision. |

### Closed historical items retained for lookup

The former plan's completed IDs remain part of the historical record and are
not silently deleted: Task 9, 5a, 5b, 6, 20, 22, 23, 29, 30, 32, 33, 35, 38,
39, 40, 41, 45, 47, 48, 52, 57, 58 (reverted), 61 (historical signing),
62, 10c's PCE/ZX portions, SG, ZX, N64DD reset/RTC/save work, the swap-screen
feature, Task 46, and NGCD-M2. See the exact archival headings in
[implementation-history.md](implementation-history.md).

## Evidence-backed experiment acceptance

Before an implementation task leaves “deferred” or “parked,” record:

- exact source revision, APK/native-library identity, device/OS/driver, ROM
  identity, settings, scene, thermal state, and cache state;
- baseline and candidate runs with repeated paired order, presented-frame
  distributions, CPU/RSP/renderer/pacing/wait stages, audio xruns/latency,
  visual correctness, and lifecycle tests;
- the actual commands, toolchain, unavailable checks, limitations, and
  independent review result;
- a rollback path and a clear stop condition. No source-only “2–3×,”
  “massive,” “native NEON,” or “direct presentation” claim is sufficient.

The next eligible action is to device-check the 2026-09-24 work (see the handoff), then
the P0 identity/trace gate — not a
default SyncFull change, RSP rewrite, audio-buffer reuse, CPU overclock, or renderer
replacement.