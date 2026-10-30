# PSP GPU statistics — the benchmark counters behind the 5x work

This is the note for the statistics-only instrumentation in `ares/psp/ge/gpu/` and `tools/psp-runner/`: what it adds,
what it costs, what it does not measure, and what it has already said about the Vulkan renderer at 5x. It changes
nothing about what is drawn; every mode is off unless the environment names it.

## The switches

- **`PHOBOS_GPU_STATS=1`** — counts, free (measured within noise: 383.7 ms against 386.9 ms median on a 300-frame 5x
  window). Per command kind (Draw, Upload, Readback, Copy, Present, Shot, Mesh, Move, Depth, Alpha): the commands
  recorded, the `area` their rectangles cover (one rectangle a command; at the GPU's scale for the kinds that fill a
  target, the PSP's size for Present and Shot, the read-back's scale for Readback; `null` for Draw and Mesh, whose
  triangles' coverage is not known here), and per target: made, shown (presented, shot, or read back as the host's
  picture), copied into a texture for draws to sample, with their rooms at the scale and the shown-only area.
- **`PHOBOS_GPU_STATS=time`** — the same, plus each kind's GPU time from a timestamp where the kind changes. A
  timestamp is a sync point: this mode costs about 3% of a 5x frame on llvmpipe (398.4 ms against 386.9 ms median), so
  only shares are quoted from it.
- **`PHOBOS_GPU_STATS=draws`** — the same, plus one line a draw beside the report (`<report>.draws.csv`): `ms`,
  `triangles`, `target`, `scale`, `pass`, `pipeline` (a fingerprint of the pipeline key's fields), `writes_depth` (the
  shader writes the depth and does the GE's range test), `shown`, `copied`. A draw whose timestamps were skipped has an
  empty `ms`. At most 400,000 draws and 64 targets are recorded; a long run covers its first part only. The run itself
  pays about 2.5% (634.8 s against 619.1 s for 1,200 frames), so its absolute times are inflated and only shares are
  used.

The runner also has `--frame-times FILE`: each frame's host time in milliseconds, written and flushed as the frame
ends, so a run the harness must kill keeps all but its last fifteen lines.

## What it does not measure

- **Fragment coverage of Draw and Mesh.** Their `area` is `null`: a draw's triangles' coverage is not known without
  asking the GPU. `writes_depth` marks the DEPTHS pipeline — the shader writes `gl_FragDepth` and does the range test —
  which is a *precondition* for paying hidden fragments, not proof that any fragment is hidden.
- **The hidden-fragment share itself.** Not measured; the design for measuring it (occlusion queries on the same
  geometry, image-neutral) is written but unbuilt.
- **The device.** Everything here is the host (lavapipe): a tile-based phone GPU prices fill, passes and bandwidth
  differently.

## What it has said about 5x

From the heavy Chains of Olympus window (1,200 frames from boot, Vulkan fast, 5x, 34 draws a frame, 7,071 triangles):

- Of a 511.6 ms frame, Draw is 89.1%, Mesh 5.5%, and **every target-wide operation together 5.4%** (Depth 2.8, Move
  1.2, Readback 1.0, Upload 0.3, Copy 0.07). The cost is inside the draws.
- The draws go into two 512x512 PSP targets (2560x2560 at 5x, about 26 MB of colour each) that are both shown and
  copied: **97.8% of draw time between them**. Shown-only targets are negligible.
- The DEPTHS pipelines are 80.7% of draw time (an upper bound, see above); the tail is small in count and large in
  time (slowest 1% of draws 22.3% of draw time).
- The blended and depth-write-off draws that break the opaque runs are **69.4% of draws**, sit almost entirely *after*
  the opaque block in the same pass and target (22,120 after, 1,520 between, 0 before), and are small-triangle,
  alpha-tested scene translucency and effects, not HUD. The interleaving is inherent to the content, so a pass-level
  depth pre-pass covers at most 3.3% of draw time and cannot pay for its second traversal and index traffic.

## Status of the code

Reviewed read-only to PASS on the snapshot it was committed from (see the commit message for the hashes). The modes
are off by default; with them unset the renderer's command stream and behaviour are unchanged. The measurements above
are host-only and say nothing about the RP6, whose GPU is where tiling and bandwidth could change the picture.
