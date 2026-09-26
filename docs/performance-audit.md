# Phobos performance evidence baseline — 2026-09-15

## Identity, scope and actual checks

Documentation/static audit only; no runtime edits, speculative optimization,
instrumentation, signing changes or device actions beyond device enumeration.

- `git status --short`: clean before this work. Branch `master`, HEAD
  `b34337376bd5643f2afdec75241f4c5e1e81d9b8`.
- `git ls-remote origin refs/heads/master`:
  `f1174e7654141accad40b9ffc2c7d978e93a00f0`. Ancestor check against HEAD exited 0.
  Already synchronized; no fetch/merge/reset was needed. Reconciled import preserved.
- `git log -5 --oneline` confirms reconciliation `3678299b7` and subsequent setup.
  Path history for Vulkan/Runner terminates at imported `27e1d7574`; do not
  treat every historic handoff commit as independently verified local provenance.
- `git submodule status`: libadrenotools pinned at
  `8fae8ce254dfc1344527e05301e43f37dea2df80`, uninitialized (`-` prefix).
  A new native build needs initialization; it was not necessary for this doc scope.
- `javac -version`: 17.0.15. SDK directories present, NDK 28.2.13676358 present.
  Wrapper inspection: Gradle 9.6.0; version catalog AGP 9.3.2; CMake request 3.22.1.
  No build run this iteration; existing APKs are not proof of a current-source build.
- `.local/android-sdk/platform-tools/adb devices -l`: empty device list.
  Attachments contain the two process guides, not native captures.
- Existing release APK SHA-256 inventory (not installed or source-attested):
  legacy `9e335e9cd0830c18bc2845b586209699ba8dd0bb82bded7ad1bf3605cc994795`;
  modern `5f99cffe12607f6aa0158b7695e5cf06502bdc58368fde750dffc52319de8b92`.
  Signing/update safety is owned by separate work; these APKs are not benchmark-ready
  merely because they exist.

Available capabilities: source editing, shell, remote read, independent read-only
specialists and review. No connected target, identified reference APK, ROM identity,
matched settings or frame traces. Consequently **zero fresh native performance
runs**, no measured ranking by speed, no parity or speedup claim.

## Pinned user-supplied reference, not an attested installed configuration

The user supplied the stable reference
`https://github.com/pwnedbygary/mupen64plus-ae-turnip` tag `v336`.
Read-only `git ls-remote` resolves that tag to
`dc955483a97daa99cb1f9db06e2334464fa1664d`, matching the source revision
previously inspected. Debug/DD branches are excluded. The tag contents and
“stable” characterization are user-reported and were not independently
measured here. The source pin is useful for comparison, but the installed
reference branch/APK, Phobos APK, device, driver, settings, and benchmark
identity remain unconfirmed.

Upstream context is `https://github.com/ares-emulator/ares`; it is not a
drop-in performance reference or permission to transplant code.

All reference paths below are relative to that fork at that revision:

- `mupen64plus-video-parallel/upstream/parallel_imp.cpp:148-225`: commands enqueue;
  SyncFull waits only when `vk_synchronous` is true.
- `.../upstream/gfx_m64p.c:139-156,233-255`: plugin default `SynchronousRDP=1`,
  read at ROM open. **But app override matters**:
  `app/src/main/java/paulscode/android/mupen64plusae/persistent/ParallelRdpPrefs.java:62`
  defaults the profile property to `"False"`; `.../jni/NativeConfigFiles.java:335`
  writes it to Video-Parallel. Plugin default-on is NOT the effective app default.
- `app/src/main/assets/mupen64plus_data/profiles/emulation.cfg:106,115` selects
  `rsp-parallel` for relevant profiles; actual selected profile must be recorded.
  This does not show that all configurations use the same RSP or accuracy.
- `mupen64plus-video-parallel/upstream/parallel_imp.cpp:52-145` also uses host scanout,
  fence wait, map, copy and screen swap. “Async” does not mean zero waits/copies.

A setting difference is a competing explanation for perceived speed, not a safe
patch to transplant. No disassembly or comparative native timing was collected.

## Historical claims reconciled

The old implementation plan's performance section (search `2–3×`, `SyncFull`,
`Native NEON`) is superseded, not silently adopted:

| Historical claim | Current evidence and disposition |
|---|---|
| Async RDP makes CPU 2–3× faster / massive improvement | No controlled trace supports a magnitude. Conditional reference wait is real; safety and Phobos benefit unknown. Withdraw bypass recommendation. |
| Mario Tennis frequently SyncFull-stalls | Wait site exists; no counts or wait histogram in available evidence. Frequency/dominance unmeasured. |
| Native NEON must beat SSE-to-NEON | ARM64 includes sse2neon (`ares/n64/n64.hpp:17-24`), SIMD gate in `accuracy.hpp:22-29`. Generated release assembly and workload timing required. |
| Reference RSP is proven native-NEON and Phobos translation is slower | Source-level intrinsic style does not establish generated instructions or workload share. Disassemble exact release objects and sample RSP time before any rewrite. |
| JIT cadence should be raised for full speed | `accuracy.hpp:9-16` is 2048*2 and records historic Conker regressions at larger cadence. Preserve default; old FPS comments are reports, not new runs. |
| Broad perf pass still needs basic compiler tuning | `CMakeLists.txt:4-5` already enables O3, ThinLTO, vectorization and unrolling. `android/app/build.gradle.kts:36-53` sets legacy armv8-a+simd / modern armv8.2-a+fp16+dotprod. Inspect actual compile commands before proposing flags. |
| Audio callback always locks/copies stream registry | Versioned cache already exists (`PhobosRunner.cpp:155-173`); don't propose an already-landed optimization. |
| Old overall compatibility/signing status is current | Historical reports remain useful regression targets, not fresh validation. Signing/update safety is outside this audit; its separate task is cancelled and compatibility remains unverified. |

## Ranked investigation backlog

Ranks prioritize low-risk discrimination, NOT measured speed gain. Every impact
estimate is a hypothesis. Effort L/M/H refers to an eventual bounded experiment.
No row authorizes changing release defaults.

| Rank / area | Source observation | Potential impact / effort / change risk | Smallest discriminating check |
|---|---|---|---|
| 1 Presentation and copies | `ares/n64/vi/vi.cpp:214-276` maps/copies into Screen; `PhobosRunner.cpp:1159-1260` may map again, convert with NEON and hold Vulkan lock through window operations | Medium/high at upscale; M; measurement low, ownership change high | Separate map wait, copy bytes/time, window lock/post, CPU vs Vulkan fallback per frame; determine whether duplicate copy contributes materially |
| 2 SyncFull / queue waits | `ares/n64/vulkan/vulkan.cpp:265-272` waits before DP completion; `parallel-rdp/parallel-rdp/command_ring.cpp:63-100` and `rdp_device.cpp:1136-1178` drain/wait | Potential high if dominant; M; measurement low, bypass very high | Count SyncFull and histogram wait wall time; correlate worker scheduling and GPU timestamps, not CPU run duration alone |
| 3 Pacing / observability | `PhobosRunner.cpp:693-751` smoothed run duration, deadline cap, optional counters; `PhobosJNI.cpp:470-477` exposes aggregate stats only | High value for diagnosis; M; low if external tracing | Paced and separately uncapped run; distinguish sleeping/runnable/CPU-running. Current overlay cannot give GPU time or presented-frame percentiles |
| 4 Audio allocations and diagnostics | `PhobosRunner.cpp:175-237` creates chunk vector each drain and reads ring size for diagnostics outside mutex; `:155-173` cache already present | Low/medium; L/M; medium for concurrency | Allocation counts, lock time, xruns and valid synchronized occupancy. Existing ring log alone cannot prove underrun; evaluate buffer reuse only if measurable |
| 5 Error logging | `vulkan.cpp:30-53` repetitive-error rate limiter disabled; debug scanout probes in `vi.cpp:219-237,329-371` | Low normally, potentially high on failing driver; L; low/medium | Count errors without enabling expensive debug. First retain first-error/count evidence; pipeline failures invalidate accuracy comparison, not a performance success |
| 6 CPU/JIT/dcache | `cpu/recompiler.cpp:180-223` chaining disabled, deferred clocks and selective invalidation present; `cpu/dcache.cpp:6-19,50-106` emulated costs and debug-only counters | Potential high only if CPU-bound; M/H; high for timing/cache edits | Sampling plus block exit/invalidation and cache counts in separately overhead-calibrated build; keep guest cycle policy fixed |
| 7 RSP SIMD/codegen | `rsp/interpreter-vpu.cpp:55-83,127-157`, `rsp/recompiler.cpp:89-116`, SIMD gate above | Unknown; M/H; medium/high | Disassemble exact ARM64 release objects and sample RSP share first; only then test a specific instruction sequence against independent semantic fixtures |
| 8 VI wide mode | `vi.cpp:239-267,283-405` box filtering / CPU fallback RDRAM reads | Scene-dependent medium/high; M; high for compatibility | Count mode transitions, fallback frames and per-mode time; retain Mario Tennis intro/gameplay and Rogue Squadron regression scenes |
| 9 Generic audio / input / JNI | `ares/ares/node/audio/stream.cpp:112-140` filters/resampling; Runner input caches `:304-326`; JNI calls `PhobosJNI.cpp:357-377,465-477` | Unknown usually lower than rendering; M; medium | CPU samples, calls per frame, allocation/lock counts across N64 and audio-heavy systems before batching/removing work |
| 10 Other systems | PS1 CPU `ares/ps1/cpu/cpu.cpp:33-73` interpreter batch/sync; GPU `gpu/gpu.cpp:12-24` renderer/blitter; MD `ares/md/md.cpp:6-12` software VDP integration; SFC `cpu/timing.cpp:48-60`, `ppu/main.cpp:102-213`; GBA `cpu/cpu.cpp:70-106`, `cpu/memory.cpp:87-161` | Unknown; profile L/M, replacement H; high for renderer/JIT redesign | Representative per-core CPU samples and frame-time distribution. No reason yet to replace CPUs/renderers; protect threaded video and multi-stream audio regressions |

Other repository areas (MIA load/decompression, UI scanning, save I/O, pipeline
cache creation) are startup/transition paths rather than established steady-state
Mario Tennis bottlenecks. Keep cold-load latency separate; include them when a
trace actually implicates them. This is broad source triage, not exhaustive
instruction-level proof that no other hotspot exists.

## SyncFull correctness gate — not eligible for bypass

`ares/n64/rdp/render.cpp:616-624` raises MI DP interrupt and clears busy state.
Phobos waits for timeline first. The renderer receives live RDRAM and exposes
hidden RDRAM (`vulkan.cpp:125-143,472`); CPU fallback reads RDRAM directly.
Changing interrupt timing without a GPU completion/visibility ownership model
can expose stale framebuffer data. Audit CPU reads/writes, DP DMA, hidden data,
readback barriers, command boundaries and interrupt acknowledgement together.

Reset retains VkDevice; VI clears old scanout fence (`vi.cpp:422-430`).
Save/load pauses and holds runMutex (`PhobosRunner.cpp:2908-2947`), which alone
would not drain newly asynchronous GPU work. Unload has bounded waits and
abandon handling (`vulkan.cpp:156-211`, Runner `:1823-1907`).
Map/unmap/endScanout lifetimes must survive failures; a timeout is not proof of
completion. Require tests for framebuffer effects, reset, state save/restore,
quit/reload, GPU timeout and DP interrupts before considering an async design.
No blind removal of synchronization, watchdog additions or fake completion.

**2026-09-24 update:** the user explicitly asked for an opt-in **Asynchronous RDP**
setting (Mupen's `SynchronousRDP=False`), default off. The default stays synchronous.
See [the 2026-09-24 section](#2026-09-24-scan-and-behavior-preserving-changes)
for how the opt-in path drains the GPU before state save/load and reset.

## Next bounded experiment and blockers

Use [the protocol](mario-tennis-benchmark.md) to identify the actual Phobos APK
and the reference APK built from the user-supplied `v336` source, then collect
external low-overhead CPU/scheduler/frame traces first.
No custom instrumentation is justified yet without a device to validate its
overhead. If external traces cannot attribute the waits, the next *separate*
reviewed change is an opt-in bounded stage recorder at the listed sites, with
no timing changes and with on/off overhead measurement.

Missing inputs: attested reference and Phobos APK identities; target device/OS,
driver binary/version; legal game hash/region, reproducible scene and settings.
The user-supplied reference source/tag is now identified, but an APK built from
that tag is not. Historical handoff mentions Retroid Pocket 6 / Adreno 740, but
this run has not confirmed that device or its driver. These block native
comparison, not this audit.

Documentation checks: `git diff --check`, internal Markdown link existence and
independent source-citation/snapshot review are the appropriate acceptance checks.
Actual final command results and review hashes are recorded in the commit record.

## 2026-09-24 scan and behavior-preserving changes

The user asked for every remaining host-side performance gain in every core, with
accuracy first. Written source-only while the user's phone was in use by another
session, then compiled for both flavors with host unit tests passing (see the
[handoff](handoff.md#checks-run-2026-09-24-local-mac-no-device)); nothing has run on a
device or been measured, so **no speedup is claimed**. Five read-only passes informed
it: N64 versus the Mupen64Plus-AE parallel
plugin at the pinned `v336` source, build/infrastructure/JNI bridge, per-core hot
loops, and the two touch-control references. Classes: **(a)** behavior-preserving
host change, **(b)** accuracy or configuration difference (report or opt-in only),
**(c)** needs measurement.

### Why Mupen64Plus-AE's parallel-RDP runs faster (ranked hypotheses)

1. **(b) SyncFull.** Both builds wait at the same site, but the Mupen64Plus-AE app
   writes `SynchronousRDP=False` (`ParallelRdpPrefs.java:62` →
   `NativeConfigFiles.java:335`), so its CPU never waits for the GPU at a full sync.
   Phobos always waited. Now an opt-in setting (below).
2. **(a) Presentation.** Each N64 Vulkan frame took three CPU passes in Phobos: the VI
   copied the mapped scanout into the Screen under `vulkan.mutex`, the Screen ran a
   full-frame palette pass, and the frontend mapped and copied the scanout again,
   discarding the first two. Mupen does one copy. Fixed (below).
3. **(c) Incoherent RDRAM.** If `VK_EXT_external_memory_host` import fails,
   parallel-RDP keeps a separate GPU RDRAM copy and syncs it on every flush and
   scanout. Both builds allocate 64 KiB-aligned RDRAM, so import should succeed on
   Adreno; confirm on device by searching logcat tag `Granite` for
   `VK_EXT_external_memory_host not supported or failed`. No new log is needed.
4. **(b)/(c) Configuration.** Phobos always sets
   `COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT` (CPU-visible coverage bits),
   uses 4 frame contexts (Mupen 3), keeps bindless on (Mupen disables it) and never
   calls `set_quirks`.
5. **(c) CPU/RSP architecture.** ares runs a cycle-accurate R4300 JIT (no block
   chaining, emulated caches, `JitInterleaving` 2048×2) and its own RSP recompiler;
   Mupen64Plus-AE profiles use a dynarec and `rsp-parallel`. Likely the largest
   remaining gap and outside parallel-RDP glue; not a candidate for host-side change.
6. **(a) Overheads.** The Granite repetitive-error rate limiter was disabled
   (`if (false && …)`), and several diagnostics ran in hot paths (below).

### Implemented

| Change | Files | Class | Notes |
|---|---|---|---|
| N64 Vulkan frames presented once | `ares/n64/vi/vi.cpp`, `ares/ares/node/video/screen.{hpp,cpp}`, `ares/n64/vulkan/vulkan.{hpp,cpp}`, `PhobosRunner.cpp` | (a) | `vulkan.frontendPresentsScanout` (set by the Android frontend at N64 load): `VI::refresh` sets only the viewport and calls `endScanout`; `Screen::setPassthrough(true)` makes `Screen::refresh` call `platform->video` without the palette pass; the frontend maps and copies the scanout once. The CPU fallback path and non-Android behavior are unchanged. |
| N64 screenshots from the scanout | `vulkan.{hpp,cpp}` `readScanout`, `PhobosRunner.cpp` `takeScreenshot` | correctness | Needed once frames stopped passing through `lastFrameBuffer`; see touch-controls F17. |
| Non-N64 video conversion | `PhobosRunner.cpp` `convertToWindowPixels`, `doubleLineWidth` | (a) | NEON R/B swap (`vqtbl1q_u8`), palette-range check once per frame (`vcltq`/`vmaxvq`), NEON 2× line doubling (`vzipq_u32`); `lastFrameBuffer` is written only on this software path. |
| Input callback | `PhobosRunner.cpp` `input()`, `setInput()` | (a) | Per-node binding cache (bits, ZX-key flag); system name resolved only on a cache miss; keyboard set consulted only while a key is held; removed the axis heartbeat and `setInput` logs. Quick-tap press counters added (touch-controls F19). |
| Granite error rate limiter re-enabled | `vulkan.cpp` `LoggingInterface` | (a) | First message per 5 s window still logs. |
| Hot-path diagnostics removed | `ares/ng/cartridge/board/sma.cpp`, `ares/ps1/peripheral/dualshock/dualshock.cpp`, `ares/ps1/disc/cdxa.cpp`, `PhobosRunner.cpp` | (a) | Neo Geo SMA presence/PRN/write logs on every access; PS1 DualShock "TEMP DIAG" every 30 reads; PS1 CD-XA 1 Hz sector/sample logs; frontend video `LOGD` every 2,000 frames. One-time SMA load logs and the rare bank-switch log remain. |
| PS1 BIOS TTY tracer off on Android | `ares/ps1/cpu/debugger.cpp` | (a) | `setTerminal(true)` made the message tracer permanently enabled, so every taken branch ran the BIOS putchar/puts hook and printed to stdout, which Android discards. Desktop builds unchanged. |
| Neo Geo LSPC model check hoisted | `ares/ng/lspc/render.cpp` | (a) | `Model::NeoGeoCD()` read once per line instead of twice per visible sprite. |
| ThinLTO link optimization | `CMakeLists.txt` | (a) | Objects were already ThinLTO bitcode; `target_link_options(phobos_android PRIVATE -flto=thin -O3)` runs the LTO backend at O3 instead of lld's default O2. Verify the link line with `ninja -v`. |
| C sources get the flavor `-march` | `android/app/build.gradle.kts` | (a) | `cFlags` now mirror `cppFlags` (libco, sljit, volk, libchdr/zstd/lzma/miniz). |
| Opt-in Asynchronous RDP (default off) | `vulkan.{hpp,cpp}`, `PhobosRunner.cpp`, `PhobosJNI.cpp`, `PhobosCore.kt`, `SettingsStore.kt`, `MainViewModel.kt`, `N64ExperimentalSettingsScreen.kt`, `EmulationMenu.kt` | (b), user-authorized | `vulkan.asynchronousRdp` skips the SyncFull timeline wait. It applies immediately (next full sync), so no restart or notice is needed. Before state save, state load and reset the frontend calls `Vulkan::drainRdp()` (bounded 2 s timeline wait; never on an abandon path) while the setting is on, or while `rdpWorkPending` shows an async full sync that nothing has waited on since (the setting was just switched off), so in-flight GPU writes cannot land in a snapshot or restored RDRAM. Games that read rendered frames back with the CPU (photos, motion blur, pause backgrounds) may glitch. |

Correctness changes in the same area, from the Rogue Squadron review (details in the
[handoff](handoff.md#touch-controls-overhaul-and-performance-scan--2026-09-24-in-progress)):
`scanout_memory_range` and the ares CPU fallback use the VI origin and width again
(upstream), not the RDP's latest color image, and the wide-mode transition hold is armed
only on real transitions and released as soon as the new buffer is rendered.

### Considered and not changed

| Item | Class | Reason |
|---|---|---|
| SyncFull default | (b) | Stays synchronous; async is opt-in only. |
| `PROFILE_PERFORMANCE` | (b) | Matches upstream ares. Already skips per-step SFC coprocessor sync (`sfc/cpu/timing.cpp:51-53`) and decimates PCE PSG audio 64× (`pce/psg/psg.cpp:38-44`). Do not extend. |
| SFC performance PPU | (b) | Phobos already forces `accurate=false` (scanline renderer). Reported only; MD and PCE keep their accurate VDPs. |
| Audio drain buffer reuse | — | Task #4 is cancelled and not authorized. |
| JIT icache profile counters | (a) | The recompiler already counts only in homebrew mode; the per-instruction increments are in the interpreter, and homebrew reads them through the emux interface. Negligible gain, guest-visible. |
| `unlikely` on PS1/SFC/MD/GBA/GB/PCE/FC debugger hooks; GBA DAC window copies | (a)/(c) | Speculative code-layout gains in upstream core files; measure first. |
| `-mcpu`/`-mtune`, PGO | (a)/(c) | Need device A/B runs. (`-fvisibility=hidden` was done on 2026-09-25, scoped to `phobos_android` so libadrenotools keeps its exports.) |
| Screen mutex across `platform->video`; `Vulkan::render` lock scope | (a)/(c) | Buffer-ownership changes; measure lock contention first. |
| 1 Hz FPS and `AudioDiag` logs, N64 debug-logging dumps | — | Negligible at 1 Hz and useful field diagnostics; N64 dumps are behind the debug toggle. |

### Required verification

1. Done 2026-09-24 (NDK 26.1, local): both flavors build, `:app:testModernDebugUnitTest`
   passes, and the link command contains `-flto=thin -O3`. A CI build with NDK 28.2 is
   still needed.
2. Device regressions: N64 (Mario Tennis intro and gameplay, Conker, Zelda OoT, Rogue
   Squadron boot and menu, a game that boots into a wide VI mode) for presentation,
   screenshots, pause/resume, reset, state save/load, rotation; Neo Geo SMA title
   (Garou, KOF 99); PS1 FMV with XA audio; a non-N64 system in each video path
   (palette, 2× line doubling).
3. Asynchronous RDP on and off, including state save/load and reset while on.
4. Any speed claim needs the [benchmark protocol](mario-tennis-benchmark.md) and
   external traces, before and after.

## 2026-09-25 device profiling (Retroid Pocket 6) and changes

The user reported Mario Tennis dropping into the 30s–40s in Phobos while Mupen64Plus-AE
with parallel-RDP held 60, and that Asynchronous RDP did not close the gap. Everything
below was measured on the RP6 (Snapdragon 8 Gen 2: Cortex-X3 prime core at 3.19 GHz,
2×A715, 2×A710, 3×A510), with release builds from NDK 28.2 (the CI toolchain; an
NDK 26.1 local build ran 10–15% slower and was not used for comparisons).

**Method.** CPU profiles came from `simpleperf` against local builds carrying
`<profileable android:shell="true"/>` (never committed); per-thread CPU from
`/proc/<pid>/task/*/stat`; FPS from the 1 Hz `Emulation Stats` log. The Mario Tennis
attract loop is not a reliable benchmark: after 25–45 s the demo plays out differently
between runs, so dips land at different times. The benchmark is instead the user's
slot-0 save state (a Mario vs Boo match): launch, load the state with Z+L1, sample 30 s,
two runs per build, Asynchronous RDP on.

**Mupen reference.** The user's Mupen64Plus-AE build with the `Parallel` profile
(new_dynarec, `rsp-parallel`, parallel-RDP at 1×, `SynchronousRDP=False`) and the same
Turnip v26.3.0-R5 driver as Phobos: 59–62 FPS presented (SurfaceFlinger timestats),
emulation thread about 22% of a core, whole emulation process about 30%. Its native code
is built with `APP_OPTIM := release` even in the debug APK. Mupen presents synchronously
(it waits for the scanout fence on its emulation thread), so presentation is not what
makes it faster.

**Phobos baseline (CI build of `373cec0`).** In the match: 50.6 FPS average, worst
second 27.1, frame-time p90 26.2 ms; emulation thread 84–86% of the X3 (it is placed
there 95% of the time, at about 3.08 GHz, with negligible run-queue wait).

### What made Phobos slow, in order of cost

1. **A full synchronize after every JIT block.** `CPU::instruction()` capped the JIT
   budget by `compare - count` and clamped a negative value to 0. Count and Compare are
   33-bit counters; once Count passes Compare the next timer interrupt comes only after
   Count wraps (about 92 seconds at the default countPerOp), but the clamp made the
   budget 0 for that whole time. Mario Tennis runs in that state permanently, so every
   block ended in `CPU::synchronize()` — about 3 million times a second in the match
   (cycle-derived: ~93.75 MHz / ~30 cycles per short block under the clamp). That also
   ran the RSP in tiny slices. `JitInterleaving` (2048×2) was never reached; this is
   also why the 2026-08-12 A/B saw no difference between 2048×2 and 4096×2 in
   Mario Tennis.
2. **A futex syscall per audio sample and per RDP command.** Bionic's
   `pthread_cond_signal` always makes a `futex` call. `AndroidPlatform::audio()` ran
   about 32,000 times a second, each locking `audioMutex` twice and notifying; the
   parallel-RDP `CommandRing` notified once per command (up to 160,000 a second in a
   match) and its worker relocked and notified per command too. Together about 15% of
   the emulation thread, plus about 29,000 wake-ups a second of the audio thread.
3. **Per-dispatch block lookup.** Blocks always return to the dispatcher. After the
   clamp fix, simpleperf on the same Mario vs Boo scene still showed about 9 million
   dispatches a second (blocks end long before the interleaving budget; synchronizes
   are only ~46 k/s). At the clamped baseline the synchronize rate (~3 M/s) was the
   costly one; `computeStateKey()` rebuilt about 25 fields every lookup (about 10% of
   the emulation thread) and the section/list lookup missed cache on the block table
   and block structs.
4. **PLT/GOT indirection.** With default visibility, every internal call went through
   the PLT and every global (`cpu`, `rsp`, `vi`, `rdram`, ...) through the GOT.
5. **Frame-boundary waits.** The emulation thread waits at scanout for the ring
   worker to drain, and `Screen::frame()` spins on `nall::spinloop()`, which is
   `usleep(1)` (50–100 µs) on ARM, until the screen thread takes the frame. Present in
   Mupen in equivalent form; small in heavy scenes, where the thread is CPU-bound.

Asynchronous RDP only removes the SyncFull wait, which was not the bottleneck: the
emulation thread stayed 86% busy with it on.

### Implemented

| Change | Files | Class | Notes |
|---|---|---|---|
| JIT budget uses the distance to Compare modulo 2³³ | `ares/n64/cpu/cpu.cpp` | timing | Syncs fell from about 3,000,000 to about 46,000 a second. Phases where Count had passed Compare now get the designed `JitInterleaving` budget (never more), instead of a sync per block. The budget is still capped to the distance through the wrap, so a Compare past the wrap is not overshot by more than one block (the same `JitInterleaving` bound as any other timer). Two related bounds stay as before, just more visible with fewer syncs: (1) an MTC0 to Count/Compare ends the block but does not re-cap a running `jitClockTarget`, so a handler that writes `Compare = Count + small` while Count is past Compare can see the interrupt up to about one interleaving late; (2) the interrupt check can miss a step that crosses both the 2³³ wrap and Compare, and Compare = 0 never fires — both pre-existing. Opt-in overclock and countPerOp also change effective speed slightly: `peripheralClocks >>= f` and `clocks*countPerOp/4` truncate once per sync, so ~65× fewer syncs retain more of those fractional clocks than the old sync-every-block path (defaults are unaffected because every step cost is even). Conker's pub menu ran 4+ minutes at 60 FPS without a stall; Zelda OoT, Paper Mario, Rogue Squadron, Mischief Makers, F-Zero X and Wave Race 64 boot and run. |
| Audio handed over in blocks | `PhobosRunner.cpp` | (a) | Samples collect in one per-thread `AudioThreadState` (one emulated-TLS lookup per call at `minSdk` 26) and move to the ring every 256 stereo frames and at the end of each emulated frame; ring copies are two `memcpy` segments; the audio thread is only notified when the ring was empty; `audioStreamOpen` replaces the per-sample `audioMutex` check. Output samples are identical. |
| RDP ring wakes only a sleeping worker | `parallel-rdp/command_ring.{hpp,cpp}`, `rdp_device.{hpp,cpp}`, `vulkan.cpp` | (a) | `Vulkan::render()` brackets each command range with `begin/end_command_batch()`. The producer notifies only when the worker is waiting, and within a batch at most once per 128 words so the worker keeps working alongside it; `drain()`, a full ring and `wait_for_timeline()` kick first. The worker takes every queued command under one lock and notifies only a waiting producer. Same commands, same order. An earlier variant that notified once per batch starved the worker on long display lists and was replaced. |
| Hidden symbol visibility | `CMakeLists.txt` | (a) | `-fvisibility=hidden -fvisibility-inlines-hidden` on `phobos_android` only (all 67 JNI entry points are `JNIEXPORT`; libadrenotools and its hook libraries keep default visibility). |
| Cached state-key mode bits | `ares/n64/cpu/{recompiler.cpp,cpu.hpp,context.cpp,exceptions.cpp,interpreter-fpu.cpp}`, `ares/n64/rdram/rdram.cpp` | (a) | Status/FCSR/RDRAM-map bits are recomputed only after `invalidateStateKey()`, called from `Context::setMode()` (power, exception entry, Status and Config writes, ERET), `Exception::nmi()`, `setControlRegisterFPU()`, `Recompiler::reset()` (power, flush, state load) and the two RDRAM map writers. The JIT never writes these fields directly (MTC0/CTC1/ERET call C++). GP/SP/watchpoint bits are still evaluated per lookup. A development build cross-checked the cached key against a full recomputation every 1024th lookup: no mismatch in millions of checks. |
| Inline fast block lookup | `ares/n64/cpu/{cpu.hpp,cpu.cpp,recompiler.cpp}`, `nall/nall/gdb/server.{hpp,cpp}` | (a) | 4096-entry direct-mapped table (16-byte entries) for aligned KSEG0 RDRAM PCs, checked inline in `CPU::instruction()`; a hit requires a clean section whose generation matches the block's (bumped whenever `section()` clears it; `reset()` clears the table). The KSEG0 `devirtualize()` result is computed inline, and `GDB::Server::hasWatchpoints()` is inline. |
| N64/PS1 picture aspect | `PhobosRunner.cpp` `recordVideoGeometry` | correctness | See touch-controls F13: N64 progressive scanouts (640×240) were shown at 8:3. |

### Results (Mario vs Boo save state, 30 s, Asynchronous RDP on)

Measured on the Retroid Pocket 6 with local Modern release builds of this working tree
(NDK 28.2.13676358, both flavors compiled; the table is Modern). Asynchronous RDP was
turned on for the runs (it defaults to off). An earlier RDP-ring variant that notified
once per batch is not in these numbers; it was replaced before measurement by the
128-word kick path described above.

| Build | Average FPS | Worst second | Frame p90 | Emulation thread |
|---|---|---|---|---|
| CI `373cec0` | 50.6 | 27.1 | 26.2 ms | 84% |
| This change set (two runs) | 58.4 / 58.4 | 49.7 / 45.9 | 17.4 / 18.4 ms | 76% |

The remaining dips are the first seconds after a state load (the JIT recompiling) and
short CPU-bound stretches of the match. Mupen still does the same work with under a
third of the emulation-thread CPU: its dynarec links blocks and does not emulate the
caches, its timing is approximate, and its RSP runs whole tasks at once.

### 2026-09-25 follow-up: same-section unconditional `J` linking

| Change | Files | Class | Notes |
|---|---|---|---|
| Same-section `J` block linking | `ares/n64/cpu/{cpu.hpp,cpu.cpp,recompiler.cpp}`, `PhobosRunner.cpp` (stats) | (a) | Revive ares-style lazy links for opcode `J` only (not `JR`/`JAL`), same 4 KiB section, KSEG0, safe delay slot. Each hop re-checks dirtiness, `jitClockTarget`, interrupt/NMI/`sysadFrozen`. Unresolved `linkedBlock` is rejected in JIT before the C++ trampoline (GP/SP state-key churn leaves many candidates unresolved). Mario vs Boo save-state (Async RDP on — this row first said off, but the device setting was enabled; see the correction below): mean FPS 59.8; emulation-thread CPU ~109% of one core vs ~123% on the prior master build; ~18k successful links/s. |

### 2026-09-25 follow-up: accuracy-neutral backlog + Experimental speed hacks

On branch `feature/n64-accuracy-neutral-perf-2026-09` ([PR #4](https://github.com/pwnedbygary/phobos/pull/4)).
Defaults unchanged except where noted; speed hacks are opt-in under N64 Experimental.

**Correction (Async RDP):** every RP6 run on 2026-09-25 had Asynchronous RDP **on** —
the persisted device setting; load logs print `N64 asynchronous RDP enabled` for each
run. Earlier text here, the `J`-linking row above, and commit `c02166932` said "off". The
before/after comparisons still hold: every run used the same setting.

| Change | Files | Class | Notes |
|---|---|---|---|
| Cross-section + dual-edge linking | `ares/n64/cpu/{cpu.hpp,cpu.cpp,recompiler.cpp}` | (a) | Terminal `J` may link across 4 KiB sections (target generation/dirty checks). A block ending in a not-taken conditional branch links its external fallthrough through a per-exit `LinkSlot` (`jitLinkedCodeFromSlot`); a taken branch sets `EndBlock` and leaves before that dispatch. Edge links need the terminal-`J` delay-slot rule (no branch, state-key change, Count/Compare write or helper call; no in-block SP/GP key change). Both link helpers also require the runtime PC to equal the link target and the live `computeStateKey()` to equal the target block's key — the same identity the dispatcher uses. |
| RSP pipeline hash skip | `ares/n64/rsp/{rsp.hpp,recompiler.cpp}` | (a) | `Block::execute()` skips the full pipeline assign when `pipeline.hash()` matches the specialization key; otherwise still copies and zeros `clocks`. **Reverted** later the same day: the hash cost far more than the copy (see [RSP pipeline copy restored](#2026-09-25-follow-up-rsp-pipeline-copy-restored-regression-fix)). |
| `Screen::frame` CV handoff | `ares/ares/node/video/screen.cpp` | (a) | Producer waits on `_frameCondition` (100 ms deadline) instead of `spinloop()`; consumer clears `_frame` then unlocks before `refresh()` and notifies. |
| Faster CPU sync | `cpu.cpp`, Settings / Experimental UI, JNI | (b) opt-in | `fasterSync`: `JitInterleaving × 4`. Default off. Stalled Conker's pub until the Count read fix below. |
| Skip cache timing | `dcache.cpp`, `cpu.hpp` icache fill, Settings / UI, JNI | (b) opt-in | `skipCaches`: no icache/dcache fill or writeback stall cycles (Mupen models none). Cache contents stay emulated: a C++-only dcache bypass would be incoherent with the JIT's inline dcache hit paths and CACHE ops (the Task 58/59 hazard), so none is done. Default off. |
| RSP task mode | `rsp.cpp`, Settings / UI, JNI | (b) opt-in | `taskMode`: an unhalted RSP runs ahead of the CPU up to ~1 frame (`187'500'000 / 60` clock units; 2M-instruction guard), then the regular loop handles remaining halted time so DMA still progresses. Default off. |

**Mischief Makers:** previously parked (fatal trap after Start from bad RSP audio results).
On RP6 after the Count/Compare budget wrap, title → save select → in-game dialogue ran at
~60 FPS. Likely a side effect of contiguous RSP slices; VU accuracy not claimed fixed.

**Touch (same branch):** seamless D-pad diagonal highlight (unioned L-fill); N64 L = large
shoulder, Z = small pills on both sides in landscape. The right Z starts hidden in
portrait (four shoulder buttons don't fit one 360 dp row; the layout editor can show it).
Host unit tests: 41 pass (`:app:testModernDebugUnitTest`, including layout overlap).

**Measure (Mario vs Boo save state, 30 s, Async RDP on, RP6 `49016109`):** final
snapshot, two runs: mean FPS 55.6 and 58.3 (emulation thread 129% and 112% of one core;
steady stretches ~59–60). An earlier snapshot of this branch measured 58.2 / 110%. The
same build varies this much run to run on the RP6, so no FPS change is claimed for the
review fixes. `linkTaken` ~30k/s. Smoke (18–25 s each, no crashes or N64 STALL): Mario
Tennis, Mischief Makers, F-Zero X, Paper Mario, Ocarina of Time, Conker (intro at ~60).

**Speed hacks (commit `c02166932`, Async RDP on, one hack at a time, same save state):**

| Config | Mean FPS per run | Emulation thread | Notes |
|---|---|---|---|
| All off | 53.7, 55.1 | 126–127% | Same build earlier: 55.6, 58.3. |
| Faster CPU sync | 53.9, 55.3 | 127% | No gain. Conker's pub menu: after ~75 s of normal play, repeated 10–20 s freezes with brief recoveries (screen-region hashes; the core kept presenting ~60 FPS and logged no N64 STALL). Switching it off live restored smooth animation (0 static samples in 42 s). Reproduces the `accuracy.hpp` note that interleaves above `2048*2` freeze Conker's pub. |
| Skip cache timing | 57.6 (one comparable run) | 117–130% | The changed timing makes the benchmark play out differently: two runs ended at the pre-match screen and one likely missed the load hotkey, so those runs aren't scene-comparable. |
| RSP task mode | 55.2, 56.7 | 125% | Within run-to-run noise. |
| All three | 57.4 (one run) | 128% | Within noise. |

None of the three gives a measurable gain in this scene, so the remaining gap to Mupen is
not sync overhead or cache stall cycles. The emulation thread stays near 125%.

**Why Faster CPU sync stalls Conker (root cause, confirmed on device):** ares advances the
CP0 Count register only inside `CPU::synchronize()`. Between syncs, MFC0 Count returns the
value from the last sync, and a timer interrupt fires only when one sync step moves Count
across Compare (`cpu.cpp`, `interpreter-scc.cpp`). libultra arms a timer as *read Count, add
a delay, write Compare*. Conker uses a very short timer, 1171 Count ticks (~25 µs). If a
sync lands between the read and the write, Count jumps past the new Compare. The write
then sits behind Count, and the interrupt cannot fire until Count wraps back to it:
2³² ticks at 46.875 MHz = 91.6 s. The waiting thread blocks, the RSP, RDP and audio go
idle (AI queue empty), and only the VI interrupt continues.

A temporary debug-gated log of Compare writes caught this at both stalls in one run:
Compare `ba1694dd` was written 809 ticks behind Count (read `ba16904a` + 1171). The game
resumed 91.54 s later, when Count reached `ba1694dd` again, then stalled 1 s later the same
way (91.53 s). No wrap-straddle miss was logged.

The default interleave (2048×2 clocks ≈ 1024 Count ticks per step) keeps each jump just
under Conker's 1171-tick timer. That matches the historical table: 1024×2 and 2048×2 are
stall-free, while 4096×2 and above stall. Faster CPU sync (4096 ticks per step) usually
exceeds it. The default is therefore safe only by about a 15% margin; upstream ares has the
same Count model.

A fix is possible without changing timing defaults: make Count reads include the clocks
run since the last sync (and account for them on Count writes), and use a wrap-safe
crossing check. That keeps `Compare = Count + delay` in the future at any interleave.

**Fix (same day):** `CPU::countSinceSync()` adds the clocks run since the last sync to MFC0
Count. MTC0 Count records those clocks (`countWriteSkip`) so the next sync doesn't add
them again; the field is runtime-only (not serialized) and is reset on power and state
load. MTC0 Compare forces a resync so the JIT budget is re-capped against the new
Compare. `synchronize()` fires the timer on a modular 33-bit distance, which also covers
a crossing that straddles the wrap and Compare = 0. With the fix and Faster CPU sync on,
Conker ran 280 s through boot, intro and the pub with no RSP gap over 3 s; both earlier
runs had stalled twice for 91.5 s in the same window. Default-settings regression with the
fix (RP6, Async RDP on, new performance HUD on): Mario vs Boo 58.5 and 58.8 FPS mean
(emulation thread ~110%; slowest-10% seconds ~57 FPS versus 47–50 in earlier runs of the
day). Mischief Makers, F-Zero X, Paper Mario and Ocarina of Time boot and run at ~60 with
no crashes, and Conker reaches the pub at ~60.

The same capture exposed a latent crash in the N64 Debug Logging stall dump
(`PhobosRunner.cpp`): its format string had an `EPC=0x%08llx` field with no argument, so
later `%s` fields read shifted varargs. Earlier dumps printed a heap pointer as "EPC", and
this build segfaulted in `vsnprintf`. The EPC argument is now passed.

**Review:** Bugbot on the uncommitted diff, four passes. Fixed: (1) `LinkSlot` recorded
the block-entry state key instead of `emitStateKey`; (2) conditional edge links lacked
the terminal-`J` delay-slot rule — both link helpers now also verify runtime PC and the
live state key; (3) "skip cache timing" missed the JIT's inline icache-miss stall. The
first draft of skip-caches bypassed the dcache in C++ only (incoherent with the JIT's
inline dcache paths), replaced by timing-only. Final pass: no bugs.

### 2026-09-25 follow-up: emulation thread on the fastest core

Branch `feature/emu-sched-2026-09`, stacked on the performance HUD
([PR #5](https://github.com/pwnedbygary/phobos/pull/5)). Host scheduling only; emulated
timing is unchanged.

**Finding.** The new HUD showed the emulation thread's core and clock, and the RP6 often ran
it on the Cortex-A715/A710 cores (2.8 GHz) rather than the Cortex-X3 (3.19 GHz). Sampling the
thread's CPU every 50 ms during Conker's attract mode put it on the X3 about a third of the
time; in the Mario vs Boo match it was there for 83–89% of frames. The existing
`sched_setaffinity` to the last four cores at thread start had no lasting effect: Android
resets a thread's affinity when it moves the app between cpusets, and the thread was found
with all eight CPUs allowed. In the match the thread is CPU-bound at the frame budget
(emulation work ~16.5–17 ms per frame while running ~82% of the time; the rest is waits inside
the frame), so a frame spent on a slower core overruns.

**Change.** Settings → Emulation → "Use the fastest CPU core" (default on). The emulation
thread's affinity is set to the highest-capacity CPUs (`cpu_capacity`, else
`cpuinfo_max_freq`; on the RP6 only CPU 7) and re-applied every frame. Under light load
Qualcomm core control pauses the fastest core; `sched_setaffinity` then fails with EINVAL and
the scheduler's placement stands. Genesis and SNES smoke runs hit this and ran at 60 FPS on
the mid cores. Off restores the previous one-shot last-four-cores mask. The 1 Hz
`Emulation Stats` log line now also reports the longest frame interval and the number of
intervals over 20 ms.

**Measured** (Mario vs Boo save state, 30 s, Async RDP on, two runs each):

| Setting | Mean FPS | Worst second | 10th-percentile second | Intervals over 20 ms | Longest interval | Work per frame |
|---|---|---|---|---|---|---|
| On | 59.4, 59.5 | 53.4, 53.9 | 58.6, 58.6 | 176, 156 | 28.6, 28.3 ms | 16.7 ms |
| Off | 58.3, 58.6 | 44.3, 44.7 | 55.9, 56.4 | 254, 235 | 34.3, 31.8 ms | 17.2 ms |

An experiment build compared four modes in the same scene (two runs each): the scheduler's
choice (57.6 and 58.1 FPS), the fastest core re-applied every frame (59.5 and 59.5), an ADPF
performance-hint session reporting each frame's work against the frame period (58.5 and 58.3),
and both (59.2 and 59.3). The hint session alone barely moved the thread (89–92% of frames on
the X3) and added nothing on top of pinning, so it isn't part of the change.

### 2026-09-25 follow-up: RSP pipeline copy restored (regression fix)

Branch `feature/rsp-pipeline-copy-2026-09`, stacked on the fastest-core change
([PR #6](https://github.com/pwnedbygary/phobos/pull/6)). It reverts the RSP pipeline hash skip
from [PR #4](https://github.com/pwnedbygary/phobos/pull/4).

**Finding.** An 8 s simpleperf profile of the Mario vs Boo match (cpu-clock with off-CPU
samples and DWARF call graphs, profileable local build) put 27% of the emulation thread's
time in `RSP::Pipeline::hash()`. PR #4 made `Block::execute()` compute that CRC32, byte by
byte over 28 bytes of hazard state, before every RSP block, to skip copying the ~90-byte
pipeline struct when the hash matched. RSP blocks run millions of times a second, and the
hash costs far more than the copy it saved. The skip path also wasn't equivalent to the copy:
it only cleared `clocks` and left the other fields as they were. This likely explains why
Mario vs Boo measured about 58.5 FPS after PR #4 against 59.8 after `J` linking alone, which
was put down to run-to-run variance at the time.

**Change.** `Block::execute()` copies the block's pipeline unconditionally again, exactly as
upstream, and the `pipelineHash` field is gone.

**Measured** (Mario vs Boo save state, 30 s, Async RDP on, fastest-core pin on, two runs each):

| Build | Mean FPS | Worst second | 10th-percentile second | Intervals over 20 ms | Longest interval | Emulation thread CPU |
|---|---|---|---|---|---|---|
| PR #6 | 59.4, 59.5 | 53.4, 53.9 | 58.6, 58.6 | 176, 156 | 28.6, 28.3 ms | 81% |
| This change | 59.9, 59.9 | 59.6, 59.6 | 59.6, 59.8 | 21, 11 | 24.6, 24.9 ms | 76% |

Smoke: Mischief Makers, F-Zero X, Paper Mario, Ocarina of Time and Conker run at ~60 FPS
with no crashes.

**Other waits in the same profile.** About 11% of the emulation thread's time was spent
blocked on a mutex inside the Turnip driver's `vkQueueSubmit`: three quarters of it from the
VI scanout submit (`RDP::VideoInterface::scanout`), the rest from the RDP renderer's submit.
Submit ioctls took another ~10%. Another thread holds the driver's queue lock at those
moments, likely the presentation thread's `vkQueuePresentKHR` or the RDP worker's submits.

### 2026-09-26 follow-up: frame handoff no longer waits for the display

Branch `feature/n64-rsp-dispatch-2026-09`, stacked on the RSP pipeline copy fix
([PR #7](https://github.com/pwnedbygary/phobos/pull/7)).

**Finding.** With Fast Forward Speed set to Unlimited, fast-forward in Mario Tennis still ran at
59.8 FPS, and every frame's measured time stretched to 16.7 ms, one display refresh. Right after
a state load, the same scene took about 9 ms per frame at normal speed. ares' threaded `Screen`
handed each frame to its video thread under `Screen::_mutex`, and the video thread held that
mutex for its whole `refresh()`, including the platform present: `ANativeWindow_lock` blocks until
the display returns a buffer, which happens at vsync. `Screen::frame()` also waited for the video
thread to claim the previous frame. So the emulation thread could never get more than one frame
ahead of the display. Fast-forward was capped at the refresh rate on every system. At normal
speed, the emulator's own 60 Hz deadline clock and the display's vsync drifted against each
other and periodically held frames that needed 9–12 ms of work until the next vsync.

**Change (ares `Screen`, all systems): mailbox handoff.** `frame()` swaps its finished frame in
under `_mutex` without waiting. A frame the video thread never claimed is dropped, and its buffer
is cleared the way `refresh()` clears a presented one (skipped for passthrough frames, which
never read these buffers). The video thread claims the newest frame and prepares it under
`_mutex`, including the core's refresh callback (N64 VI, PS1 blitter), then releases the lock
and only then calls the platform present. Non-threaded video is unchanged.

Also in this change: the RSP recompiler reads the instruction-tracer flag once per
`RSP::main()` instead of on every block lookup, where the chain of dependent loads stalled the
block-cache load. The RSP I/O tracer hooks only touch their register-name tables when tracing
is on. The pause menu's duplicate "Reset System" button is gone; Reset stays in the quick-action
row.

**Measured** (RP6 in High Performance mode, `performance_mode=2`; Async RDP on; fastest-core pin on):

| Metric | PR #7 | This change |
|---|---|---|
| Fast-forward, Mario vs Boo, uncapped FPS | 59.8 (capped) | 89.8 (80.5–98.7) |
| Fast-forward, attract loop | capped at 60 | 87–134 |
| Mario vs Boo 30 s × 2: mean FPS | 59.9, 59.9 | 60.0, 59.9 |
| Mario vs Boo: intervals over 20 ms | 1, 3 | 6, 23 (the 23 fall in one 7 s heavy stretch) |
| Mario vs Boo: measured time per frame | 15.2, 14.3 ms | 11.1, 11.2 ms |
| Attract loop, 120 s: worst second | 51.3 | 59.0 |
| Attract loop: intervals over 20 ms | 2.17 per second | 0.69 per second |
| Attract loop: measured time per frame | 14.5 ms | 11.3 ms |

The new build's attract run excludes a 16 s window where the user switched fast-forward on,
plus 5 s after it. Presented frames (SurfaceFlinger timestats, Mario vs Boo, 30 s): both builds
showed 1,797–1,798 frames, all 16 ms present-to-present, none dropped. At 3.2 GHz this scene
doesn't make either build miss a refresh; the gains are headroom, worst cases and fast-forward.
Standard performance mode, where the user first saw drops, is still to be measured. Smoke:
Mischief Makers, F-Zero X, Paper Mario, Ocarina of Time, Super Mario All-Stars (SNES),
Sub-Terrania (Genesis) and the PlayStation boot (Ape Escape) run at 60 FPS with no crashes.

### 2026-09-26 follow-up: speed hacks re-measured; GPU driver waits after the handoff fix

Measurements only, no code change. RP6 in High Performance mode, Async RDP on, fastest-core pin
on, build `551e9348f` (the PR #9 CI APK).

**Speed hacks** (Mario vs Boo save state, 30 s × 2, one hack at a time):

| Config | Intervals over 20 ms | Longest interval | Work per frame | Emulation thread CPU |
|---|---|---|---|---|
| All off | 1, 2 | 20.1, 21.0 ms | 11.6, 11.5 ms | 54% |
| Faster CPU sync | 0, 0 | 16.8, 16.8 ms | 11.5, 11.3 ms | 53–54% |
| RSP task mode | 15, 11 | 28.3, 27.1 ms | 10.6, 10.6 ms | 50–51% |
| Skip cache timing | 0, 7 | 16.8, 25.4 ms | 12.0, 11.6 ms | 60–64% |

Every run averaged 60.0 FPS. Work per frame is the stats log's `AvgFrameTime`, which no longer
includes waiting for the display since the mailbox handoff.

- Faster CPU sync saves about 0.2 ms per frame (under 2%), within run-to-run variation. It stays
  an opt-in, default off: it costs nothing when off, and the Conker stall it exposed was fixed at
  the root (Count reads between syncs).
- RSP task mode cuts average work by about 8% but bunches RSP work into fewer frames, with 5–10
  times as many intervals over 20 ms. At 60 FPS that is worse pacing; it could only help uncapped
  fast-forward.
- Skip cache timing costs host CPU. The N64 CPU still runs a fixed number of cycles per frame,
  so cheaper instructions only mean more idle-loop iterations to emulate. It is an emulated-CPU
  overclock for games that slow down on hardware, not a host speedup.

**GPU driver waits.** An off-CPU profile of the same build (Mario vs Boo, 8 s), compared with
the profile in the [RSP pipeline copy](#2026-09-25-follow-up-rsp-pipeline-copy-restored-regression-fix)
section (build `f1fde64`): the emulation thread's off-CPU time with the GPU driver on the stack
fell from 14.9% to 3.8%, and inside the VI scanout path from 5.2% to 0.02%. The thread now sleeps
in the frame limiter 32% of the time, against 1% before. With the scanout submit's waits gone,
moving that submit off the emulation thread isn't worth doing. The two builds are before PR #7
and after PR #8, so this doesn't separate those changes' effects.

### 2026-09-26 follow-up: loops stay in the JIT block

Branch `feature/n64-taken-links-2026-09` ([PR #10](https://github.com/pwnedbygary/phobos/pull/10)),
stacked on the theme system ([PR #9](https://github.com/pwnedbygary/phobos/pull/9)).

**Finding.** Temporary exit counters in the CPU recompiler (Mario vs Boo, 30 s) counted about
10.1 million dispatcher round trips a second. 94% were taken conditional branches back to an
earlier instruction of the same block, i.e. loops, and 97% of those came from one four-instruction
loop where Mario Tennis polls a flag between frames (`lui v0, 0x8006` / `lw v0, 0x1f6c(v0)` /
`beq v0, zero, -3` / `nop`). A taken branch always ended the block, so every poll went back
through `CPU::instruction()`: the interrupt checks, the state-key computation and the block
lookup. Not-taken edges already continued inside the block or linked to the next one.

**Change.** A conditional branch whose taken target is an already emitted instruction of the same
block no longer sets `EndBlock` when taken. After the delay slot the taken edge passes the same
budget check as the fallthrough edge, plus a check that no mode-key change (Status, FCSR, RDRAM
map) happened, and jumps to the target's label. `EndBlock` then only signals an exception or an
invalidation, which still leave through the epilogue. The delay slot must not change the state key
or timer state, and no in-block SP key change may precede the branch; if the delay slot calls a
helper, the taken edge exits as before. Cycles are still charged per instruction, and the budget
exit happens at the same clock.

**Timing check.** With Asynchronous RDP off, where the emulation is deterministic, a temporary
probe logged an RDRAM hash, the CP0 Count register and the PC 120, 300, 600 and 1,200 frames
after loading the Mario vs Boo save state. Count and PC matched the previous build at every
checkpoint in every run, and the RDRAM hashes matched at 300, 600 and 1,200 frames. The frame-120
hash, and once the 1,200 one, also varied between runs of the same build (framebuffer data from
before the load). Boot isn't deterministic on this device even for one build, so boot runs weren't
comparable.

**Measured** (RP6 in High Performance mode, Async RDP on, fastest-core pin on; local release
builds of this branch with and without the change, runs interleaved):

| Metric | Before | After |
|---|---|---|
| Dispatcher round trips (instrumented builds) | 10.1 M/s | 0.6 M/s |
| Fast-forward, Mario vs Boo, uncapped FPS | 89.2, 89.2, 89.2 | 95.5, 95.3 |
| Fast-forward, measured time per frame | 11.4 ms | 10.6 ms |
| Mario vs Boo 30 s, measured time per frame | 11.9, 11.6 ms | 9.8, 10.9 ms |
| Mario vs Boo, intervals over 20 ms | 7, 1 | 4, 0 |

The poll loop itself still runs about 10 million times a second, now entirely as JIT code.
Smoke: F-Zero X, Paper Mario, Ocarina of Time,
Mischief Makers, Rogue Squadron, Hybrid Heaven, GT 64, Wave Race 64 Shindou Edition, Mario Kart 64
Amped Up, SM64 B3313 and Conker run at 60 FPS with no crashes or N64 stalls.

### Next (not implemented)

- Measure this change and the fastest-core pin in the RP6's Standard performance mode, and try
  a performance-hint session so the fast core clocks up there (the user saw High Performance
  mode run the fast core at 3.2 GHz instead of ~2.8 GHz, with visibly steadier frame times).
- Conker's intro runs on a mid core: under that lighter load Qualcomm core control keeps
  the fastest core paused, so the pin doesn't apply and frame intervals jitter. A
  performance-hint session might raise the mid core's clock there (untested).
- Idle-loop skip: for a pure polling loop (loads from cached RDRAM only, no stores or helper
  calls, loop-invariant registers), advance the clock to the budget end in one step, a whole
  number of iterations, which is exactly what running it would do. The Mario Tennis poll would
  then cost almost nothing instead of ~10 million iterations a second.
- Taken branches to other blocks, `JAL` and `JR` still return to the dispatcher (about 0.11,
  0.11 and 0.12 million a second in Mario vs Boo).
- The UI theme system ([PR #9](https://github.com/pwnedbygary/phobos/pull/9)) and the
  MangoHud-style performance overlay ([PR #5](https://github.com/pwnedbygary/phobos/pull/5))
  are done.