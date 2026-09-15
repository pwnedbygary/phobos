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

## Pinned reference, not the user's installed configuration

Handoff identifies `https://github.com/pwnedbygary/mupen64plus-ae-turnip`.
`git ls-remote` and an isolated shallow clone of `master` agree on
`dc955483a97daa99cb1f9db06e2334464fa1664d`. This pins the source comparison only;
the user's installed branch/APK and benchmark settings remain unconfirmed.

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
| JIT cadence should be raised for full speed | `accuracy.hpp:9-16` is 2048*2 and records historic Conker regressions at larger cadence. Preserve default; old FPS comments are reports, not new runs. |
| Broad perf pass still needs basic compiler tuning | `CMakeLists.txt:4-5` already enables O3, ThinLTO, vectorization and unrolling. `android/app/build.gradle.kts:36-53` sets legacy armv8-a+simd / modern armv8.2-a+fp16+dotprod. Inspect actual compile commands before proposing flags. |
| Audio callback always locks/copies stream registry | Versioned cache already exists (`PhobosRunner.cpp:155-173`); don't propose an already-landed optimization. |
| Old overall compatibility/signing status is current | Historical reports remain useful regression targets, not fresh validation. Signing is outside scope. |

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

## Next bounded experiment and blockers

Use [the protocol](mario-tennis-benchmark.md) to identify the actual two installed
builds and collect external low-overhead CPU/scheduler/frame traces first.
No custom instrumentation is justified yet without a device to validate its
overhead. If external traces cannot attribute the waits, the next *separate*
reviewed change is an opt-in bounded stage recorder at the listed sites, with
no timing changes and with on/off overhead measurement.

Missing inputs: actual reference branch/revision and APK identity; target device/OS,
driver binary/version; legal game hash/region, reproducible scene and settings.
Historical handoff mentions Retroid Pocket 6 / Adreno 740, but this run has not
confirmed that device or its driver. These block native comparison, not this audit.

Documentation checks: `git diff --check`, internal Markdown link existence and
independent source-citation/snapshot review are the appropriate acceptance checks.
Actual final command results and review hashes are recorded in the commit record.