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
| `-fvisibility=hidden`, `-mcpu`/`-mtune`, PGO | (a)/(c) | Visibility risks libadrenotools' symbol lookups; the others need device A/B runs. |
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