# Mario Tennis matched-native comparison protocol

Status: protocol established; **no matched runs collected**. Never replace blank
fields with remembered FPS. Source candidate pins are in [performance-audit.md](performance-audit.md).

## Run manifest (required before comparison)

Keep a private manifest per run and publish only sanitized numerical summaries.
Record:

- Phobos and reference full Git SHA, dirty diff hash, submodule SHAs, source-to-APK
  build record; package ID, versionCode/name, flavor, ABI, build type, compiler/
  NDK flags, APK SHA-256, native library build IDs and signing certificate digest.
  A source pin without an attested APK does not identify the tested executable.
- Device model, SoC, Android version/build, display refresh/resolution, power mode,
  battery/charging state, cooling, ambient temperature and thermal status/frequencies
  before/during/after. Keep device serial private; same physical device for both.
- Loaded Vulkan driver name/version, binary SHA-256, pipeline cache UUID, selected
  custom/system driver, GPU capabilities. “Turnip” alone is insufficient.
- Legally supplied Mario Tennis region/revision, size and SHA-256, firmware if
  relevant. Do not upload games/firmware. Record save data and per-emulator state
  hashes privately; never load a Phobos save-state into Mupen or vice versa.
- Exact scene, characters, court, camera, menu choices and timestamp/input script.
  Use a cold boot and identical deterministic attract segment if reproducible;
  otherwise record a fixed manual route and its variability. Separate intro and
  in-match tests; do not pool them. Pin start/end visual events and duration.
- CPU core/JIT, RSP implementation/HLE/LLE, RDP backend, synchronous RDP setting,
  resolution/upscale/output size, VI filters/AA/dither/divot/gamma/deinterlace,
  supersampling, frame skip, threaded rendering, cheats, expansion pak, audio rate/
  latency/mute, speed limiter, display vsync and all timing/overclock knobs.
  Export effective per-game settings, not merely global defaults.
- Capture tool/version/config, sampling frequency, timestamp clock, symbol mapping,
  permissions and unavailable counters; retain artifact hashes privately.

Two comparison modes must remain distinct:
1. **Accuracy-matched:** same applicable rendering/CPU/RSP accuracy and speed
   policy; Phobos SyncFull remains on, reference sync explicitly on. Document
   non-equivalent features; if material, do not label the pair accuracy-matched.
2. **User-experience:** actual chosen settings (including possible reference async/
   HLE), fully disclosed. Measures configurations, not renderer superiority.

## Collection procedure

1. Confirm both APK identities and upgrade compatibility. Back up saves using
   existing supported export; do not uninstall, clear storage, change signing or
   overwrite unbacked saves. Do not install if certificate/update safety is unknown.
2. Fix display/power/settings. Disable debug overlays and verbose logging on both.
   Keep audio enabled identically. Distinguish cold shader compilation from warm
   gameplay: warm each app/scene for 120 seconds, then measure 120 seconds.
   Keep caches intact for warm tests; cold tests require a separate safe protocol,
   not deleting app data. Record cache state and pipeline failures.
3. Cool to the same predefined thermal band (e.g. within 2°C at the same accessible
   sensor and same thermal status). If unavailable, report unmatched thermal control.
   Stop runs with throttling transitions, wrong scene, shader failure or interruptions;
   retain their counts/reasons, do not silently discard poor performance.
4. Run at least five paired repetitions per scene/mode, alternate A/B and B/A order
   (record order/seed). Report all valid runs and exclusions. Do not compare one
   cold run to one warm run.
5. First collect normal paced play. Then separately measure throughput with each
   application's verified uncapped mode, leaving emulated timing unchanged.
   Fast-forward often changes audio/skip policy: if not matchable, label throughput
   comparison non-equivalent instead of interpreting it as speedup.
6. Collect frame events and CPU running/runnable/sleep intervals with Android
   Perfetto/scheduler traces where permissions allow; sample native CPU stacks
   with simpleperf at a recorded modest rate (start at 100 Hz). Check installed
   tool help/capabilities first. SurfaceView/native-window frame events may not
   equal app UI frame metrics: validate the selected SurfaceFlinger layer.
   If GPU counters/timestamps are inaccessible, mark GPU attribution unknown.

## Metrics and attribution

Record per-run sample counts, p50/p90/p95/p99 and maximum **presented-frame
intervals**, missed deadlines, VI/emulation intervals, duplicate/dropped/fallback
frames, emulation speed versus game animation rate, audio xruns/latency, visual
correctness and hangs. FPS alone (VI or UI overlay) is insufficient.

Use separate tracks for emulation CPU execution, RSP CPU, renderer worker CPU,
SyncFull wall waits, scanout/fence waits, conversion/copy CPU, window lock/post and
pacing sleep. GPU timestamps measure GPU execution; CPU wait duration includes
scheduling/queueing and must not be called GPU time. Running CPU time is not
wall time. Overlapping intervals cannot be summed as independent frame costs.
Correlate clock domains explicitly; sample percentages are not precise stage times.

If extra instrumentation is required, implement it only as a separate reviewed
change: disabled by default, per-thread fixed-size buffers, monotonic timestamps,
frame/stage IDs, bounded counters and out-of-band export after run. No per-command
logcat, allocations or mutexes in measured hot paths; no added GPU waits.
GPU timestamp queries must be capability-checked, read asynchronously, and report
unavailable/disjoint/wrapped results rather than fabricate them. Separate inclusive
root->run from nested RSP/SyncFull. Do not change interrupt or fence behavior.

Quantify overhead with at least five interleaved recorder-OFF/ON pairs of the
same build and scene, plus external tracing OFF/ON. Report median and p95 frame
cost change, CPU time and variability. Predeclare a 2% median frame-time overhead
budget and no material p99/accuracy regression; if noise exceeds budget, increase
repeats or lower sampling and report unresolved overhead. Never subtract assumed
overhead or attribute instrumented-vs-uninstrumented differences to optimization.

## Results and acceptance

Use a row per run:

`run_id, app_sha, apk_sha, scene, settings_hash, mode, order, duration_s,
frame_count, p50_ms, p95_ms, p99_ms, max_ms, missed, cpu_ms, rsp_ms,
syncfull_wait_ms, scanout_wait_ms, gpu_ms, copy_ms, window_ms, pacing_ms,
xruns, temp_start, temp_end, validity, reason, capture_hash`

Use `unavailable`, not zero, for unmeasured fields. Summarize per-run distributions
and paired differences with variability/confidence intervals; frames in a run
are correlated, not thousands of independent trials. Declare a practical effect
threshold before testing; accept a speedup only if repeatable beyond noise and
without correctness, pacing, audio or thermal tradeoffs hidden in the result.

For a later patch add lifecycle/accuracy regression runs: Mario Tennis intro and
gameplay, Conker, Rogue Squadron, reset, state save/load, quit/reload, and representative
non-N64 threaded video/multi-stream audio cores. Stop on corruption/hangs.

Current results table: **empty**. Missing device access and run identities block
execution and comparative conclusions; they do not justify inventing measurements.