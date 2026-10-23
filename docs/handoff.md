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

Standing note: the PSP core's PGD/NPDRM code is open to agents (the owner's decision, 15 Oct 2026).
`ares/psp/kernel/pgd.cpp`, `pgd.hpp`, io.cpp's sceNpDrm functions, its PGD ioctls and the EDATA path are written and
maintained by agents like the rest of the core, and the owner approves their PRs as usual. Entries and parts from
before that day call it "the owner's area" and leave it to him: true then, history now.

Current focus (2026-10-13): the PSP core (its parts in [psp-core.md](psp-core.md), and the
entries below, newest first) and the
[test and performance plan](implementation-plan.md#test-and-performance-plan-2026-10-13):
PSP throughput, fast-forward and Vulkan at 1x, 2x, 4x and 5x, accurate and fast modes
measured apart, with the N64/Mario Tennis regressions kept. A matched Phobos/v336
comparison (APK, device, driver, settings, scene and traces) has still not been made, and
no overall speedup or parity is claimed. Do not resume the cancelled signing or
audio-buffer tasks without new authorization. Preserve saves and do not assume APK update
compatibility.

Publication preparation: the plan reconciliation received independent PASS,
including byte-for-byte preservation of the original plan in its archival appendix.
This follow-up corrects cancelled signing-task status and adds this portable entry
point, and restores tracking of the existing `.gitmodules` file for fresh clones.
Documentation and Git-metadata checks/review accompany the commit; no APK/device test
is implied. Verify GitHub's branch tip against local HEAD after publication.

## PSP Vulkan: Burnout Legends at 4x shown from the GPU, after a finish and past a texture's move — 2026-10-22

Branch `cursor/psp-upscale-shown-cachy`, from master `062dd53e2`, with master `f373b9e7e` (#297 and #299) merged in.
ares/psp/ge/gpu/gpu.cpp, tests/psp/gpu.cpp, docs/psp-core.md (part 117), docs/psp-gpu-renderers.md ("Upscaling",
"Presenting", part 49's RP6 line), docs/implementation-plan.md (package 10's row) and this file.
The owner's report (22 Oct, the RP6, Vulkan (fast) at 4x and 5x): the PSP still looked native; his screenshot of
Burnout Legends' "Select a Compact" was 4x4 blocks. The investigation's notes, frames, scripts and logs are kept
outside the repository.
- **What** (part 117):
  - `GPU::shownTarget()`: above 1x a target owning no page shown is shown where its other checks pass. A finish
    another frame buffer asked for had given its pages back (Burnout's movie-backed menus refuse a full-screen sprite
    each frame), and every frame was memory's 480x272. At 1x as before.
  - `GPU::holds()`: a texture running from one frame buffer into the one below it is taken in two parts, nothing cut,
    before anything is moved; moved only where it can't be. Burnout's car select samples its picture as a texture of
    1024 rows, and the move gave the frame shown's pages to the other target.
  - Three tests, `gpuShownAfterFinish`, `gpuShownBelowTexture` and `gpuBelowTextureTexels` (the two-part take's texels
    against the software renderer at 1x), passing with the GE on the emulation thread and on its own. In scratch
    builds the GPU group fails 2 checks without `shownTarget()`'s clause (the first test), 5 without `holds()`' rule
    (the second and the third), 7 without either, and 5 with the second part copied a row off (the third and
    gpuStackedTexture).
- **Measured** (the desktop runner, RADV): Burnout at 4x 521 to 4,988 of 4,990 frames from the GPU, 4x4 blocks with
  detail none to 27.3-27.8% (menus) and 59% (car select); at 1x the same pictures byte for byte, compared by the frame
  each arrived in and in dense captures. At 4x, the same frames byte for byte: Midnight Club 3, Liberty City Stories
  (its 88 moves now none), Lumines, God of War: Chains of Olympus (its 76,752 moves kept), Colin McRae Rally 2005 and
  Dirt 2.
- **Checks** (GCC 16.2.1, RADV): the sanitized suite, master 650 groups, 0 failures, with the change 653, 0; its GPU
  group with the GE on its thread 81, 0; the GPU group on lavapipe, master 78, 0 (one skipped), with the change 81, 0
  (the same one); release with `-Wno-error=array-bounds` (GCC 16's false positives in movies.cpp), master 650, 0, with
  the change 653, 0. `tools/psp-runner/build-android.sh` (NDK r28, the app's release flags) builds it.
  `git diff --check` clean; no added line over 120 columns.
- **Left**: Burnout's top banner at 4x has a dark line at the bottom of native row 63 (a 512x64 sprite filtered over
  its texture's last row into its first, which repeats): in the 4x target before this change too, shown now; a
  follow-up. The RP6 not run (the same functions present there).
- Independent review: its first round (NEEDS CHANGES) found `gpuShownAfterFinish` failing with the GE on its thread;
  fixed here, with a texel oracle for the two-part take and the docs' nits. The delta review's verdict is in the commit
  message; the look at master's merge into this branch (#297's and #299's docs beside these), in the message of the
  commit merging this branch into master.

## PSP kernel: the polling skip widened to the EA games' and Midnight Club's loops — 2026-10-21

Branch `cursor/psp-spin-widening-cachy`, from master `8c2b1ddee` (#295): W1 (`c793ab17d`), W2 (`a7552b776`) and the
docs. ares/psp/kernel (threads.cpp, interrupts.cpp, events.cpp, spins.cpp, kernel.cpp, kernel.hpp),
tests/psp/spins.cpp, docs/psp-core.md (part 116; part 105's Left) and this file (part 105's entry's Left; #295's
entry). The design: its third round, passed by its review's round two; the design, its reviews and the notes, scripts
and logs are kept outside the repository. No game was run: the games' exactness and host cost are the next round's
(the second round ran them: below). Master `062dd53e2` (#296) merged in as `760ee14a7` (only this file conflicted;
both entries kept).
- **What**: W1, a semaphore's wait taking its count at once (unless its callbacks run) and its refusals (but
  ILLEGAL_CONTEXT) made calm, and a spin's turn working out its waits and counting signals on the shadow
  (`spinWaitSemaphore()`, `spinSignalSema()`), through decisions the kernel and the shadow share (`mayWaitRefused()`,
  `semaphoreWaitRefused()`, `signalRefused()` with `held`, `wouldPreempt()`), each given the interrupts as the turn
  has them; `spinProve()` passes only with every semaphore back where it was. W2, a callback check with none due made
  calm and worked out (`spinCheckCallback()`, declining the context path and a check with one due). The skip is
  otherwise unchanged; `spinSkip()` counts each of the PSP's GE timing's refusals for tests (`Kernel::spinChecks`),
  deciding nothing and kept in no state. Part 116 has the exactness argument: the go's end, with all three of
  `geDriven()`'s callers; W1's proof; the rule that every writer of a thread's status, priority or suspension, and of
  `dispatchSuspended`, is neither quiet nor calm or runs between goes, the rotation aside.
- **Why**: the EA games' loops (Need for Speed: Shift, Burnout Dominator and Legends) signal a semaphore and take it
  back with a wait that succeeds at once, twice a turn, and Midnight Club: L.A. Remix's checks its callbacks with none
  due: neither call was calm, so the detector started afresh every turn and the loops were never skipped (part 105's
  Left, closed). The design's prototype took those games' host instructions under the PSP's timing down 50-79% (the
  EA three) and 24% (Midnight Club), every state master's (its runs kept outside the repository); this code's own
  runs, the second round's: 79%, 71% and 50%, and 24%, every state master's. Both are the host instructions of the
  headless runner's -O1 build on one x86-64 desktop, not the app's build (The games, below).
- **Tests** (tests/psp/spins.cpp; each new group named "the GE's looks polled beside …", so that CI's GE-thread step
  runs it with the thread on too; part 116 gives each case's turns): Shift's turn skipped as run (88,629 turns) and
  one taking more than it gives not; a CB wait skipped (151,563), an asynchronous read's callback run once at the next
  CB wait as with every turn run, and a try made directly with the callback notified refused; waits refused
  (CAN_NOT_WAIT with the anchor's call made with interrupts on, an unknown semaphore, ILLEGAL_COUNT twice) and a
  signal refused only in the turn's view, skipped, each result the kernel's; a signal with a better thread ready not
  skipped, the anchor's call made with interrupts on or off, and with a thread of main's priority, or a better one
  suspended, skipped (233,492); a signal a thread waits on, and a wait that waits, not skipped; W2's callback check
  skipped with none due (187,950), the callback notified by a timer's handler or a read run once at the next check,
  and a try made directly with it notified refused. #295's groups and #289's kink group check their own refusals
  through `Kernel::spinChecks`; the two-calls group's skip at once also checks what the other refusals need, and its
  `positioned()` loops are bounded (#295's review, suggestions 1 and 2).
- **Mutations** (the diffs and logs kept outside the repository), each through the release suite, the GE thread off,
  CI's PSP-timing step and its GE-thread step, failed checks. With these tests all 18 fail in all three (656, 120
  and 76 groups), the same number in each but `closing`'s: `closing` 8, 8, 4; `waitcalm` 12; `cbcalm` 4; `cbdue` 2;
  the refusals' `calm` 2, 2, 4 and their codes 2, 2, 4; `held` 12; `preempt` 8; `preempt-cpuflag` 4;
  `refused-cpuflag` 6; `preempt-equal` 2; `preempt-suspended` 2; `checkcalm` 6; `pending` 2. With master's tests
  every one passes (650/0, 114/0, 70/0) but `closing`, 650/4, 114/4, 70/0, through "spins refused"'s semaphore that
  counts. `cbdue` and `pending` fail only in the try made directly. `cbdue`'s first runs didn't build (its parameter
  left unused); they were run again with `(void)callbacks;`.
- **#295's groups' counts, taken again** in a scratch copy after the rework (its log kept outside the repository):
  line for line what #295 counted on `45eb68d3c` (74,345 refused part way and 213 skips; 1,727 refused with several
  calls and 3 skips; 3 refused unsteady and 36 skips; 1 refetch marked as VRAM's).
- **Checks** on `a7552b776`, the worktree clean (each log, kept outside the repository, starts with the tree and its
  status lines): the release suite with the GE thread off 656 groups, 0 failures, on 656, 0; CI's PSP-timing step
  120, 0; its GE-thread step 76, 0; the six new groups and #295's four sanitized (ASan and UBSan), the thread off 10,
  0 and on 10, 0, no sanitizer report; W1's commit alone (`c793ab17d`) through the release suite, the thread off,
  655, 0. `git diff --check` and `git show --check` clean on each commit; no added line over 120 columns.
- **Departures from the prototype** (the design's third round's; the two set against each other in a diff kept outside
  the repository, every decision the same): split into W1 and W2; `semaphoreWaitRefused()` hands back the semaphore
  it found (the design review's suggestion 1), no longer `const`; `spinSkip()`'s counters for tests (#295's review,
  suggestion 1); the comments the prototype left stale, and its "(W1)" and "(W2)" labels.
- **The games** (the second round; part 116's "The games"): master `062dd53e2` against `760ee14a7`, each runner with
  the same scratch patch (each frame's XXH3-128 of `root->serialize(true)`'s bytes, `kernel.spun` in the summary, the
  saved state's frame named). Exactness from boot, 2,400 frames, the Software renderer: Burnout Dominator, Need for
  Speed: Shift, Burnout Legends, Midnight Club: L.A. Remix and Resistance: Retribution, both GE timings, the GE thread
  off and on, every frame's state master's in all 20 configurations (48,000 frames), and the state saved at the top of
  frame 2,400 (after 2,399 frames) too. Master skips none of the four games' loops; the branch skips 3.5 to 41.6
  million turns in each, and as many of Resistance's as master. Host cost, eight polling games (3,600 frames, Vulkan
  fast with late frames, the GE thread off, two rounds taking turns), in the emulation thread's user-mode host
  instructions a frame, from the headless runner as tools/psp-runner/build.sh builds it (-O1, `BUILD_DEBUG`, GCC 16)
  on one x86-64 desktop; the Android app builds at -O3 with LTO and wasn't measured, nor were the RP6 and the Mac:
  under the PSP's timing Dominator 147.56 to 30.55 M (−79.3%), Shift 133.06 to 38.26 (−71.2%), Legends 220.96 to
  110.82 (−49.8%), Midnight Club 93.46 to 71.49 (−23.5%); under "Fast" −26.0%, −32.4%, −5.6% and −16.1%; the other
  four games within 0.01%; every state at frame 3,600 master's.
- Independent review: PASS on `2ddcb6237`, independent and read-only (round one: no high or medium finding; one low,
  the PR description's CI image version, corrected there; two informational, the conflict with #296's entry, resolved
  in the merge, and W1's message's "the PSP's" for the kernel's results). The delta round on the merge, `869dd95af`
  and `b1bb7b449` found two wording matters (NEEDS CHANGES): files kept outside the repository still named by path,
  and the host costs' build and host unsaid. The docs commit that adds this line fixes them; the delta round's verdict
  on it goes in the message of the commit merging this branch into master.

## Licenses: the notices of the third-party code the builds ship that LICENSE lacked — 2026-10-21

Branch `cursor/license-notices-cachy`, from master `8d85eda9a` (#293), with master `8c2b1ddee` (#295) merged in.
LICENSE, this file and LicenseNoticesTest.kt (three more required names); no other code, no packaging change. Notes,
listings and checksums: `~/phobos-work/scratch/license-notices/` (the briefs:
`~/phobos-work/scratch/prompts/license-notices.txt`, `license-notices-r2.txt` and `license-notices-r3.txt`).
- **What**: LICENSE gains the notices of the third-party code inside what the builds ship: SDL 3.2.30 and the code it
  bundles, with PipeWire's SPA header code; MoltenVK 1.4.2 and the libraries in its dylib; the AppImage's runtime and
  the libraries linked into it; the NDK's libc++ in the APKs; and its "Android and Kotlin libraries" notice names the
  rest of the Maven libraries the APKs carry. 19 blocks, 38 notices to 57; the Apache text's "Used by" line names
  MoltenVK, SPIRV-Cross and SPIRV-Tools.
- **Why**: the owner's choice (21 Oct, ~18:27): "LICENSE: a small PR adding the notices for whatever the builds
  actually include." Building the website, its author had found LICENSE naming none of SDL 3, MoltenVK, VMA, jansson or
  GLFW. Round one left the code inside SDL, MoltenVK's dylib and the AppImage's runtime for the coordinator, who put it
  in this PR (round two), as it ships; round three made the independent review's fixes.
- **How it was established**: from the build files (CMakeLists.txt, the packaging scripts, desktop.yml, android.yml,
  build.gradle.kts) and from the artifacts: v2.4.0's AppImage, Windows zip, macOS zip and APKs and nightly-216017's
  APKs, downloaded with gh (each file's sha256 GitHub's), read as archives (the AppImage's squashfs from its offset,
  944,632, with 7z), their binaries by strings and symbols; in round three also this PR's CI AppImage, macOS zip and
  APKs (runs 39693815936 and 39693816063). Nothing was run.
- **Added in round one**: SDL 3.2.30, linked statically into all three desktop programs (FetchContent,
  `SDL3::SDL3-static`): each carries `SDL-3.2.30-release-3.2.30`. MoltenVK 1.4.2:
  `Contents/Frameworks/libMoltenVK.dylib` in the macOS app, whose `Resources/LICENSE-MoltenVK` is MoltenVK's LICENSE at
  `v1.4.2` byte for byte (the Apache text). SDL's block is its file word for word, without the spaces three of its
  lines end with (git diff --check rejects them). Both after MinGW-w64's block.
- **Round two, MoltenVK's dylib**: byte for byte the one in MoltenVK v1.4.2's release (`MoltenVK-macos.tar`, its sha256
  GitHub's), so its libraries are the revisions its `ExternalRevisions` pin. Its symbols: SPIRV-Cross `6c09849f` (its
  parser and GLSL, MSL and reflection compilers), SPIRV-Tools `0d6fd73c`'s core library only (no optimizer, validator,
  reducer, linker, linter or differ), cereal `a56bad8b`'s binary archives (none of its JSON or XML code). SPIRV-Cross
  and SPIRV-Tools (LICENSE files the Apache text, no NOTICE files): the copyright lines of the files their symbols come
  from, and a pointer to the Apache text; cereal: its BSD-3-Clause LICENSE. Beyond the brief's list, SPIRV-Headers
  `29981f65`: SPIRV-Tools' tables are generated from its grammar files (their names, the core grammar's to Arm's TOSA
  set's, are in the dylib's strings) and SPIRV-Cross compiles its spirv.hpp's inline functions; its block carries its
  LICENSE's notice, each shipped grammar's own statement (Khronos's; Arm's for TOSA, the motion engine, graph debug
  info and, from round three, experimental ML operations, `Arm.ExperimentalMLOperations.` being in the dylib) and the
  MIT text they name.
- **Round two, SDL's bundled code**, by symbols (the Linux and macOS programs keep theirs) and by strings or bytes (the
  stripped Phobos.exe), each block word for word from SDL's tree at `release-3.2.30`, its first line naming the
  platforms. In all three: HIDAPI (the BSD-style choice, LICENSE-bsd.txt, and from round three the lines its three
  backends carry, "libusb/hidapi Team ... Copyright 2022"; hidraw, Windows and macOS backends), yuv2rgb (its YUV2RGB
  table, computed from its source, in all three), stb_image 2.30's JPEG decoder (its JPEG messages in all three),
  SDL_rotate from SDL_gfx (on Windows by the software renderer, whose one string is there and which calls it). Under
  SDL's zlib license with their own copyright lines, in all three: Valve's controller table, Max Maisel's Steam Deck
  driver and, from round three, Gareth McCaughan's qsort and Collabora's SDL_ReadSurfacePixel (both inferred on
  Windows: no LTO or section garbage collection in the desktop build, so SDL's objects go in whole); on Linux: Igalia's
  X11 settings and, from round three, Collabora's evdev capabilities, NVIDIA's X11 OpenGL code and Wim Taymans's
  PipeWire camera driver. On Linux only: the X11 EDID parser, the XSETTINGS client, imKStoUCS, xdg-user-dirs' lookup
  functions, the 22 Wayland protocol files, each one's every interface in the program (21 share one permission notice,
  given once after their copyright lines), and from round three PipeWire's SPA header code (Ubuntu 22.04's
  0.3.48-1ubuntu3.2: 39 SPA and 30 pipewire headers reached, five copyright lines, one MIT text; four spa_pod_*
  functions kept out of line). Left out: SDL's libm, OpenVR and the libusb backend (in no binary: no symbols or
  strings, and three fdlibm tables in none), Valve's two Steam headers and the Khronos, Mesa and Microsoft headers
  (declarations and constants only), SDL's test library (not built: no SDLTest_ symbols) and public-domain code
  (dlmalloc, the CRCs, murmur3, PDCLib's strtok_r, the VGA font).
- **The AppImage's runtime** (its first 944,632 bytes): type2-runtime's continuous build, which appimagetool downloads
  when the AppImage is made, naming its own commit: `8f39b89` in v2.4.0's, `fedfad51a` in this PR's CI AppImage
  (`type2-runtime/commit/fedfad5`; the review's M1, for which round three rewords the notice). Built on Alpine 3.21 (its
  `.comment`), linked (type2-runtime's Makefile) with musl, libfuse 3.15.0 (patched by type2-runtime), squashfuse 0.5.2,
  zstd, zlib and mimalloc, which the runtime's own LICENSE omits; in 2.4.0's, zstd 1.5.6 and zlib 1.3.2 (their
  version strings), musl 1.2.5 and mimalloc 2.1.7 (inferred). One block after MinGW-w64's: each part's license and its
  project's source; libfuse's LGPL2.txt, zstd's LICENSE and zlib's terms are word for word texts LICENSE has (FFmpeg's
  LGPL 2.1, Zstandard's, zlib's), so pointed to; the rest word for word, musl's COPYRIGHT indented (two of its lines
  are 70-dash rules). No Alpine package revisions: a runtime doesn't record them.
- **The runtime isn't pinned**: scripts/package-linux-appimage.sh fetches appimagetool's continuous build, which
  downloads type2-runtime's continuous runtime unless given `--runtime-file` (src/appimagetool_fetch_runtime.cpp), and
  the notice is worded for that. type2-runtime's one fixed release, `20251108`, predates `8f39b89`'s change that creates
  extraction directories with mode 0700. Left for the owner or later PRs: the pin (a hosted copy checked by sha256,
  passed with `--runtime-file`, and a CI step comparing the runtime's commit string with the notice), libfuse's source
  beside each build (LGPL 2.1 §6), and appimagetool and linuxdeploy pinned.
- **What the AppImage bundles**: this PR's CI AppImage (run 39693815936, read as an archive) holds two shared
  libraries, usr/lib/libavcodec.so.63 and libavutil.so.61, FFmpeg's, under its notice; besides them, the runtime,
  usr/bin/phobos, AppRun, the desktop files and icons, the data folders and LICENSE and COPYING.
- **In the repository, in no build**: VMA (nothing includes `vk_mem_alloc.h`), jansson (`thirdparty/jansson` holds only
  `jansson_config.h`, an include path nothing uses), librashader (two headers nothing includes; no library in any
  package), GLFW (`thirdparty/GLFW` is a five-function stub Phobos wrote, no GLFW code, and no binary keeps it); none
  of their strings in any binary. Also unbuilt though LICENSE has their notices: the OpenGL and EGL registry headers
  (no compiled source includes them) and zlib (Android links the system's `libz.so`; of what ships, only the
  AppImage's runtime has zlib, above).
- **Shipping, with notices already**: in every build, sljit, libchdr with LZMA, miniz and Zstandard, xxHash (inline),
  volk, paraLLEl-RDP, Vulkan-Headers, TZXFile (`ZXTape!` in every binary), ymfm, qon with `qoi2.h`, MAME's portions,
  puff, FFmpeg, the firmware, C-BIOS and the ZX Spectrum ROMs; sse2neon in the APKs and the Mac's arm64 half; MinGW-w64
  and winpthreads in Phobos.exe; libadrenotools, the ten fonts (equal by sha256 to `res/font`'s), the console icons
  and font-rs's translation in the APKs.
- **Android's libraries**: build.gradle.kts's 16 implementation lines, and in every APK 80 META-INF version files and
  the classes the dex files define. The notice named the direct AndroidX and Compose libraries (not
  ui-tooling-preview), Material, Kotlin's, Coil, OkHttp, Okio, AndroidSVG, Protocol Buffers and the Public Suffix List;
  now also the AndroidX libraries those use (by group), Compose's others, Accompanist's Drawable Painter, Kotlin's
  Parcelize and Android Extensions runtimes, JetBrains' annotations, Guava's ListenableFuture, Error Prone's and
  JSpecify's annotations, each with its holder from its sources. All Apache-2.0.
- **The Windows zip** carries `LICENSE.txt` and `COPYING.txt` (scripts/package-windows.sh:48-49; v2.4.0's has both),
  so no packaging change.
- **atomicfu and libc++**: atomicfu stays named in the Android libraries notice. libc++ is linked statically into
  three of the APKs' native libraries, with LLVM's other runtime libraries: libc++abi and libunwind in
  libphobos_android.so and libhook_impl.so (their own messages), and compiler-rt's builtins in those, in
  libdatastore_shared_counter.so and FFmpeg's libavcodec.so and libavutil.so (its Android atomics check, `ro.arch` and
  `exynos9810` with a `getauxval` import) and, from Apple's toolchain, in the macOS app's Phobos and libMoltenVK.dylib
  (`___isOSVersionAtLeast`, both architectures). Their license is Apache 2.0 with the LLVM Exceptions, which ask for no
  notice in object code, but code not relicensed keeps LLVM's legacy license, and whether any is in the builds isn't
  known, so they have a short notice (round three: the review's L3, for libc++; the follow-up: R2-L1, for the others,
  whose LICENSE.TXT files are libc++'s but for their names and compiler-rt's MIT copyright line).
- **Checks**: LicensesScreen's split re-implemented (python3 -B): 38 notices on master, 40 after round one, 55 after
  round two, 57 after round three (58 in the APK with COPYING's), titles unique, no notice line exactly 70 dashes; each
  round-two block's words against its sources, whitespace normalised (91 checks, at round two's head), and at round
  three's its new and changed lines (33) and the review's own 119 (its verbatim.py), all holding; master's assembly
  equals nightly-216017's `assets/licenses/LICENSE.txt` byte for byte, and round two's head's APKs carry its LICENSE so
  assembled; LicenseNoticesTest holds by reading (it pins no count, but the first two titles, unique titles and, from
  round three, 19 names each starting one; its assertions mirrored in Python hold); `git diff --check` and `git show
  --check` clean. CI runs the unit tests; its runs go in the PR's body.
- Independent review: NEEDS CHANGES for `94d7802d1` (runtime notice names `8f39b89`; its own AppImage has `fedfad51a`).
  Fixed after it: M1, L1 to L4 and the HIDAPI and test suggestions in `24ca53ef6` to `a568e627a`; N1 in the PR body.
- Follow-up, from master `062dd53e2`: R2-L1 and R2-N1 to R2-N3 from the review's second round; libc++ in the Apache
  text's "Used by" line; LicenseNoticesTest requires "SDL 3.2.30 (", which only SDL's own notice starts.

## Tests: the spin skip's untested refusals and the GE thread's refetch charge — 2026-10-21

Branch `cursor/psp-spin-tests-cachy`, from master `c837d1868` (#291), master `8d85eda9a` (#293) merged in
(`706fc295f`). tests/psp/spins.cpp, tests/psp/ge.cpp, docs/psp-core.md (part 115's Left) and this file; no emulation
code. The polling skip's widening, phase 2's first step: F1's tests (the design,
`~/phobos-work/scratch/psp-ge-profile/spinwide/DESIGN-spin-widening-r2.md`), recalibrated for the hybrid. Notes,
scripts and logs: `~/phobos-work/scratch/psp-ge-profile/spinwide/tests/`.
- **What**: four tests, each passing as written and failing with what it checks taken out, which master's tests all
  pass without:
  - "…its list stalled: no skip past the GE's end" (`spinsGePartway`): spinSkip()'s refusal of a skip the GE would be
    done part way through. geWait()'s list stalled as the GE draws, its mover now every 4000 microseconds (geWait()'s
    new `every`; 2000 as before for the others), so steps end with the GE waiting at the stall. Master's threaded
    group of that list passes without the refusal: its mover sets the GE going again within a step, and the GE's time
    then starts afresh from the clock (geResume()).
  - "…two calls a turn moving lines: no skip of a push that zig-zags" (`spinsGeTwoCalls`): the refusal of several
    calls a turn with lines moved (#289's review, suggestion 4). F1's poll, its lines before its first call, would end
    a skip where the push peaks, which is exact; here the anchor's call ends the turn's instructions-only stretch,
    where the push dips, past the busy window's kink with the push rising. The turns after such a skip make up what it
    fell short by before a step ends, so the test also makes a skip at once, as spinsDirect does: without the refusal
    it pushes the GE back 38,806 cycles where the turns run push it 38,827.
  - "…its turns settling: no skip till the last two turns move alike" (`spinsGeSettling`): the refusal when the last
    two turns moved different lines; k = min(k + 1, 5) and movz pick a line not read before on the fifth turn only.
  - "ge time, the PSP's, with the GE on its thread: a list going one way into VRAM, its refetch where the list goes
    on" (`geTimePspOneWayThreaded`): the GE thread's own refetch charge (`ares/psp/ge/thread.cpp:762-766`; #291's delta
    review's low finding): a list in RAM JUMPing into VRAM and ending there, run without the thread and with it, its
    ramClocks[1] 3 × Cost::Command (6.69) and allClocks 6 × Cost::Command + Cost::Jump (55.78) worked out by hand, on
    both GEs. The review's mutation D (`~/phobos-work/scratch/review-291/r2/mut/mutD-thread-refetch-by-word.diff`)
    makes the render GE's ramClocks[1] 49.09.
- **Reaching them** (`tests/inst.sh`: a copy counting spinSkip()'s refusals by arm, never committed;
  `tests/logs/inst-*.log`): the stalled list 74,345 refusals part way and 213 skips (5,883,259 turns); two calls a turn
  1,727 refusals of several calls, the at-once try among them, and 3 skips on the interpreter (5,842 turns); the
  settling turns 3 refusals unsteady (one a list, on the recompiler) and 36 skips (811,836 turns); the one-way list 1
  refetch marked as VRAM's.
- **Mutations** (`tests/mut/`: each diff as made on this tree and each run's log; r2/mut's three made again, D the
  review's), each run through the release suite with the GE thread off, CI's PSP-timing step and its GE-thread step,
  groups / failed checks. With master's tests every run passes: 646/0, 110/0 and 66/0 for each. With these, each
  fails at its own test only, in all three:
  - partway: 650/2, 114/2, 70/2 (the stalled list's states differ after step 1, with both kinds of traffic);
  - multicall: 650/1, 114/1, 70/1 (the skip at once: the GE done at 1,520,857 against 1,520,878 with the turns run);
  - steady: 650/1, 114/1, 70/1 (the settling turns' states differ after step 1, on the recompiler);
  - D: 650/2, 114/2, 70/2 (ramClocks[1] 49.09 against 6.69 by hand, on the GE folded and on the render GE).
- **Checks** on `c837d1868` with the two test files as committed in `60810b8ad` (each log in `tests/logs/` starts with
  the tree, its status lines and the files' hashes): the release suite with the GE thread off 650 groups, 0 failures,
  on 650, 0; CI's PSP-timing step 114, 0; its GE-thread step 70, 0; the four groups sanitized (ASan and UBSan), the
  thread off 4, 0 and on 4, 0. `git diff --check` and `git show --check` clean; no added line over 120 columns; the
  diff from master `8d85eda9a` is the two test files, docs/handoff.md and docs/psp-core.md.
- **Departures from F1's constructions** (besides the two-calls group's and the mover's, above; the review's L1 and
  its suggestion 3):
  - The settling group reads its new line on the fifth turn, not on the fourth as F1's construction does (phase 1's
    DESIGN-spin-widening.md:206-212). spinTurn() counts turns from the spin's third call, so the first try compares
    the fourth and fifth turns; with F1's construction the fifth turn moves nothing, so a skip would be exact without
    the steady refusal (the review ran F1's construction with the refusal taken out, and it passed). F4's hole test
    (DESIGN-spin-widening.md:214-217) needs the same shift: Y on the fourth turn, X on the fifth.
  - The part-way group's poll differs from F1's: F1 polled sceKernelGetThreadId() with one uncached read; the group
    polls sceGeDrawSync(1) through geWait(), with uncached and missing-line traffic. The change is harmless: the
    refusal is reached 74,345 times and its mutation is caught.
- Independent review: PASS on `45eb68d3c`, with runs (`~/phobos-work/scratch/review-295/REVIEW.md`); one low finding.
  Taken after it: L1 and suggestions 3 and 4 in the next commit; 1 and 2 left for the widening's rework of spinSkip().

## Docs and CI: the wording points #290's, #291's and #292's reviews left — 2026-10-21

Branch `cursor/docs-review-followups-cachy`, from master `c837d1868` (#291). `.github/workflows/desktop.yml`,
docs/psp-core.md (part 115) and this file; no emulation code. Sources: the delta reviews,
`~/phobos-work/scratch/review-290/REVIEW-r2.md`, `review-291/REVIEW-r2.md` and `review-292/REVIEW-r2.md`. Notes:
`~/phobos-work/scratch/docs-followups/`.
- **desktop.yml's N64 dialect check** (#290's I1): GCC's `-ftrapv` and `-fwrapv` override each other, so a later
  `-ftrapv` undoes the dialect's `-fwrapv`; the "undone later" pattern and its error name it now. The step's run: block
  as GitHub runs it, on copies of #290's stand-ins: 15 cases as expected, the 13 from before and two new, a later
  `-ftrapv` failing the step ("1 of 38 N64 compile commands undo a flag") and an earlier one, which `-fwrapv` overrides,
  passing; master's step passes the later one too. actionlint: master's two desktop.yml warnings only.
  After this PR's review (its L1) the check also counts a later `-fstrict-overflow`, which GCC's manual says implies
  `-fno-wrapv`: 18 cases as expected, a later one failing (a22cb99b6's step passes it), an earlier one and a later
  `-fno-strict-overflow`, which implies `-fwrapv`, passing.
- **#290's entry** (I2): the recompilers count cycles a little differently in some of a game's blocks of several
  instructions, while the test's cases, one instruction under test each, count the same.
- **#292's entry** (nits 1 and 2): the RP6's frames over 20 ms in one order, the control's then the PR's
  (`~/phobos-work/scratch/n64-adreno/rp6/compare-292.txt`); the structure check names the store at fault, or for the
  changed module its changed constant.
- **#291's entry and part 115** (rulings (1) and (2)): master `d82f5db6f` (#292) merged in (`0e4fa0a80`) beside #290's
  merge, and part 115's checks say what #292 changed; what the runner is built from is #291's PR body's list
  (`ares/psp/`, `ares/ares/`, `tools/psp-runner/`, `tests/psp/ares/`, `nall/`, `libco/`, `thirdparty/`): the folders of
  every source tools/psp-runner/build.sh compiles, and of every file its objects depend on (review-291's list for 25
  objects, 1,004 files) but FFmpeg's built headers and the system's.
- **After this PR's review** (its suggestion 2): #290's, #291's and #292's entries record their delta reviews' PASS,
  #291's also how it merged with no macOS runner for its head; #291's Left and part 115's add the GE thread's own
  refetch charge, which no test catches a slip in (#291's delta review's low finding).
- **Checks**: `git diff --check` and `git show --check` clean; no added line over 120 columns; the diff is desktop.yml,
  docs/handoff.md and docs/psp-core.md. CI's runs go in the pull request's body.
- Independent review: PASS on `a22cb99b6`, read-only (`~/phobos-work/scratch/review-293/REVIEW.md`); one low finding.
  Taken after it: L1 (a later `-fstrict-overflow`) and suggestion 2 in the next two commits, suggestion 3 in the body.

## PSP GE timing: the CPU beside the GE refitted, Super Monkey Ball within 15% and its exception gone — 2026-10-20

Branch `cursor/psp-smba-refit-cachy`, from master `0f7c17d8a` (#289), master `1257da75d` (#290) merged in
(`a2c64338e`), master `d82f5db6f` (#292) merged in (`0e4fa0a80`). The core (`ares/psp/ge/`, `ares/psp/kernel/`), the
runner's counter line, the PSP tests and CI's `ge-round10c`, `tools/psp-measure/` (contention12.py, its README,
ge-terms.py), `tools/psp-ge-profile/` (gate.py, its tests and the README's line on them), docs/psp-core.md part 115 and
this file. Notes and logs: `~/phobos-work/scratch/psp-ge-profile/smba-refit/` (the design, then step 1's probe to step
6's review fixes).
- **What**: #279's CPU beside the GE (part 113) refitted to the PSP's rounds 10, 10b and 10c in a hybrid form: the
  front end's vertex and list-word reads by #279's rule, texels and transfers a fixed time a line; a cap at 0.75 of a
  window, the spin skip refusing a skip where it binds at either end (counted, as the runner's `ge contention:` line
  reports); the GE's reads counted where they read (VRAM's as other work); and the CPU's own stalls beside the busy GE
  in its time. Save-state layout 35 kept.
- **Why**: Super Monkey Ball's frame was −20.6% with #279, the owner's accepted exception limited at −21%; round 10c
  (part 114) put the rest in its strips' RAM vertex reads and the CPU slowed beside them. The owner's limit for
  Activision Hits' 3D menu: +10%.
- **The gate** (part 115's table; the PSP's timing): every judged frame within 15% (8 of 8, master's 7), Super Monkey
  Ball −20.6% to −12.7% and its exception out of `gate.py`, the 3D menu +6.7% to +9.95% (its idle capture +6.4% to
  +9.6%), the median miss 9.8% (master's 6.5%). With the GE on its thread the seven profiles are the same byte for byte.
- **Checks** (cachyOS, GCC 16.2.1; logs in `smba-refit/step4/`): on `b14ef87a0`, the release suite with the GE thread
  off 646 groups, 0 failures, on 646, 0, #289's step 110, 0, the measure groups sanitized with `ge-round10c` required
  14, 0 (`r4/suite-*.log`, `r4/measure-sanitized.log`, each naming its tree); the gate (`gate-{off,on}-b14ef87a0/`);
  "Fast" against master (`b7/`); #277's On = Off sets, 260 runs (`r4/4d/`); the host cost and H6 (`r4/4g/`); the
  tools README's recipe (`r4/p6/`); the tests' mutations (`step3/mut*.log`, `step4/*-mutation-fail.log`,
  `r4/r2l1/`). On `f7d27d438`, the independent review's two tests (logs in `smba-refit/step6/`): the release suite off
  646, 0, on 646, 0, #289's step 110, 0, the measure groups sanitized 14, 0; the review's mutations A and C fail the
  new tests, C with the GE thread off and on (`step6/mut/`). Part 115 has them all.
- **The coordinator's library check** on `b14ef87a0`: "GATE: CLEAN" at 19:42, 266 games each in Software and Vulkan
  fast, none newly failing, every exit line, none newly flat at frames 1,200 and 3,600, none more than 1.5x slower
  (`~/phobos-work/scratch/libcheck/lib-refit.log`, sha256
  `4ca24ff66f9983463d08c53c9a7aa9ea666225a586b218c4f2b32ff9315e76b8`). It stands for the head, which changes nothing
  the runner is built from (`ares/psp/`, `ares/ares/`, `tools/psp-runner/`, `tests/psp/ares/`, `nall/`, `libco/`,
  `thirdparty/`).
- **The RP6 run**, the coordinator's, 20 Oct 20:03-20:16 (`~/phobos-work/scratch/runner-android/RP6-CHECK-291.txt`,
  `logs/refit-rp6-table.txt`; the headless runner, Vulkan fast, the PSP's timing, two rounds taking turns), fps from
  master to this branch: Super Monkey Ball 141.38 to 151.44 (+7.1%), Killzone 165.10 to 163.13 (−1.2%), Burnout
  Dominator 112.11 to 102.50 (−8.6%), Need for Speed: Shift 123.95 to 113.62 (−8.3%), Burnout Legends 79.28 to 73.81
  (−6.9%); every run of this branch's with 0 windows capped and 0 skips refused. The owner's OK (20 Oct ~22:01):
  merge once the review passes; the polling skip's widening comes next.
- **Reviews so far**: the design (three rounds, PASS), the probe (PASS), the fit tool (PASS for `a2b5af99a`), the code
  by reading (PASS for `ee0e44309`, its one low finding fixed in `bb09cefc0`; round three PASS for `b14ef87a0`, 19:03,
  `REVIEW-r3.md`), the independent review (below); part 115 names their files.
- **Left** (part 115): Activision Hits' lists 2 and 3; the polling skip's widening; Killzone's per-frame Vulkan variance
  and its fuzz-ahead variance with the GE on its thread, both there before this branch; a probe of this head's own
  windows of the 3D menu.
  - The design's open Hypothesis that every RAM texel read costs a fixed time a line (only cache fetches and transfers
    were seen to behave so; streamed narrow reads are 56.5% of Super Monkey Ball's RAM texel clocks and 16.0% of the
    menu's first list's): the fit's menu +9.2% as it has them and +9.6% read the other way, Super Monkey Ball −12.6%
    and −11.9% (`step1/fit-real2/c12-real-final.txt`, lines 103-104), the menu's frame +9.952% against the owner's
    +10%. A round-11 case would settle it: narrow triangles streaming a RAM texture beside the streaming pattern at
    four rates (the design, r3).
  - No test catches a slip in the GE thread's own refetch charge (`ares/psp/ge/thread.cpp:764`), the low finding of the
    delta review (`~/phobos-work/scratch/review-291/REVIEW-r2.md`): its mutation D, which charges a refetch by the
    memory of the word just taken rather than by the mark's kind, passes CI's GE thread step, the release suite with the
    thread On and the gate. Today's code is right by reading; a one-way JUMP in a threaded test (a list in RAM that
    jumps into VRAM and ends there) would catch it.
    Done since: the spin tests' entry (above), whose group "ge time, the PSP's, with the GE on its thread: a list going
    one way into VRAM, its refetch where the list goes on" fails with mutation D in the release suite, CI's PSP-timing
    step and its GE-thread step.
- Independent review: PASS on `b753b3b46`, with runs (`~/phobos-work/scratch/review-291/REVIEW.md`); four low findings.
  Taken after it: L1 and L2's tests (`f7d27d438`); L4 and section 6's suggestions 2 to 5 in the docs.
  Round two passed the whole PR at `0e4fa0a80` (`REVIEW-r2.md`). With no macOS runner for that head in six attempts it
  merged on the owner's OK on `b753b3b46`'s and `c9a2f7727`'s green macOS builds; the merge's own passed on attempt 2.

## N64: the rasterizer Qualcomm's stock driver rejects, with its stored spec-constant values computed — 2026-10-20

Branch `cursor/n64-qcom-rasterizer-cachy`, from master `1257da75d` (#290). `rdp_device.cpp`, the new generated
`shaders/rasterizer_patched.hpp`, `tools/parallel-rdp/` (its script and CI check),
`.github/workflows/n64-rasterizer.yml` and this file; `slangmosh.hpp` unchanged. Notes and logs:
`~/phobos-work/scratch/n64-adreno/` (`FINDINGS.md`: the trace in sections 1-2, the RP6 probes E1 and E2 in sections 8
and 9.8, this round in section 10; its files in `r3/`, its commands in `COMMANDS.txt` section 8).
- **The fault**: on the RP6's stock driver (Adreno 740, driver 0676.53, compiler E031.41.03.47) N64 games run with
  sound but a black picture under "GPU Driver Issue Detected". Every pipeline made from parallel-rdp's rasterizer
  fails to compile (the driver's "Shader compilation failed", `VK_ERROR_UNKNOWN`): the generic one and each
  specialization, 34 in Mario Tennis, 41-42 in Ocarina of Time. Every draw goes through the rasterizer, clears
  included, so nothing reaches the frame buffer; each failure is one "Failed to create … blacklisted" line, which the
  app counts for the dialog. The module is the bank's SMALL_TYPES=0 rasterizer: parallel-rdp turns 8/16-bit
  arithmetic off on Qualcomm's proprietary driver (`rdp_renderer.cpp:204-208`).
- **What it rejects** (a standalone probe making the app's pipelines from variants of the module, run on the RP6 by
  the coordinator, E1 and E2): `shade_pixel`'s 24 stores of `OpSpecConstantOp` results (the static state flags, the
  dither mode, the 16 combiner inputs, the texture formats and sizes, in its `RASTERIZER_SPEC_CONSTANT` blocks), even
  where specialization leaves them dead. Those values computed by ordinary instructions (S2): it compiles in the
  generic and fill pipelines (485.9 and 281.3 ms). Only the 4 spec-constant branches rewritten (S3), or each stored
  value through `OpCopyObject` (S8): it fails. A plain spec constant stored (S7): it compiles. The combiner's 16 stores
  are the only group that fails alone. The ubershader compiles there too (862.6 ms).
- **The change**: `tools/parallel-rdp/patch_rasterizer.py` (Python 3, standard library) reads `slangmosh.hpp`,
  refuses unless its SMALL_TYPES=0 rasterizer has the SHA-256 the patch was made for (`2787b409…`), rewrites those 24
  stores and nothing else, and writes `rasterizer_patched.hpp` beside it (the module, `e3b620ef…`, and the offset and
  size of the bank's resource layout record for it); `--check` re-derives the file and compares it byte for byte. The
  module is byte for byte E2's S2, the one the RP6 compiled. `CommandProcessor::init_renderer` puts it in the bank's
  place, with that layout, whenever the bank chose the SMALL_TYPES=0 rasterizer (`resolver("rasterizer",
  "SMALL_TYPES") == 0`, the bank's own test), right after the bank is built and before the renderer and the VI get it.
  Pipelines are made at first dispatch from `shader_bank->rasterizer` (`rdp_renderer.cpp:1779`, its only use) and the
  N64's pipeline cache is a driver blob that replays nothing, so no pipeline is made from the original. No driver
  check.
- **Exact**: SPIR-V 1.6 rev. 8, 2.12: "The OpSpecConstantOp instruction is specialized by executing the operation and
  replacing the instruction with the result." The rewrite executes the same operation as the ordinary instruction on
  the same operands, each `OpSpecConstantOp` operand computed the same way, down to the specialization constants, so
  every specialization stores the same bits.
- **Which devices**: every device on which parallel-rdp chooses SMALL_TYPES=0, which is more than Qualcomm's: its
  driver rules (`rdp_renderer.cpp:180-211`) turn small types off on AMD's proprietary and open-source drivers (RADV
  too), NVIDIA's proprietary driver and Intel's Windows driver as well, and a device lacking `shaderInt8` or
  `shaderInt16` takes SMALL_TYPES=0 too (`PARALLEL_RDP_SMALL_TYPES=1` sets the driver rules aside). So AMD and NVIDIA
  desktops run the patched module by default; here RADV with no variable did, every frame the same as master's. The
  render passes' GPU time per frame (parallel-rdp's timestamps, two alternated runs of each build): Mario Tennis 0.261
  and 0.165 ms with master, 0.162 and 0.166 with this; Super Mario 64 0.149 and 0.149, 0.101 and 0.124: no slowdown
  seen (a shared machine, the GPU's clocks not pinned; not a benchmark).
- **The CI job** (`N64 Rasterizer`, its own workflow; paths: the parallel-rdp shaders folder, `tools/parallel-rdp`,
  itself; SPIRV-Tools 2025.1 from ubuntu-24.04's apt, its version printed): `patch_rasterizer.py --check`, then
  `check_rasterizer.py`, reading both modules from the headers on its own: `spirv-val` (Vulkan 1.1 and 1.3); the
  structure, the proof for every specialization: the two modules side by side, each id paired, each of the 24 stores
  of an `OpSpecConstantOp` result in `shade_pixel` preceded by instructions that mirror its tree node for node
  (opcodes, result types, literals, the same `OpSpecConstant` and `OpConstant` leaves) and store its root, every
  other instruction the same, none of those stores left. Then, as cross-checks, both modules specialized alike for
  the app's generic (spec ids 0-2) and fill (0-7) pipelines, 4 random specializations (values from all 32 bits,
  passed in hex and read back: spirv-opt leaves an `int` it can't read, a decimal above 2^31 - 1, at its default,
  without an error) and one for which a single `-O` leaves an extra constant: the 24 stores store the same
  constants, and `-O` followed by cleanup rounds until nothing changes gives the same program (the rounds:
  `--unify-const --eliminate-dead-const --eliminate-dead-code-aggressive -O`). One store left as shipped, a
  changed upstream module (one constant), a wrong rewrite (one `OpBitwiseAnd` made `OpBitwiseOr`), a missing `& 255`
  and a wrong leaf (SpecId 6 for 5) each fail both commands, the structure check naming the store at fault, or for the
  changed module its changed constant (instruction 1665, `OpConstant 254` against `255`).
- **Checks** (cachyOS, GCC 16.2.1; an RX 9070 XT with RADV, Mesa 26.3.0-devel; lavapipe): the Linux desktop Release
  build as desktop.yml's preset makes it (FFmpeg off, 2 jobs): exit 0, 510 steps, its only warning SDL3's;
  `rdp_device.cpp` with `-Wall -Wextra`, master's and this: the same 15 warnings, none in the added lines. `--check`
  passes; the header's module is byte for byte E2's S2 (`cmp`). The CI job's commands pass with SPIRV-Tools 2025.1
  (Ubuntu 24.04's package, unpacked) and 2026.4; the five broken inputs fail both. Pictures, from master's and this
  branch's desktop builds with the same scratch instrumentation (a picture every 60 emulated frames to frame 1800, no
  input; a stderr line naming the bank's rasterizer by Granite's hash, checked after each run; ares's "Deterministic
  Entropy", without which runs differ from one another: ares seeds the power-on state from the host, Mario Tennis's
  intro takes one of two courses and Super Mario 64's eight runs all differ): Mario Tennis, Super Mario 64, Mario
  Kart 64, Ocarina of Time and Mario Golf on RADV with PARALLEL_RDP_SMALL_TYPES=0, master against this twice and each
  build against itself, the SMALL_TYPES=1 control the same way, no variable once, and on lavapipe once: the same
  pixels in every frame compared (25 to 30 a run). `git diff --check`, `git show --check`, no changed line over 120
  columns; actionlint: no findings.
- **The RP6** (the coordinator's check, 21 Oct 08:20-08:29; `~/phobos-work/scratch/n64-adreno/rp6/RP6-CHECK-292.txt`
  and `compare-292.txt`): the Retroid Pocket 6 on Qualcomm's stock driver (Adreno 740, 0676.53), the app from boot
  with the N64 recompiler on, 55 s a game, its logcat and screenshots at 5 to 50 s. First the installed 215987, the
  control (#290's own APK, master `1257da75d`'s tree), then this PR's own APK 215990 over it (CI run 39520621527's
  Modern APK, built from GitHub's merge `c297f3766` of `3ed0a6731` into `1257da75d`). Mario Tennis, Ocarina of Time,
  Paper Mario and F-Zero X, the control against this: 34, 41, 32 and 24 "blacklisted" lines against none, and 34,
  42, 32 and 23 "Shader compilation failed" against none; from 10 s on, the control's pictures are the "GPU Driver
  Issue Detected" dialog over black, this PR's the games (Mario Tennis's intro, Ocarina's Hyrule Field and title,
  Paper Mario's logos and stage intro, F-Zero X's title and race demo), with no dialog. From the 10th second, 59.9
  fps median in all eight runs; AvgFrameTime medians 8.83 and 8.56 ms, 7.24 and 7.53, 5.79 and 5.65, 8.04 and 8.39;
  Phobos running at 55 s in all eight; the saves and states unchanged (md5). One observation: the frames over 20 ms, the
  control's and then this PR's, were 27 and 57 in Mario Tennis's intro, 2 and 2 in Ocarina, 3 and 2 in Paper Mario and 7
  and 3 in F-Zero X, the median rate still 59.9 fps; the likely cause of Mario Tennis's extra ones, untested, is the
  patched pipelines compiling on first use (in E2, about 0.5 s for the generic pipeline and 0.28 s for a fill state)
  where the control's failures took about 6 ms each. Not covered: an upscaled N64 setting; other Qualcomm drivers or
  devices. The result stands for this head: `rdp_device.cpp` and `rasterizer_patched.hpp` are `3ed0a6731`'s byte for
  byte.
- **Left**: the patched module on AMD's and NVIDIA's proprietary drivers and Intel's Windows driver (not here);
  whether Mario Tennis's extra slow frames on the RP6 are the patched pipelines' first compiles (untested).
  Follow-ups: Granite's pipeline blacklist, a Phobos
  addition, writes its function-static set under a read lock and doesn't log the `VkResult` (`command_buffer.cpp:1140`,
  `1547`); the driver dialog's wording; offering the fix upstream (the owner's call); the review's suggestions 3 and
  5, which change `rdp_device.cpp`: the hook's comment naming every SMALL_TYPES=0 device, not only Qualcomm's, and an
  environment switch to skip the patch, to compare the two modules on a device without a rebuild.
- Independent review: PASS on `3ed0a6731`, read-only (`~/phobos-work/scratch/review-292/REVIEW.md`); suggestions only.
  Taken after it: Low 1 and Low 2 (per-store structure, random values from all 32 bits, cleanup rounds) and the nit.
  Round two passed the whole PR at `c9a2f7727` (`REVIEW-r2.md`); its two wording nits went into #293.

## N64: ares's dialect for the upstream systems' code, and the RSP's recompiler against its interpreter — 2026-10-20

Branch `cursor/n64-ares-dialect-cachy`, from master `0f7c17d8a` (#289). `CMakeLists.txt`, `tests/rsp-recompiler/`,
`.github/workflows/desktop.yml` and this file; no core code changes. Notes and logs:
`~/phobos-work/scratch/n64-mario-tennis/` (`FINDINGS.md`, rounds two to four in `round2/` to `round4/`).
- **The fault** (FINDINGS.md): with the recompiler on, Mario Tennis's 3D intro, Super Mario 64, Mario Kart 64, Ocarina
  of Time and Mario Golf rendered corrupt on x86-64. The RSP's recompiler calls the out-of-line `RSP::VMOV<e>` and
  `RSP::VMULQ<e>`, which read vector lanes through a pointer cast from the `r128` union (`u16()`, `element()`); GCC
  compiled VMOV's read before the store it reads (stale stack bytes in every recompiled VMOV's lane) and VMULQ's from
  a register never loaded. The interpreter inlines its own copies, compiled right. ares's own build compiles its code
  with `-fwrapv -fno-strict-aliasing`, with GCC and Clang alike (cmake/common/compiler_common.cmake and the
  per-platform cmake/*/compilerconfig.cmake), which this CMakeLists.txt never included. Every GCC run, 11.4 (CI's Linux
  job's), 12.4, 13.3, 14.4, 15.2 and trunk, gets FINDINGS.md's standalone copy 128 of 128 wrong at -O3 (Compiler
  Explorer), and none with the two flags; Clang none.
- **The change**: every source under ares/ but ares/ares (shared with the PSP core) and ares/psp gets `-fwrapv
  -fno-strict-aliasing` with GCC, Clang and AppleClang: upstream's dialect flags, not its warnings. Its `-fno-char8_t`
  is left out: it only retypes `u8""` literals, and the 90 units the dialect applies to and the 2,028 files of ares/,
  nall/ and thirdparty/ they read have none. compile_commands.json of the Linux desktop build (GCC) before and after:
  89 of 462 units changed, each by those two flags only (the 18 upstream systems, PCE's vdp-performance, the N64's
  vulkan.cpp and 36 parallel-rdp units, 33 components), the test's added, none removed; none of the 24 units the PSP
  core links from (ares/psp, ares/ares's node and android_globals, nall, libco, sljit, libchdr) changed. With Clang 23
  (configured, not built) the same units carry both flags and the PSP's neither. In the linked Release binary all 16
  `RSP::VMOV<e>` store the shuffled vector before they read the lane; the desktop build has no LTO. The PSP core's
  code in the Linux desktop link (GNU ld) comes from those 24 objects, none changed, and every inline-function copy
  psp.cpp.o's lost went to the desktop's own objects, none to a changed unit (the review's check 4). Not traced: the
  Android link (lld, ThinLTO) and the macOS link, where the linker may keep a changed unit's copy of an inline function
  the PSP shares (nall, ares-core, std): the same source compiled without the two assumptions the flags remove, so the
  PSP's results could differ only where that code has undefined behaviour. The coordinator's RP6 check runs a PSP
  scene on the PR's APK beside master's nightly.
- **The test**: `tests/rsp-recompiler` (target `rsp-recompiler`, on phobos_core's own objects, its source in ares's
  dialect too): VMOV for every e and de with Vd and Vt apart and the same, every COP2 function, MFC2/CFC2/MTC2/CTC2,
  LBV-LTV and SBV-STV with the base known to the recompiler and not; 34,816 cases from seeded random states,
  recompiled against interpreted, the VU registers, ACCH/ACCM/ACCL, VCOH/VCOL/VCCH/VCCL/VCE, DIVIN/DIVOUT/DIVDP, the
  GPRs and DMEM compared, not the PC, the branch state, the status or the cycle counts (a scratch copy comparing
  `pipeline.clocksTotal` too: equal in all 34,816 cases), the stack filled with a pattern first; a group with
  differences ends with a line for each differing instruction (its mnemonic, or an invalid COP2 function's number) and
  its count. Linked with master's N64 object (GCC 16.2.1): 4,608 differ, the VMOV group's 4,096 of 4,096 (each lane
  `a5a5`, the pattern) and the COP2 functions group's 512: VMOV's 256 of 256 and VMULQ's 256 of 256, no other
  instruction; with the change: 0, in 0.35 s. desktop.yml's Linux job builds and runs it after the build, first
  checking `ninja -t commands`, each condition failing the step on its own with an error and up to 10 of the sources
  at fault: N64 commands found, each with `-fwrapv` and with `-fno-strict-aliasing` (counted apart), neither undone by
  a later `-fno-wrapv` or `-fstrict-aliasing`; PSP commands found, none with either flag. Here 38 N64 commands with
  both and the PSP's 1 with neither; on master's commands the step fails ("38 of 38 N64 compile commands lack
  -fwrapv"). 34dfeb7fa's check, one `&&` list under `bash -e`, could fail the step only on its last (the PSP's) test.
- **Checks** (cachyOS, GCC 16.2.1; the Linux desktop build as desktop.yml's preset makes it, Ninja Release, but FFmpeg
  off: its script builds with every CPU, against this machine's two-job rule). On a scratch copy with the change, the
  recompiler on against the interpreter at the same frames (a picture every 60 to frame 1800): Super Mario 64 2.3,
  Mario Kart 64 2.0, Ocarina of Time 0.2, Mario Golf 2.0, none above 20; Mario Tennis 7.2, 2 of 29 above 20 (frames
  1140 and 1200): the logo board a few pixels apart as the camera pans, both pictures right (the RSP's recompiler alone
  gives the same two; the recompilers count cycles a little differently in some of a game's blocks of several
  instructions (the checker below: about 3% of Mario Tennis's), while the test's cases, one instruction under test each,
  count the same). FINDINGS.md's differential checker on that
  copy: 0 differences in 40,000,000 Mario Tennis blocks and 56,000,000 Super Mario 64 blocks. Speed, the recompiler
  on: 59.9 FPS in every run; with the same work on both sides (round one's recompiler-only fix against this) the
  emulation per frame is 4.16 and 4.09 ms (Mario Tennis), 3.74 and 3.71 ms (Super Mario 64), within the runs' spread.
  actionlint: master's two desktop.yml warnings only. The dialect check's run: block as GitHub runs it (`bash
  --noprofile --norc -eo pipefail`, on the review's stand-ins for the build): exit 0 on this branch's commands and
  with the two flags the other way round; exit 1 on master's, with no N64 or no PSP command, with one N64 command
  lacking either flag or undoing one later, with a PSP command carrying either, and with a failing test. `git diff
  --check`.
- **Left**: the upstream code the PSP core and the runner share, still without the dialect (ares/ares, nall, libco,
  mia); the teardown crash (FINDINGS.md); an RP6 run (the coordinator's): with the NDK's Clang the N64's units now get
  `-fwrapv -fno-strict-aliasing` too, and the Android link wasn't traced for the PSP (above).
- Independent review (fresh session): NEEDS CHANGES for 34dfeb7fa; desktop.yml's N64 dialect check can't fail its step.
  Fixed after it: L1 in 35b6489f5; M1 (each condition fails the step) and L2 and L3 in the commit after it.
  Round two passed the whole PR at `43917e2ac` (`REVIEW-r2.md`); its two informational points went into #293.

## Tests and CI: the kernel's tests with the PSP's GE timing, and the skip's contention refusal tested — 2026-10-20

Branch `cursor/ci-psp-timing-cachy`, from master `dcf2f1b2c` (#287). Tests and CI only (`tests/psp/kernel-machine.hpp`,
`ge.cpp`, `modules.cpp`, `spins.cpp`, `.github/workflows/psp-core.yml`); the core doesn't change, so docs/psp-core.md
has no part for it. A follow-up from #269's review (`~/phobos-work/scratch/review-269/REVIEW-r3.md`, lines 123-128).
Notes and logs: `~/phobos-work/scratch/ci-psp-timing/` (`NOTES.md`).
- **The switch**: `PHOBOS_TESTS_GE_TIMING=PSP` (`testsPspTiming()`, tests/psp/kernel-machine.hpp) makes every kernel
  test machine that doesn't choose its GE timing start with the PSP's, the option "GE Timing"'s default since #269;
  unset, nothing changes. Machines made while a `FastTimed` lives stay on "Fast" (as an `Unthreaded` keeps the GE off
  its thread under `PHOBOS_TESTS_GE_THREAD`); tests that call `pspTiming()` keep it.
- **Twelve groups pin "Fast"'s numbers** and now say so with a `FastTimed`. With the switch on and no choice they failed
  73 checks, each a time or an order only "Fast" gives: ge time (`geDone` 0x2993 for 0x600: Sony's peaks), ge time's
  interrupt (its 294-295 µs), ge FINISH after a quiet function ("Fast"'s 12288 cycles inside the spin), ge profile (9
  records for 8: "Fast"'s one ModelWork record, 1536 cycles), ge time, the PSP's: the CPU's caches (its "Fast" machine
  counted 0x402 lines for none), ge callbacks, ge pause, ge suspending signals' callbacks and ge break (the list still
  drawing where each expects it done by the program's next call), ge endless list (a million commands at once, a
  SIGNAL taken as met), a spin skipped directly (`anchor()` sets the detector up as "Fast" runs it) and programs
  unloading themselves (the interpreter's state against the recompiler's: with the PSP's timing the CPU's caches,
  which a state holds, move in compiled code alone). None showed a fault in the PSP's timing.
- **CI**: the release job's step "Run the kernel's GE tests with the PSP's GE timing", after the GE thread's, on the
  same objects, with its own summary line: `PHOBOS_TESTS_ONLY` the spin groups and the GE's looks polled, every "ge "
  group (the GE thread's among them), the sample programs and the curves, 105 groups with the new test;
  `PHOBOS_TESTS_REQUIRE=vulkan, programs`. Left out for time: the six measured groups and three of the GPU's, which
  have a machine the switch moves too (a selection with them took 175 s here, against the step's 24). 26 s here; CI
  took 35 s on `a0dd68a27`, where the GE thread's step took 186 s (181 s on master's last run, PSP Core run 39439646839
  on `dcf2f1b2c`; 104 s here).
- **What the step catches** (gcov on a coverage build: `covbranch.txt`): it takes 18 branches the release lane (its
  suite and the GE thread's step) never takes. No mutation tried is caught by the step alone. (1) The spin skip's
  SIGNAL check dropped (ares/psp/kernel/spins.cpp:192): the step reaches it three times, in "the GE's looks polled
  behind a list, past a SIGNAL, with interrupts held off" (its SIGNAL case), each time the SIGNAL's time (8864) past
  the skip's end (7864 + 976): the CPU's go ends at that time (threads.cpp:296-297) and a skip stops SpinMargin turns
  short of the go's end (spins.cpp:179-181), so the check never refuses and the same skips are made without it. (4) A
  due SIGNAL taken with interrupts held off (ge.cpp:466): the step reaches that once, in "ge calls and threads"
  (tests/psp/ge.cpp:789-846), its SIGNAL reached (7143) by the end of sceGeListEnQueue (7880) while main holds
  interrupts off; taken there, its callback is a call into the program, which doesn't start while they're held off
  (interrupts.cpp:24-25), so the order the group checks comes out the same. (3) (1) with threads.cpp:297 dropped as
  well: caught by both, the step 6 failures, the release lane 71. (2) The skip's contention refusal dropped
  (spins.cpp:211): passed every lane (below).
- **The new test** for (2), "the GE's looks polled with the PSP's GE timing after a sleep: no skip across its busy
  window's kink" (spins.cpp, explicit `pspTiming()`, the recompiler): gcov of today's lanes found the refusal never
  made, its right-hand operand (`geContendBound(geWindow) != geContendBound(end)`) evaluated 40046 times and true none
  (566 and none in the step). Three lists of 16 RAM-textured sprites, each queued, then 256 fresh lines read, the
  shortest sleep (230 µs) and a poll (sceGeListSync) whose turns move no lines: the sleep leaves the GE's busy window's
  time the cycles passed, and the poll's turns make it the CPU's about 4440 turns in, the push the window owes peaking
  there (the first list's 15776 cycles: 14764 as the poll begins, 9340 had the turns to the step's end been one
  stretch). After every step the clock, `geDoneAt`, the window and the rest of the state are the same with the skip as
  without it; 1.46 million turns skipped, 3340 skips refused at :211 (a scratch probe's count). With (2) it fails at
  step 1: the GE done at 4515134 against 4516146, pushed 14764 against 15776. It passes with the GE on its thread (the
  GE thread's step selects it). A first version reading 16 lines failed (2) by 9 cycles; 256 make it about 1000.
- **The SIGNAL check with the GE on its thread** (reading only, no test): it can't change what a skip does. With runs
  pending `due()` (spins.cpp:187-192) compares `geReachBound()`, a lower bound (ge.cpp:396-404), and folds only where
  that lies within the skip; a SIGNAL still to come bounds the go (threads.cpp:298-302), which the skip makes exact
  before taking its room (spins.cpp:173-177; `geExactLimit()`, ge.cpp:447), so the skip ends before the SIGNAL's exact
  time. Only a SIGNAL already reached and not taken (interrupts held off) makes `due()` refuse, and then the turns
  skipped would change nothing: it can't be taken till they're back on.
- **Checks** (here: cachyOS, GCC 16.2.1; release builds with `SANITIZE=-Wno-error=array-bounds` for its false warning at
  tests/psp/movies.cpp:52, which CI's GCC 13 doesn't give): the release suite with the switch off 640 groups, 0
  failures (master's 639 names and results, and the new one), on 640, 0; the GE thread's step 60, 0 (105 s); the new
  step's selection 105, 0 (26 s); the new test sanitized 1, 0, no sanitizer report; actionlint (master's two
  desktop.yml warnings only); `git diff --check`. All at niceness 5 through `pinned.sh`, two compiles at once.
- **Not run** here: the rest of the sanitized suite, tests/allegrex, tests/psp/ares (CI's).
- Independent review: PASS on `a0dd68a27`, read-only (`~/phobos-work/scratch/review-289/REVIEW.md`), no file changes.
  Taken after it: section 6's suggestions 2, 5 and 6 (`a23c3e8e9`); round two passed it (`REVIEW-r2.md`).

## PSP measure: ge-terms.py applies to master again, its five places where #277 left them — tools only — 2026-10-20

Branch `cursor/ge-terms-master-cachy`, from master `16156ab9a` (#286), master `dcf2f1b2c` (#287) merged in
(`bc4e4521d`). `tools/psp-measure/ge-terms.py`, its README's recipe and this file; the core doesn't change.
- **Why**: since #277 (part 111) master's script stopped at its tenth edit on master ("0 places for '    pipe.vertices
  += format.indexFormat ? …'"): five of its 21 places had moved.
- **What**: the five follow the code, adding what they added: the vertices' terms beside `ramRead(0, work)` in
  `GE::oweVertices()`, JUMP's and CALL/RET's at `runList()`'s `front()`, the words' term and the R line in `geTime()`'s
  branch for the synchronous GE, the S line in `geOwe()`'s. Its patched copy is byte for byte what the Super Monkey
  Ball refit's step 1 copy of the script (made for `9f2d0633d`) gives. The docstring says the places are master's for
  the GE thread Off: with it On, `geTime()` and `geOwe()` take the thread's branches, so the file has no R or S lines
  (and, by reading, both threads add to the counts unguarded).
- **Checks** (cachyOS x86-64, GCC 16.2.1, Python 3.14.7; `~/phobos-work/scratch/ge-terms/COMMANDS.txt`): it applies
  to a `git archive` of `16156ab9a` and fails closed on a missing anchor; the patched runner builds with `build.sh`
  (which compiles `psp.cpp` with `-w`; under `-Wall -Wextra` the script's added lines add no warning); Super Monkey
  Ball and Chains of Olympus from boot, 2,400 frames, the PSP's timing: master's runner and the patched one without
  and with PHOBOS_GE_TERMS give the same PNGs, state, WAV, output and report (but for the runner's source and the
  host's timings), and the terms files are step 1's byte for byte; the README's recipe ran (its folder made first, as
  `tar -x -C` needs; round 10c's built `EBOOT.PBP` for `make`: no pspdev here), and `ge-fit.py` gives `GE::Cost` back
  to its figures (61 terms of 61).
- Independent review: PASS on `c1673609b`, read-only (`~/phobos-work/scratch/review-288/REVIEW.md`); docs findings only.
  Taken after it: L1, L2 and L3, and section 6's suggestion 1.

## CI: the sanitized PSP suite keeps the Vulkan driver loaded, so LeakSanitizer sees what lavapipe holds — 2026-10-20

Branch `cursor/ci-lsan-mesa-cachy`, from master `b398e86f7` (#285), master `16156ab9a` (#286) merged in (`74b58eef2`).
Tests and CI only (`tests/psp/run-tests.sh`); the core doesn't change, so docs/psp-core.md has no part for it. Notes
and logs: `~/phobos-work/scratch/ci-lsan/`.
- **Why**: on GitHub's ubuntu-24.04 image 20261018.339.1 the sanitized "PSP system tests" job failed at exit, after
  every group passed, on a LeakSanitizer report from frames in an unloaded module: 240 bytes in 3 allocations (a 128
  and two 56s), or the two 56s alone. 5 of 5 that finished there without the fix (`b26e0cebe`, #279's `69ea2650d`,
  master's `9f2d0633d`, #286's `0954b8800`, and master's `16156ab9a` since; a sixth, `9bcaff348`'s, was cancelled
  mid-suite); none in the 156 that finished on 20261004.327.1 and 20261012.334.1 since 15 Oct (#279's re-run among
  them), nor in #286's re-run there. The only difference in the images' software lists that the job could use is the
  kernel (6.17.0-1022-azure to 7.0.0-1017-azure); GCC (the FFmpeg build's name, which has the compiler's identity in
  it, is the same), Clang, glibc 2.39-0ubuntu8.9, Mesa 25.2.8-0ubuntu0.24.04.4 and the loader 1.3.275 are the same.
- **The blocks are lavapipe's own**, CI's frames named with Ubuntu's debug file for that build (frames #1 and #2 land
  exactly on the calls of `realloc` in `get_cpu_topology` and of it in its caller, which fixes the base). The 128
  bytes: Mesa's L3 cache masks (src/util/u_cpu_detect.c:566), made once through `call_once` from
  `llvmpipe_create_screen` in `vkEnumeratePhysicalDevices`, on Zen CPUs only (family 0x17 and up), kept in its
  static `util_cpu_caps`. The two 56s: `u_mmInit`'s two blocks, each linked to the other (u_mm.c:77 and 81), the
  heap of the x86 code `translate_sse` makes (`rtasm_exec_malloc`, its static `exec_heap`), made when it first makes
  some (in CI, at a draw in lavapipe's queue thread). Mesa frees neither; only lavapipe's static variables hold
  them, and those go when the loader closes lavapipe at `vkDestroyInstance`, before the check at exit. Why the
  images before never reported them is not known (the 128 bytes need a Zen runner, the 56s don't); lavapipe has no
  NODELETE flag of its own and no thread-exit destructors, but glibc can still keep it loaded. A lead, a hypothesis
  untested on CI: Ubuntu's lavapipe, asahi and nouveau drivers define the same five `STB_GNU_UNIQUE` symbols, glibc
  keeps whichever of them loads first (NODELETE), and loader 1.3.275 opens `icd.d` in directory order, which a new
  image can change (review-287, section 3).
- **Reproduced here** (cachyOS, kernel 7.2.9, Ryzen 7 7800X3D, Mesa 26.3-devel's lavapipe, loader 1.4.363, GCC
  16.2.1): the sanitized GPU groups (`PHOBOS_TESTS_ONLY=gpu`, lavapipe alone through `VK_ICD_FILENAMES`) leak the
  same 240 bytes in 3 allocations. A 40-line C program (the loader opened, an instance, its GPUs enumerated, the
  instance destroyed, the loader closed) leaks the 128 bytes: still with the loader kept open or Mesa's device-select
  layer off, not with lavapipe kept open, and again with that and `use_globals=0` (a global holds them).
- **What**: `run-tests.sh` sets `VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1` (the caller's value kept; only `1`
  turns it on, so a caller's `0` or empty value turns the fix off) when the address or leak sanitizer is on: the
  loader (1.3.260 and later) then keeps drivers and layers open, so their static variables are still LeakSanitizer's
  roots at exit. The release lane doesn't change. No suppressions file: frames in an unloaded module have no name for
  a pattern to match, the queue thread's pair has no frame of Phobos's, and a pattern on `VulkanBackend::create`
  would hide that function's own leaks too.
- **What it can hide**: only what a Vulkan driver or layer still holds from its static variables at exit, which isn't
  a leak by LeakSanitizer's measure; the libraries the kept drivers load stay too (lavapipe's LLVM, for one), so
  their static variables are roots as well. A block of Phobos's is hidden only if a driver kept a pointer to it there
  (Phobos gives Vulkan no allocator or user data). A Vulkan object Phobos fails to destroy is reported as before (the
  driver's frames named now), unless the driver itself keeps it in a static variable. The sanitized lane no longer
  exercises unloading a driver; the release lane still does.
- **Checks** (here; logs in `~/phobos-work/scratch/ci-lsan/logs`): the GPU groups before, 240 bytes leaked, exit 1;
  after, "78 groups, 0 failures, 1 skipped" (CI's skip), no report, exit 0. A scratch copy's deliberate leaks (never
  committed: `new char[1234]` in `VulkanBackend::create`, `new char[4321]` in "memory windows"), with the fix: both
  reported (11 of the 1234 at vulkan.cpp:2237, the 4321 at memory.cpp:8), nothing of lavapipe's; the same build with
  `VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=0` from the caller: lavapipe's 240 bytes back beside them. `bash -n`
  and `git diff --check` clean; shellcheck's two warnings are master's.
- **CI on 20261018.339.1**: with the fix, `74b58eef2`'s sanitized job 118629925990 passed there, "639 groups, 0
  failures, 1 skipped", no LeakSanitizer line; master `16156ab9a` without it failed there (job 118623513419, 240
  bytes in 3 allocations).
- **Not run** here: the rest of the sanitized suite, the release lane, tests/allegrex, tests/psp/ares (CI's).
- Independent review: PASS on `9b18548cc`, read-only (`~/phobos-work/scratch/review-287/REVIEW.md`), wording nits only.
  Taken after it: L1, L2 and section 6's points on the switch's values, kept drivers' libraries and unloading.

## CI: every workflow's checkout step off Node 20 — 2026-10-20

Branch `cursor/ci-checkout-node24-cachy`, from master `b398e86f7` (#285). CI only, a follow-up from #283's review
(`~/phobos-work/scratch/review-283/REVIEW.md`, its question 3).
- **What and why**: the 17 `actions/checkout@v4` steps in `.github/workflows/` use `actions/checkout@v7` now, pinned
  by major tag as before (android.yml 2, desktop.yml 3, ngcd.yml 1, psp-core.yml 8, psp-ge-profile.yml 1,
  psp-shaders.yml 1, rsp-neon.yml 1); nothing else in the workflows changed. v4's action.yml declares `node20`, which
  GitHub is retiring, so every run carried its deprecation warning. Seven other actions the workflows use still
  declare `node20`; they are left as they are, and the pull request lists them as a possible follow-up.
- **Why v7** (v7.0.1, the newest major): its action.yml declares `node24` and is otherwise v4's byte for byte, so the
  options the repo passes (`submodules: recursive`, `fetch-depth: 0`, `filter: blob:none`) mean what they did. The
  majors' breaking changes miss this repo: v5 needs runner 2.327.1 or later (every job is on GitHub's hosted
  runners); v6 keeps the token in a file under `$RUNNER_TEMP` that `.git/config` includes, not in `.git/config`
  itself: git on the runner still gets it, and only a Docker container action would need runner 2.329.0 for it (none
  here; the repository and its one submodule are public); v7 refuses fork pull requests' code under
  `pull_request_target` and `workflow_run` (neither is used, and v4.4.0, what v4 is now, refuses it too). The one
  change the repo will see is on a tag push (the review below, section 4.3): v7 keeps the pushed annotated tag
  annotated, where v4 fetched it by commit and made it lightweight (so build.txt's `git describe` gave
  `v2.3.0-208-ga8d5640f` on v2.4.0's push, not `v2.4.0`), and checkout now fails if the tag moved or was deleted
  after its push, where v4 built the pushed commit. The pull request's body has the evidence.
- **Checks**: actionlint 1.7.12 with shellcheck 0.11.0 on the seven workflows: only the two warnings master has
  (desktop.yml:189 SC2046, desktop.yml:249 SC2034); `git diff --check` and `git show --check` clean; the diff is the
  seven workflows and this file; no `actions/checkout@v4` left in `.github`. CI's runs go in the pull request's body.
- Independent review: PASS on `0954b8800`, read-only (`~/phobos-work/scratch/review-286/REVIEW.md`); no file changes.
  Its suggestion S1, the tag-push clause in "Why v7", was added after it.

## PSP GE: #277's entry after its review and library check; the GE profile's decoder files — docs only — 2026-10-20

Branch `cursor/docs-277-followups-cachy`, from master `9f2d0633d` (#277, merged at `331eed9a9`). Docs only.
- **#277's entry, below**: its last bullet still named the fix round's review and the coordinator's library check as
  next. It gives their results now (the review: PASS on `fee55ee6d`, and on `331eed9a9` with master's #284 merged in;
  the library check of `fee55ee6d`: 266 games, Off the same as master and On the same as Off), CI's on `331eed9a9`
  and the merge, and keeps the rest of next.
- **tools/psp-ge-profile/README.md**: its list at the top of what's in the folder had only `plugin/` and
  `decoder/geprof.py`; it names `decoder/gate.py` (the GE timing's gate), `decoder/test_gate.py` and
  `decoder/test_geprof.py` now, as #283's review suggested (`~/phobos-work/scratch/review-283/REVIEW.md`, its
  suggestion 3), and the "Files" table near its end gains `decoder/gate.py` and `decoder/test_gate.py` too.
- Independent review: PASS on `140e156a2`, read-only (`~/phobos-work/scratch/review-285/REVIEW.md`), no changes asked.
  Its wording suggestion (S1, the README bullet's credit to #283's review) is taken after it.

## PSP GE thread, its second part: the frame shown from the GE thread, the emulation thread's work for it cut, its two threads placed, and the PSP's GE timing with it, exact — 2026-10-19

Branch `cursor/psp-ge-thread2-2b67` (GE round 4, part B, second PR; draft PR #277), from master `d428d91f9` (#276) with
`08801ab60` (#269, the PSP's GE timing the default, and #278 with it) merged in, then master `ba1a19896` (#279, part
113, the CPU's caches and the CPU beside the GE, merged as `f5cf0c6b5`; #282); docs/psp-core.md, part 111, has the
evidence. Clean room: our code, uOFW for the GE driver's behaviour; no PPSSPP or JPCSP. Save states: layout 35, as #279
left it (with the thread On, the GE's timing state is the render GE's, drained first: the same file on or off).
- **What**: with "GE Thread" On the frame is shown from the GE thread, which takes each frame's picture at the frame's
  record, at most two frames in flight (the screen a frame behind: about 17 ms more between a press and its picture at
  60 frames a second); the window is handed over through it; saving and loading wait for it at most 2 s, and an unload
  with it stuck leaves it be (design steps 3a, 3c, 3d). The app's "PSP Graphics Overlap" (Off, On; Restart to Apply)
  is built and not offered till On beats Off on the RP6.
- **The emulation thread's work for the thread, cut**: VRAM vertices copied whole, a PRIM's texture worked out once,
  a target's run of PRIMs one model rectangle, a PRIM's record taking the words before it and its copies made in
  place, the CPU's stores into VRAM as one record (Hot Wheels: 59,000 a frame), the value rule's page checks kept
  clean. Its instructions a frame with the thread On ("Fast"): Chains of Olympus 92M to 50M, Ghost of Sparta 222M to
  118M, Super Monkey Ball 303M to 238M, Army of Two 44M to 36M, Killzone 149M to 74M, Hot Wheels 258M to 206M.
- **The two threads placed** (the app's "PSP thread placement", off by default and not offered; the runner's
  `--ge-place`): the GE thread on the fastest core the app may use, the emulation thread on the next, by the kernel's
  capacities among the cpuset's cores, asked for again every frame: 7 and 3 on the RP6, as fast as the best placement
  pinned by hand (with "Fast": Killzone 1.02x, Army of Two 0.96x, Ghost of Sparta 0.92x, Chains of Olympus 0.87x,
  Disaster Report 3 0.83x, Super Monkey Ball 0.76x; unplaced 0.60x to 0.89x).
- **The PSP's GE timing with the thread** (#269 refused it): the GE thread keeps the GE's pipeline in its clocks (a
  list's start, each run's shift, its words as `run()` counts them, the control pass's own work as marks after their
  words, its primitives, transfers, palettes and copied vertices) and hands back each run's two times; the emulation
  thread keeps the times in cycles and folds each run's as the synchronous GE ends it. Where a time decides something
  (a FINISH, a SIGNAL, the go's limits, `sceGeSaveContext`, a spin's skip), the last run's bound answers first (its
  pipeline's two ends so far, or half its words' work), and the emulation thread waits for the GE thread only where it
  doesn't. #279's CPU-beside-the-GE term is carried exactly: each stretch kept with its instructions, the caches'
  counts as it ended, whether a SIGNAL was due and the CPU's clock (since the port's review), no more than 65,536 of
  them waiting, and taken as the fold reaches it, a run's start folding first while stretches wait (the term's push
  moves the pipeline's start), a cut go counted as one, spin skips as long as the synchronous go's (deciding from the
  busy window, folded first).
- **Speed with the PSP's timing and #279** (the M1, Vulkan fast, 600 frames, five rounds, medians, `a335937a9`): On/Off
  Ghost of Sparta 1.25x, Chains of Olympus 0.98x, Super Monkey Ball 1.16x, Army of Two 1.23x, Killzone 1.00x, Disaster
  Report 3 1.40x, Hot Wheels 1.21x; before #279 1.15x, 0.99x, 1.08x, 1.10x, 1.01x, 1.38x, 1.09x (0.78x with "Fast": with
  the PSP's timing the emulation thread reaches a FINISH's time after the GE thread has counted its runs). The runs'
  starts that fold first while stretches wait: Super Monkey Ball 0.9 a frame, Army of Two 0.5, the others none. The RP6
  with the PSP's timing, before #279, placed (the GE thread on core 7, the emulation thread on 3): Ghost of Sparta
  0.95x, Chains of Olympus 0.87x, Super Monkey Ball 0.91x, Army of Two 1.03x, Killzone 0.86x, Disaster Report 3 0.99x,
  Hot Wheels 0.82x (unplaced 0.28x to 0.94x): where the GE thread's time a frame is about the emulation thread's whole
  time Off there's little to overlap, and the emulation thread on the A715 saves little.
- **Off costs nothing**: the emulation thread's instructions a frame with the thread Off against master `ba1a19896`
  (#279 in), the PSP's timing: -0.45% to +0.21%, the settled hashes the same (the gate's scenes, both renderers, 12 of
  12); before #279, against master `08801ab60`: -0.04% to -0.15%, and -0.15% to -0.26% with "Fast".
- **Exact on #279** (`a335937a9`, master with #279 in, and before it `0ab56c774` and `bee76daa1`; its term live in every
  run; the thread On with the checks against Off, the PSP's timing): the gate's scenes (14 of 14), section 9's sets (240
  of 240), "Fast" (3 of 3), the slow three from their start (6 of 6); the suite Off, 633 groups, and the thread On lane,
  53 (#279's tests, measured rounds 10 and 10b and spin test threaded), 0 failures; the ares lane, 391 checks. A test of
  the CPU's stretches beside a list given in parts, On == Off, failing with each of six parts of the port taken out.
- **Exact, the thread On with the checks against Off, with the PSP's timing** (the gate's scenes, section 9's sets, the
  contention term, "Fast" and the slow three run again after the review's fix, `df88b37ee`: all the same): the gate's
  scenes in Vulkan fast and Software (14 of 14); part 109's section 9 sets (240 of 240: the 26 scenes' end hashes with
  states and shown frames, shown frames in Software, fuzzing three ways, Vulkan accurate and 2x, the 78 settled hashes,
  the stress mode at 1x, 2x and 4x); the contention term carried, with a scratch build that turns round 10's slopes on
  (12 of 12); "Fast" unchanged (C's runner gives the merge's hashes, Off and On); part 109's slow three from their
  start, the clock fixed, every frame and then RAM and the state, either timing (6 of 6); the suite Off and On, 625
  groups, 0 failures, the measure rounds now threaded with the PSP's timing; the ares lane, 391 checks; CI green.
  ThreadSanitizer, nothing suppressed, the GPU on MoltenVK: the threaded groups (103, the measured rounds and the GPU's
  among them) no report; the ares lane and a sanitized runner (five scenes, the thread On, the PSP's timing) none from
  C's code, two from elsewhere, fixed (below), then clean.
- **Slow three** (part 109's: each from its start, Software, 3,600 frames, Off and On): BlazBlue 2 s and 7 s, SSX on
  Tour 4 and 11, Aces of War 5 to 6 and 25 to 26, with either timing (part 109: 4 and 20, 8 and 43, 10 and 41).
- **Found by the independent review** (CHANGES, item 1): C lost the control pass's front-end work after a word-buffer
  flush. A bounding box whose vertices lie on a VRAM page with drawing pending hands its check to the GE thread as a
  record of its own, which takes the words waiting, so the box's vertices' work was marked after word -1 and never
  replayed, with every mark after it in that record (and a PRIM right after the box carried none): the GE thread's
  front end ended early, a SIGNAL's time earlier than with the thread off. Marks now count the words before them
  (none: before the next word), go in a record even with no words, and the checks abort on one not taken
  (`716171da0`; its test fails on `4ee2e5acf`, a SIGNAL's time up to 274 cycles early, and passes after). The nits
  (`df88b37ee`): `unwedge()` unhooks first; quitting with the GE thread
  stuck lets it go, no hang; the placement checks its kept thread ID each frame; CI has a thread-On lane.
- **Found and fixed on the way**: `listWord()`'s piece cache kept a 32-byte piece clean after checking one word; with
  the checks on the thread's model kept every passed put point (MX vs ATV crawled); under the contention term the first
  threaded runs differed (limits taking in later stretches, a cut go never ending its stretch, spin skips sized by a
  bound's shorter limit); the On lane's state layout checks set the kernel's GE's texel cache, not the render GE's.
  ThreadSanitizer: the screen's passthrough flag (`Screen::setPassthrough()`) set on the emulation thread without the
  lock its own thread reads it under (master's runner has it too; it takes the lock now), and the ares lane's save
  states check hashing VRAM while the drawing threads could still draw (it settles them first). The runner's sparse
  PNGs (the library check's) with the thread On showed the frame the screen showed a frame late: the frame before a
  PNG's gets its own picture now, so On's PNGs are Off's (the slow three, 24 of 24), and Off's are as they were.
- **On part 113** (#279 merged in at `8a0a04b7f`, its review's fixes at `0ab56c774`, master with it at `a335937a9`):
  #279's code as it is with the thread Off. With it On, a stretch the CPU ends while runs are pending keeps its
  instructions, the caches' four counts, whether a SIGNAL was due and the CPU's clock (`Kernel::GeStretch`), each run
  the counts as it ended (`GeRun::caches`); the list's clocks by kind come with each run (`GeThread::Owed`) into the
  kernel's GE's own, which draws nothing meanwhile, and a state saves the render GE's; `goThreaded()` counts a cut go's
  instructions across its parts (`Allegrex::run(limit, from)`) and ends its stretch once. #279's test machine
  `pspTiming()` keeps the GE thread as the machine has it, and the CI thread On lane runs the CPU beside the GE's
  groups. No layout of its own: 35.
- **The port's review** (NEEDS CHANGES on `b56fa5010`, four findings) **and its fixes** (`99e067692`, `5a29bcc1d`,
  `216a89cf9`, `d62faaaa0`): a stretch waiting on the GE thread's runs carries the CPU's clock it ended at (a clock set
  while stretches waited gave the thread On another time than Off: 6876 against 6958 µs in the review's program); no
  more than 65,536 stretches wait (past that the oldest are folded with their run, as runs are past 4,096: a stalled
  list beside a program making calls grew them without end); and four port items have a test each that fails without
  it (the threaded spin skip's fold, a stretch's SIGNAL flag, a state's clocks by kind, the exact limit's entries for
  stretches, that one white-box: a limit short of its time only ends a go early). Part 111 lists what a stretch's
  fold reads and the checks; ThreadSanitizer, per the review, found nothing in Phobos's code (its only reports came
  from inside the GPU drivers' own threads, RADV's and lavapipe's).
- **The fix round's review** (PASS on `fee55ee6d`, no findings; `~/phobos-work/scratch/review-277/REVIEW-port-r2.md`):
  the reviewer's clock probe gives 6,958 µs Off and On; taking out any of the four items, or the fix round's two (the
  clock a stretch carries, the cap's fold), fails a test; the cap holds and can't hang; the docs match. It re-ran six
  games from boot, On against Off and Off against master (6 of 6 each; "Fast" 3 of 3), Resistance's spin skip,
  threaded states, the release suite (639 groups), the thread On lane (59) and ThreadSanitizer (29 groups, no report).
  The look at the merge with master's #284 (`REVIEW-port-r3.md`): PASS on `331eed9a9`, the whole PR, no findings.
- **The library check** (the coordinator's, of `fee55ee6d` on the owner's Linux desktop;
  `~/phobos-work/scratch/libcheck/lib277.log`): 266 games from their start with the library's presses, 3,600 frames,
  pictures at 60, 300, 1,200 and 3,600, the PSP's GE timing, the clock fixed (`--start-time 1700000000`), Software and
  Vulkan fast on RADV. With the thread Off no game differs from master `d9bfbefb4` in either renderer, and with it On
  none differs from Off; none newly failing, every run's exit line there. (Master's emulation code was `d9bfbefb4`'s
  until #277; `331eed9a9` adds only #284's docs and tests to `fee55ee6d`.) The same check on `b56fa5010` was clean
  too. CI on `331eed9a9`: 14 checks passed, the 3 publish jobs skipped. Merged 2026-10-20 as `9f2d0633d`.
- **Next**: Auto with its handover both ways (design step 4), a stacked PR; the app's option once On beats Off on the
  RP6; Hot Wheels' remaining store cost; the busy window kept on the GE thread, so that a run's start needn't wait.

## PSP tests: the measured rounds' groups report a missing round, and a missing file no longer ends a sanitized run — 2026-10-20

Branch `cursor/psp-measured-skips-cachy`, from master `b48c0c68b` (#280's merge), master `f8f301347` (#283) merged
in: #280's review suggestion S4. Tests and their README only; the core doesn't change, so docs/psp-core.md has no
part for it.
- **Why**: #280's independent review (claim (b)) found that neither measured8's four groups (rounds 8, 9, 10 and 10b)
  nor "measured round 6 GE time" could report its skip. Each asked `ours()` for the core's file first and returned
  when it came back empty, which it does without a word when the round's variable is unset or empty or the PSP's file
  is missing; only `theirs()` reports. With the five rounds' variables empty and all five required, the review's run
  gave "11 groups, 6 failures": measured6's other six groups failed and these five passed. CI's exposure was small
  (`run-tests.sh` always unpacks the rounds and checks their sums).
- **Found on the way**: a missing PSP file ended a sanitized run before any group could say so. The tests' readers
  (`words()` in measured6.cpp and measured8.cpp, curves.cpp's round 3 `load`) gave `memcpy` an empty vector's null
  pointer for 0 bytes, which UBSan takes as undefined; with `-fno-sanitize-recover=all` the program stopped there
  ("measured round 6 lighting", measured6.cpp:24; "curves round 3", curves.cpp:373), never reaching the failure and
  the skip their code means for a missing file.
- **What**: the five ask `theirs()` first (measured8's `misses()` and `measuredContention10()`, measured6's
  `measuredTime()`), as five of measured6's other six groups do; the sixth, the driver statuses, asks `ours()` first
  but `theirs()` before it can return. `ours()`'s comment says why. The three readers copy only when there's
  something to copy. With the files there, every group reads and checks the same.
  `tests/psp/measurements/README.md` names `PSP_GE_ROUND10b` and `ge-round10b`, says what a missing round and a
  missing file do now, and that round 10c's files have neither a variable nor a requirement (no test reads them).
- **The other readers**, checked and left: measured6's other six groups (`theirs()` first; the driver statuses ask
  both); "curves round 3" (a skip without `PSP_GE_RESULTS` or one of its five files, now that its reader doesn't end
  the run); "psp measure" (a skip without `PSP_GE_RESULTS` or its `manifest3.txt`, a failure where required; a
  picture missing from a round 3 folder is listed, as differences are; its reader has no `memcpy`).
- **Test** (cachyOS x86-64, GCC 16.2.1, `PHOBOS_JOBS=2`; logs in `~/phobos-work/scratch/measured-skips/logs`): the CI
  measure groups (`PSP_FFMPEG=0 PHOBOS_TESTS_ONLY=measure PHOBOS_TESTS_REQUIRE=programs,ge-round3,…,ge-round10b
  tests/psp/run-tests.sh`), sanitized: 13 groups, 0 failures before and after, the output the same line for line;
  release (`SANITIZE=-Wno-error=array-bounds`, for GCC 16's false warning at movies.cpp:52, not fixed here): 13
  groups, 0 failures. Every check counted in a scratch copy whose `check()` counts (release): each group's count the
  same before and after (round 6's GE time 1126; rounds 8, 9 and 10's 1243, 938 and 1834; rounds 10 and 10b's 151;
  "curves round 3" 10; the other eight groups too). The rounds' variables unset or empty: before, these five passed
  (all five rounds required: "11 groups, 6 failures", the review's output byte for byte; round 8, 9, 10 or 10b
  required alone: no failure); after, all 11 groups skip, and fail where required (all five: 12 failures, rounds 10
  and 10b's group once for each round; each round alone fails only its own groups: 7, 1, 1, 2 and 1). The variables
  naming an empty folder: before, the sanitized run stopped in measured6's first group, and these five, run alone,
  passed; after, sanitized and release, every group fails on the PSP's missing file (13 failures). "curves round 3"
  with an empty folder: before, stopped; after, a skip, and 5 failures where required.
- **Not run**: the rest of the PSP suite, tests/allegrex, tests/psp/ares, the runner and the app builds (CI's).
- Independent review: PASS on `e17c27607`, read-only (`~/phobos-work/scratch/review-284/REVIEW.md`), no changes asked.
  Its two wording nits are fixed after it (N1: measured6's `ours()` comment and the "What" bullet; N2: the PR body).

## CI: the GE profile decoder's tests on every change to them — 2026-10-19

Branch `cursor/psp-ge-decoder-ci-cachy`, from master `d9bfbefb4` (#281). CI only, a non-blocking follow-up from
#269's review; the decoder and its tests untouched.
- **The job** (a new workflow, PSP GE Profile Tests, `.github/workflows/psp-ge-profile.yml`): `python3 -m unittest -v
  test_geprof test_gate` in `tools/psp-ge-profile/decoder` with Ubuntu 24.04's own Python, on pushes to master and
  pull requests that change the decoder or the workflow (gate.py reads the owner's captures only as the gate, never in
  its tests). Not a job in psp-core.yml: its path filters would then run every PSP suite for a decoder change.
- **The plugin's capture stays skipped** (`test_plugin_capture`): `make hosttest` needs pspdev, which the runners
  don't have (`psp-config` for `build.mak`, and the SDK's headers, which `plugin.h` includes even with `-DHOST`).
- **Checks**, here (Python 3.14.7): the job's command ran 30 tests, OK (skipped=1); with one assertion broken on
  purpose (not committed), FAILED (failures=1, skipped=1), exit 1; actionlint 1.7.12 with shellcheck 0.11.0, clean.
  CI's run is in the pull request's body. Review: PASS on `483d5b1cf`, independent and read-only, with wording
  items in the PR body only.

## PSP measure, round 10c: Super Monkey Ball's small strips beside the CPU taken apart, Activision Hits' menu's second and third lists — 2026-10-19

Branch `cursor/psp-measure-round10c-2b67` (PR #280, draft), from master `08801ab60` (#269 and #278 merged), master
`d9bfbefb4` merged in (#279, #282, #281); docs/psp-core.md, part 114.
- **Why**: #279's model of the CPU's traffic (part 113) leaves Super Monkey Ball's rest on the GE's side: round 10's
  list H (the game's small trilinear CLUT4 strips) comes out about 16% short beside the CPU, the model having it read
  RAM for 37% of its clocks. #269's refit leaves Activision Hits' menu's second list +48.5% (60 sprites shrinking a
  VRAM picture, charged 1.64 ms where the PSP leaves them 0.25) and its third +26.7% (triangles magnifying a RAM
  texture into 5650); 1fd9133c specified both (`~/phobos-work/scratch/psp-ge-profile/round10/SPECS-10c-1fd9133c.md`).
- **What**: a new first menu line, round 10c (about 30 KB, `manifest10c.txt`): `time-contention4` (31: H as round 10
  drew it, nearest within the level, bilinear and nearest without levels, untextured, its vertices in VRAM, untextured
  with them there; each alone and beside streaming reads flat out and at a half and the game's mix over 256 KiB; each
  pattern alone), `time-shrink` (9: the second list as logged, its sprites alone, nearest, 1:1, 4:1, the picture
  swizzled in VRAM and from RAM, one sprite 240 wide), `time-magnify` (14: the third list as logged, 1:1 and 4 times,
  into 8888, unblended, the texture 512x256, CLUT8; bilinear and nearest each), `time-gamestrips` (6: round 10's
  game-style strips unlit, then nearest, no alpha test, no depth test, into 5650, untextured) and `time-anchors3` (7 of
  round 10's cases). About half a minute on the PSP. Synthetic: nothing of a game's lists, textures or vertices.
- **Safety**: round 10's and 10b's: new names only; the clocks put back after each test; every GE wait 5 s with the
  reset path; the contention thread's own deadline, its lines written back and invalidated after each run; the arrays
  on 1 KiB boundaries; H's VRAM vertices in VRAM's last 256 KiB, past the depth buffer.
- **Test**: "psp measure" runs round 10c after round 10b and checks every record; measured8 runs rounds 8, 9, 10 and
  10b from the new build; `tests/psp/programs/pspmeasure.elf` rebuilt (`b38077b4…`); the owner's EBOOT `243aa27d…`
  (`git archive 9bcaff348`, byte for byte).
- **The owner's run** (2026-10-19 evening; `tests/psp/measurements/ge-round10c`, packed with xz, under his go-ahead of
  2026-10-18): every run finished, nothing stalled; round 10's anchors repeat within 0.2% where nothing moved, 2 to 8%
  cheaper where round 10's build had its arrays 64 bytes past 1 KiB boundaries.
- **What it showed** (part 114): the CPU slows H through its vertices' reads from RAM, ×1.29 to 1.33 beside streaming
  reads, ×1.24 to 1.29 at a half, ×1.43 to 1.51 beside the mix, the same untextured or with any filter; with the
  vertices in VRAM (read 22 to 31% slower alone), ×0.99 to 1.02. The core with #279's model gives every variant ×1.15
  to 1.16, 1.10 to 1.11 and 1.20 to 1.21: 10 to 20% short with the vertices in RAM, and 9 to 21% over the PSP with
  them in VRAM, which the PSP doesn't slow; before #279, ×1.00. Activision Hits' second list as logged takes the PSP
  4.52 ms (the game's own 3.48): the log doesn't describe it, and the core is within 5% of the logged list. Its third
  list's logged quads take 1.35 ms of the game's 5.40 (the core 3.7% over); bilinear costs 36% at a texel a pixel and
  5 to 10% magnified, which the core doesn't charge. Round 10's game-style strips take three times as long with the
  depth test into 8888 as with either alone (the core 53% short with the pair, within 12% without).
- **Next**: Super Monkey Ball's refit (its own pull request): H's vertex reads as most of its time beside the CPU, the
  curve against the CPU's traffic, VRAM vertices at their own rate and out of the contention. For #269's agent: the
  second list's own words, a bilinear term heavy at a texel a pixel, the depth test into 8888.

## PSP GE: part 113 and the handoff after #279's second review — wording only — 2026-10-19

Branch `cursor/psp-279-nits-2b67`, from master `f5cf0c6b5` (#279, merged at `9f4e7607a`). Docs only.
- **The second review** (18de2dd5, `0c4baeb4f..9f4e7607a`: approve with nits): the spin skip exact for every case the
  code allows (two equal turns settle both caches' tags and dirty bits; one syscall a turn keeps what the window owes
  falling then rising at most; the window's time changes kind at most once); skip on and off the same in Resistance
  (160.5 million turns skipped) and Burnout Dominator (none skipped: 1,239 refusals for unsteady turns, 86 for lines);
  the gate and the measured rounds reproduced; "Fast" still master's.
- **Its wording nits, put right here**: part 113's round 10 in the core, 94.0% of the pairs within 25% (not 93%); its
  sentence on what's refused with lines in play now says what the code does; the #279 entry below: only turns of more
  than one syscall that move lines are refused, not every turn of more than one syscall.
- **Its other nits, for the spin skip's widening** (its own pull request, after #277's): tests that fail when each of
  the four refusals is taken out (the GE done part way, turns of more than one syscall moving lines, the window's time
  changing kind, unsteady turns; today's test fails only without the skipped turns' lines); a skip shortened to end
  before `geDoneAt` instead of refused whole; `spin.wait` grown after unsteady refusals; the steady rule's theoretical
  hole (a loop settling in the last turn run, its counts equal but its lines others).

## PSP GE: the CPU beside the GE — the CPU's caches counted with the PSP's GE timing, the GE slowed by the lines they move while it draws (Super Monkey Ball −29.6% to −20.6%) — 2026-10-19

Branch `cursor/psp-cpu-traffic-2b67`, from master `08801ab60` (#269, part 106); draft PR #279. docs/psp-core.md, part
113, has the evidence. Clean room: our code; no PPSSPP or JPCSP. Save states: layout 35 (2,168 bytes more, at one
place; older layouts load with the caches empty).
- **The caches** (`Allegrex::Cache`): the PSP's CPU's data and instruction caches as tags (16 KiB each, 64-byte lines,
  two ways, least recently used out), looked up by compiled code alone in blocks compiled for a system started with the
  PSP's timing ("Fast" compiles none of it: its code master's): a load's or store's line in its set's most recent way
  inline (seven instructions), an instruction's line as a block starts and at each line's first word (three on ARM64), a
  block going on to itself skipping its own first line's look. Counted: RAM's lines read in, dirty ones written back,
  uncached accesses, instruction lines.
- **The term** (`Kernel::geContend()`, `GE::Cost`): over the GE's busy window, the lines moved as a share of a thread
  streaming RAM flat out, over the longer of the time passed and the PSP's CPU's time for the window's instructions and
  stalls, times round 10's slopes by the GE's kind of read (refitted with the model's own counts:
  `tools/psp-measure/contention11.py`, vertices 0.34, words 0.27, texels 1.16, transfers 0.57, write-backs 0, an
  uncached access 0.2 of a line); the window owes passed x slow / (1 + slow), each line costing the GE its share once.
  The CPU's work before a list and polling cost it nothing. A first version charged a burst again in every later cycle
  of the window (Super Monkey Ball looked like −14.5%); the writer test caught it.
- **Against the PSP** (`contention11.py --core`, the core's own rounds 10 and 10b; `measured8`'s new group in CI): round
  10's 680 list-pattern pairs within 5.7% at the median (15.5% with the term held at none); round 10b's 144 within 1.0%
  (polling and work before x1.00, a writer beside list A x1.91 against 2.02, beside the untextured list x1.00).
- **The gate** (part 113's table, per frame and per list): Chains of Olympus −5.0% to +0.3%, Ghost of Sparta −7.1% to
  −2.2%, Killzone −12.3% and −10.9% to −9.9% and −8.4%, Super Monkey Ball −29.6% to −20.6% (its exception kept, now with
  a limit, −21%), Activision Hits' copyright screen −5.8% as it was, its menu +2.2% and +1.9% to +6.7% and +6.4% (its
  first list −10.6% to −4.4%; the second and third, +48.5% and +27%, as they were). Lists 10 of 15 within 15%.
- **The owner's decisions** (2026-10-19, 11:08): Activision Hits' menu frame at +6.7% accepted; the host cost accepted;
  merge after the review and checks; round 10c (draft PR #280, e15d2cff's) times list H's kind (Super Monkey Ball's
  small trilinear CLUT4 strips, predicted about 16% short beside the CPU's patterns) taken apart, its refit to follow.
- **The review** (18de2dd5 at `0c4baeb4f`: approve with should-fixes): "Fast" exact and free, the caches deterministic,
  states mid-list carried on, fits and gate reproduced. Fixed: the spin skip counts each skipped turn's lines as the
  turn before moved, and skips no turns the GE would finish part way through (test: the GE polled with a turn reading an
  uncached word, or missing three lines; skip on and off the same); the window's sums are whole counts and its push
  whole cycles, so a skip's one stretch adds up as its turns would (the floats' last bits differed), and with lines in
  play no turns are skipped where the push could rise and fall within them (a turn of more than one syscall that moves
  lines, or the window's time changing kind); a push moves `geReachAt` only with a SIGNAL due (as master did; `geTime()`
  works it out otherwise). Documented: the interpreter isn't counted, so under the PSP's timing `--interpreter` gives
  the GE other times than the recompiler (part 113, the runner's help). The cheap win (the data look's add) doesn't
  apply: `Allegrex` starts with its vtable pointer.
- **#277 lands after this** (b331a6e1 ports its threaded GE onto it): each stretch must keep its instructions and the
  caches' four count deltas as it ends; the pushes must agree with the folds; states save the drawing GE's clocks by
  kind; the measured rounds keep `KernelMachine::pspTiming()` (part 113, "Not yet").
- **Host cost** (the emulation thread's instructions a frame on the Mac, the PSP's timing both sides; part 113's
  tables): the counting alone (master's emulation exactly) +5.8% (Super Monkey Ball) to +16.7% (Activision Hits'
  copyright screen) on the gate's scenes; on the library check's polling games from boot +0.8% (Dragon Ball Z) to +39.5%
  (Dante's Inferno, its loading streaming through memory). As players get it (the GE slowed too, so games wait and poll
  differently): −4.2% to +16.8% on the scenes, −12.6% (Midnight Club) to +41.6% (Dante's Inferno) on the games. Cut: a
  block going on to itself skipping its line's look (−4 to −6 points on the spinning scenes), ARM64's short instruction
  compare, the kernel's sums skipped while the GE is idle; measured and left, skipping VRAM's and the scratchpad's
  looks (0 to 2.2% of them). The RP6 run (master against the branch, the scenes and Dante's Inferno, Burnout Dominator
  and Need for Speed: Shift) is the coordinator's: `smba-rp6.sh` in the runner-android scratch folder.
- **Not yet**: the kernel's cache calls and the `cache` instruction; the CPU's traffic with VRAM; the CPU slowed beside
  the GE; the interpreter counted alike; spins without syscalls pay a data look a turn (the spin skip's widening, its
  own pull request).

## PSP GE: the GE's time as the owner's PSP took it — its two ends and their queue fitted to rounds 6, 8, 9 and 10, the option "GE Timing"'s default ("Fast" the opt-in), the gate against the captures by the whole frame, a SIGNAL's interrupt at its time — 2026-10-18

Branch `cursor/psp-ge-time-2b67`, from #266's branch (part 103, merged as `a014706e9`) on master `a8d5640f5`, masters
`dd63f4ffb` (#266, #267), `230c0a731` (#268), `fe3deab82` (#272, the GE thread; #271, the display latch; #273),
`14e8065dc` (#270, round 8), `d428d91f9` (#276, round 9) and `46922a9d9` (#278, rounds 10 and 10b) merged in; draft PR
#269. The owner's decisions (2026-10-18, 21:57 to 22:02): the PSP's timing is the default, "Fast" the opt-in; each
scene judged by its whole frame (his bar since 20:35); Super Monkey Ball's frame (−29.6%) a known exception for now,
its fix the next pull request's; the merge after the review's second look, his library check with the PSP's timing
and an RP6 run.
docs/psp-core.md, part 106, has the evidence (part 76, the first model's fit, is here too). Clean room: our code, uOFW
for the GE driver's behaviour; no PPSSPP or JPCSP. Save states: layout 34 (#271 took 33; 18 bytes, and the GE's
pipeline: 28 and 16 a queued item; "Fast" 46 in all).
- **The model** (`GE::Cost`, `GE::Pipe`): the front end (words, PRIMs, vertices at 0.512 a byte with a floor of 5.02,
  lights and their shine by kind, cuts) hands each primitive, transfer and palette load to the back end (rows, pixels,
  texels) through an 83-entry queue; the texture cache keeps the texels fetched since the last TEX_FLUSH (a box in whole
  fetches, 8 KiB), a primitive paying for what it reads outside it; rows read wide past 140 texels (270 indices), a
  triangle's share 1 − (140/W)² (into 16-bit, 0.489 of the rest too); CLUT32 fetches at rates of their own; the pipeline
  is carried through stall updates and SIGNALs. The CPU beside the GE: round 10's slopes measured, the term held at none
  (the emulated CPU can't tell a game waiting without syscalls from one working).
- **The fit** (`tools/psp-measure/ge-terms.py`, `ge-fit.py`): rounds 6, 8, 9 and 10, 3,373 timed lists, 5.0% at the
  median (70% within 10%, 90% within 25%), its own fixed point after three passes; 253 lists that moved more than 5%
  between the owner's two sessions held out (`--again`: placement, round 10's time-anchors2); the cases the model's
  texels can't follow (perspective, turned, mipmaps) shown as checks; `measured8` tests rounds 8, 9 and 10 in the core.
- **The default**: "PSP" wherever one is chosen (the core's option, any value but "Fast"; the kernel's flag; the app's
  setting and its native default; the runner's `--ge-timing`); the desktop builds take the core's. The app's text: the
  GE draws at the speed measured on a real PSP, so games pace close to a PSP's; "Fast" draws faster than a PSP. The
  tests choose (the kernel test machine "Fast" but where a test times the GE as the PSP's; the ares checks "Fast"
  after checking the default; the runner's test checks its default).
- **"Fast"**, the opt-in, is master's bit for bit (checks below) and costs about 0.8% more instructions a frame on
  Vulkan-fast at round 10's refit (the review's second round: Chains of Olympus +0.75%, Ghost of Sparta +0.86%, Super
  Monkey Ball +0.6 to 1.3%), no measurable time a frame on the Mac; the template-per-timing follow-up would take it
  away.
- **The GE thread** (part 109): "PSP" keeps the GE on the emulation thread (`System::threadGE()`, without a word: the
  runner says so for `--ge-thread on`); with "PSP" the default, the thread only with "Fast". A design for a threaded
  "PSP" is in part 106 (not built).
- **States**: a PSP-timing state from a build whose queue held more (`GE::Cost::Queue` is measured; a refit may lower
  it) loads, keeping the newest items this build's holds (the loader once refused it).
- **The gate** (`tools/psp-ge-profile/decoder/gate.py`, the owner's 18 captures, lists paired by their place in the
  frame): each judged scene by its whole frame, the lists reported; it passes (exit 0): frames 7 of 8 within 15%,
  none more than 2.2% too slow, and Super Monkey Ball's (−29.6%, too fast: the CPU beside the GE) the owner's accepted
  exception (`EXCEPTED`, his words and the date; it fails any other frame miss, and that one the other way). The
  lists: 10 of 15; too slow, Activision Hits' menu's second (+48.5%: its 60 sprites shrinking a VRAM picture,
  untimed) and third (+26.7%: triangles magnifying a RAM texture into 16-bit, untimed).
- **Checks**: the fit reproduced from the tree alone (a runner built from it, the committed program and measurements:
  every term of `GE::Cost` to three figures); "Fast", chosen explicitly, against master's runner on four scenes,
  Software and Vulkan-fast: 8 of 8 the same, every frame and the states (46 zero bytes put in); tests/psp's "ge time",
  measured and "ge profile" groups, the ares system's 362 checks, the runner's test, `test_gate.py` and
  `test_geprof.py`, 0 failures; GitHub's checks for the rest (the last full local suite: 616 groups, 0 failures).
- **Round 10's refit** (PR #278's files, part 112): "ge time" and the measured groups pass with the new clocks
  (measured8's bounds restated: the refit trades a little of rounds 8 and 9 for round 10); "Fast" the same as the last
  head on four scenes, Software and Vulkan-fast. With #278 merged, "measured round 10 GE time" runs round 10 in the
  core against its files (832 lists, half within 5.5%; CI requires `ge-round10`). A round 10c (the refit's agent's
  spec) would time the menu's second and third lists' cases.
- **Next**: the CPU beside the GE, the next pull request (the owner: "fixing SMBA is the priority even though we are
  moving on past it"): the PSP's data cache as tags, counted in the recompiler's loads and stores for "PSP" alone, the
  lines a window giving the CPU's RAM traffic (part 106's "Not yet" has the design; about 10 to 15 hours).
- **Not yet**: perspective, turned and mipmapped triangles; bilinear filtering's cost; narrow CLUT strips from VRAM;
  "Fast"'s templates per timing; fixed-point charges (states across hosts and renderers carry on with the GE a few
  cycles apart); round 10 run once in CI's sanitized job for both its tests (the job's limit raised to 90 minutes).

## PSP measure, round 10: lights by kind, CLUT32 triangles, wide triangles into 16-bit, perspective, mipmaps, the CPU beside the GE — 2026-10-18

Branch `cursor/psp-measure-round10-2b67` (PR #278, draft), from master `d428d91f9` (#276, part 110);
docs/psp-core.md, part 112.
- **Why**: #269's refit with round 9 (`c0d9c14a2`) leaves three scenes outside 15%: Super Monkey Ball −30% (mostly the
  PSP's CPU competing with the GE for RAM), Activision Hits' 3D menu (its first list −19%: spotlights with their shine;
  its second +56%: CLUT32 triangles, a format no round timed) and its copyright screen (−24%: two wide triangles into
  5650). #269's agent (1fd9133c) specified the cases from each scene's terms and a log of the games' PRIM state
  (`~/phobos-work/scratch/psp-ge-profile/round10/SPECS-1fd9133c.md`); synthetic, nothing of a game's lists, textures or
  vertices in the program.
- **What**: a new first menu line, round 10 (about 0.2 MB, `manifest10.txt`): `time-anchors2` (six of round 9's anchors;
  time-strips 37's texture at four alignments), `time-lights` (39: 1 to 4 lights of each kind, diffuse and shining; the
  shine's power, which lights shine, fall-off; the game's mixes, skinned; strips drawn as the game draws them),
  `time-clut32` (22: the menu's tiles as logged, one thing at a time changed; CLUT32 strips as triangles),
  `time-widetri` (18: the copyright screen's pass, one thing at a time changed; round 9's wide triangles and a sprite
  into each 16-bit format), `time-perspective` (48: w 1 to 1.25/1.5/2/4 across and 1 to 2 down, three textures; squares
  in through mode turned a quarter), `time-mipmaps` (22: round 8's mipmap cases, level 1 alone, the filters, CLUT8;
  Super Monkey Ball's small trilinear CLUT4 strips), `time-vram8` (60: strips 4 to 24 wide, CLUT8 and CLUT4, VRAM and
  RAM), `time-contention` (197: ten GE lists, each alone and beside a thread running one of 17 memory patterns, and each
  pattern alone) and `time-contention2` (54: the streaming reads at 333/222/166). About two minutes on the PSP, three at
  most.
- **Safety**: new names only; the clocks put back after each test; every GE wait 5 s with the reset path; the contention
  thread (above the program's priority, as the spec asks) stops at the finish callback's flag, its 10 ms alone, or 5 s
  whatever happens; its memory a 1 MiB array of its own and VRAM's last 256 KiB (time-clut32's filled buffer runs into
  it, at another time), written back and invalidated after each run.
- **Test**: "psp measure" runs round 10 after round 9 and checks every record; `tests/psp/programs/pspmeasure.elf`
  rebuilt.
- **Results** (the owner's PSP, 2026-10-18 evening, every earlier round again after it; part 112): placement first: this
  build put the RAM textures and palette 64 bytes off a 128-byte boundary, and a swizzled texture there costs 7% more
  (time-anchors2: 2,307.5 against 2,154.5 µs on a 1 KiB boundary; 256 to 768 past 1.0 to 1.6%), which is tonight's round
  9 shift (swizzled RAM cases 3 to 11% dearer, unswizzled and VRAM within 0.6%); other placements moved RAM reads up to
  26% either way. The re-runs: every picture the same bit for bit, the timing medians within 0.0 to 2.1%. Lights: point
  and spot lights alike, 11 a light-vertex diffuse, 19 with shine (8 more); directional ones shine for about 1, but a
  lone one costs 21 more when the disabled light registers hold spotlights (round 8's 20.7 was that). CLUT32 fetches as
  8888; the menu's logged tiles take 3.23 ms, leaving 0.25 ms of the game's 3.48 ms list for its 66 sprites (about 1.7
  ms of texels): the log's tiles aren't the game's. The copyright pass matches the game (462,500 against about 450,000
  bus clocks a pass); its miss is bilinear reading wide (+20%), not 16-bit. Perspective halves wide rows when w changes
  along the row (10.0 to 4.2 clocks a pixel), not otherwise; a mipmapped 8888 texture reads level 1 at level 0's wide
  cost (10.3 against 2.2); narrow strips cost by 16-byte units; the CPU's RAM traffic slows the GE's RAM reads by kind
  (Super Monkey Ball's lists ×1.38 beside streaming reads, ×1.54 beside a game's mix; wide texels ×1.85 to 3.2; VRAM
  work not at all).
- **Round 10b** (part 112), for the owner the same night: `time-contention3` (the spec's patterns 11 and 12: polling
  through system calls, 1 MiB written before the enqueue with and without a writeback, and once beside the list, against
  six of round 10's lists) and `time-placement` (one swizzled RAM texture 0 to 512 bytes past a 1 KiB boundary,
  controls, a palette at two offsets, CLUT32 swizzled in RAM); its own menu line (the first) and `manifest10b.txt`; the
  program's arrays on 1 KiB boundaries from this build on. About half a minute. He ran it the same night
  (`tests/psp/measurements/ge-round10b`, part 112): polling doesn't slow the GE (×0.97 to 1.03), nor 1 MiB written just
  before the list (×0.99 to 1.05); a writer beside it slows it by its rate (wide RAM texels ×2.0 to 2.1, Super Monkey
  Ball's lists ×1.36, its small strips ×1.27); a swizzled texture or palette off a 128-byte boundary costs 5 to 8%, a
  multiple of 128 off 1 KiB 1.5 to 2%, unswizzled nothing; CLUT32 swizzled in RAM 2.42 clocks a pixel.
- **Next**: #269's agent refits with rounds 10 and 10b (no model change here); round 10's files
  (`tests/psp/measurements/ge-round10`), the re-runs of rounds 2 to 9 (`ge-reruns-round10`) and round 10b's
  (`ge-round10b`) are in the repository with the owner's go-ahead (2026-10-18; `development-process.md`).

## PSP measure, round 9: triangles reading narrow textures, a texture's loads, vertex formats, time-lists again — 2026-10-18

Branch `cursor/psp-measure-round9-2b67` (PR #276, draft), on master `14e8065dc` (#270, part 107, merged; this branch
was stacked on it till then); docs/psp-core.md, part 110.
- **Why**: #269's gate passes 5 of its 9 judged scenes at 15% (part 106). Too fast: Ghost of Sparta's menu −21%, Super
  Monkey Ball's story −31% (a texture set costs the PSP about the same at 1, 4 or 16 KiB, where the model charges by
  size), Killzone's second capture −19%; too slow by the gate: Activision Hits' 3D menu +29% (a median across its three
  lists a frame: list for list its second +100%, the others too fast, the frame −14%). Round 9 measures what the model
  guesses there: triangles reading narrow textures (taken as sprites'), texture loads and the cache's reuse, vertex
  formats (8-bit positions 4.5 times the model in round 8), and round 8's `time-lists` again with #270's fix.
- **What**: a new first menu line, round 9 (about 0.1 MB, `manifest9.txt`): `time-anchors` (15 of round 8's cases,
  first), `time-tristrips` (88 cases: strips 8 to 512 wide as triangles from seven textures, RAM and VRAM, swizzled and
  not; sprites over swizzled VRAM; squares of two triangles; 3D; bilinear; shrunk 2:1), `time-loads` (54: new, the
  same, TEX_FLUSH, the address alone, textures in turn, by size, format and place; sets, flushes and palette loads
  alone; palettes of 16 to 256 colors; mipmaps), `time-formats` (34: positions, colors, texture coordinates and
  normals of each width, whole vertices, indices, culled, drawn, through mode, strips), `time-lists2` (round 8's
  `time-lists` with cached stall addresses, all four clock settings). About a minute on the PSP, two at most.
- **Test**: "psp measure" runs round 9 after round 8 and checks every record; `tests/psp/programs/pspmeasure.elf`
  rebuilt.
- **Results** (the owner's PSP, 2026-10-18; `tests/psp/measurements/ge-round9`, part 110): round 8's anchors repeat
  within 0.09% (median; one case 2% faster, most likely its texture's alignment in RAM); a texture costs the texels
  read after a TEX_FLUSH (swizzled RAM about 14 + 0.46 bus clocks a byte, any size of texture; no refetch without
  the flush), which accounts for round 8's copy of Super Monkey Ball's lists within 8%; triangles reading narrow cost
  about a sprite's, and wide reads go row by row (a row past about 140 texels, about 270 for CLUT8); shrunk 2:1
  narrow about 3 times a pixel (not 2); RAM 8888 meshes share the RAM with their vertices; a vertex 0.48 a byte with a
  floor of 5 (3-byte vertices 8.5), an indexed one 33 to 39; `time-lists2` finished at every setting (the stall
  case fixed), the front end's lead about 30 sprites.
- **Fix round** (#270's review, its should-fix 10 here): `cpu.c`'s three waits for the GE keep to the 5 s every
  other wait has since round 4 (looks with `sceGeListSync(id, 1)` and `sceGeDrawSync(1)`; past it `cpu-syscalls.stalled`
  and `ge.c`'s reset, shared through `measure.h`); `time-lists`' case 3 gives each wait and the list's end budgets of
  their own; `time-state`'s case 6 named for GU's TEX_FLUSH. `pspmeasure.elf` rebuilt (`3da289db…`), the round 9
  EBOOT too (`e6b20588…`); nothing for the owner to run again. #270's head (`502bd680a`: its fixes, master
  `fe3deab82`) merged in.
- **Review** (`~/phobos-work/scratch/review-276/REVIEW.md`, approve with should-fixes): part 110 put right where it
  went past the data (CLUT8's row threshold about 270, not 295; Activision Hits' lists compared list for list; the
  shared RAM claimed for 8888 alone; five smaller figures), its context since #270 merged, part 107's corrections
  carried into its own text and `ge.c`'s comment (`pspmeasure.elf` unchanged); the owner's go-ahead for keeping
  `ge-round9` in the repository, given on 2026-10-18, is recorded in `docs/development-process.md`.
- **Next**: #269's refit takes round 9's files (no model change here).

## PSP measure, round 8: the GE's time where part 76's model misses the owner's captures, and the CPU's — 2026-10-16

Branch `cursor/psp-measure-round8-2b67` (PR #270; from master `a8d5640f5`; masters `5442acb8b`, `03eca2a69`,
`dd63f4ffb`, `230c0a731` and `fe3deab82` merged in); docs/psp-core.md, part 107.
- **Why**: part 76's model of the GE's time (behind the option "GE Timing" on #269) misses the owner's captures by
  −59% to +138%. Counting its terms over each scene puts the misses on what round 6 never measured: textures read
  from VRAM in strips (Killzone's 512x512 CLUT32 buffer, 79% of its GE time; Activision Hits' menus), a big CLUT8
  texture drawn whole, and Super Monkey Ball's many small primitives, state changes and CALLs.
- **What**: a new first menu line, round 8: 13 timing tests of the GE (big textures in strips, other textures,
  pixels, setup, vertices, state between draws, lists and stall updates, a SIGNAL against its list's end (how far
  ahead of the drawing the front end gets: part 106's queue), transfers, clears, the two ends' overlap, lists like
  the captured scenes', the CPU beside the GE) and cpu.c's tests of the CPU (ALU, latencies, memory, system and
  driver calls, the clocks the power service grants) and of the display as the program starts (what it holds, and
  which frame buffers set at once the firmware refuses: part 108); `manifest8.txt` lists every case. About 3
  minutes on the PSP; it wrote 167 KB. newlib's heap is held to 1 MiB, so the CPU's memory tests get 8 MiB.
- **Test**: "psp measure" runs round 8 too and checks every record; `tests/psp/programs/pspmeasure.elf` rebuilt.
- **Results** (the owner's PSP, 2026-10-18; `tests/psp/measurements/ge-round8`, part 107): round 6 repeats within
  0.15% (median); wide texture reads (256 distinct texels across) cost about 4 to 7.5 times as much a pixel; a PRIM
  about 60 clocks; a texture reload about 374; the SIGNAL's lead about 83 queue entries (a primitive and each vertex),
  not 16 primitives; a CPU thread reading RAM makes the GE's RAM-textured drawing 2.12 times as long; the bus is
  granted at half the PLL whatever is asked. `time-lists`' case 3 never saw "stall reached" on the program's uncached
  stall addresses, ran out its run's 5 s and was taken for a stall (67 of its 80 records empty): fixed
  (`pspmeasure.elf` rebuilt); round 9's `time-lists2` (#276), the same cases with the fix, has all 80 on his PSP.
- **Review** (`~/phobos-work/scratch/review-270/REVIEW.md`, approve with should-fixes): part 107's account put right
  (the record count, the stall's story, the jump's range, the bus clock, the textures' conditions, TEX_MODE, the pixel
  rate's, the sizes; no run of round 8 again asked of the owner), comments in `ge.c`, `cpu.c` and `measure.h`;
  `pspmeasure.elf` unchanged. `cpu.c`'s GE waits without a deadline, a program change, go with #276.
- **Next**: the fit is on #269 (part 106: `tools/psp-measure/ge-fit.py`, `ge-terms.py`); `measured8` once this
  program and that model are both in; Phobos's "stall reached" (both addresses masked), and its bus set to half the
  PLL for a user-level request, each a small PR of its own.

## PSP display: the frame buffer set for the next vertical sync shown from the sync, sceDisplayGetFrameBuf answering for the one asked (Army of Two's 30/20 frames a second) — 2026-10-16

Branch `cursor/psp-display-latch-2b67` (draft PR #271; from master `5442acb8b`, masters `03eca2a69`, `dd63f4ffb`,
`230c0a731` and `92eddbfea` merged in: #274, #266, #267, #268, #272, #273); docs/psp-core.md, part 108. Clean room:
pspautotests' display/setframebuf and isstate, round 8's display-start (part 107), pspsdk's pspdisplay.h; no PPSSPP
or JPCSP. Save states: layout 33 (#269 takes 33 too: the second to land renumbers to 34).
- **Why**: Army of Two flips from its list's SIGNAL callback for the next sync, then asks which frame buffer is shown
  and waits for the blank while its flip isn't yet: two flips and a wait a frame, 30/20 fps on the owner's PSP.
  Phobos took every frame buffer at once, so the game never waited: master draws 251 frames a second for 60 shown.
- **Change** (display.cpp): sceDisplaySetFrameBuf's sync 1 sets the address shown from the next blank's start, or,
  set in a blank before its vertical sync (631.5 us in), from that sync; the width and format at once (pspautotests'
  setframebuf, isstate; round 8's display-start); sceDisplayGetFrameBuf's which 1 reads the one coming, 0 the one
  shown; the screen shows the last one set in the frame after the blank, as before (its picture taken as the blank
  starts: one set in the blank before its sync, after that, shows a frame later than on a PSP, as on master). Layout
  33 keeps one waiting (13 bytes); a state with one due past the next blank's start is refused.
- **Effect** (34 scenes x Software, Vulkan, Vulkan-fast against master; their display calls traced): 31 the same in
  every frame. Army of Two: 120 frames a second drawn instead of 251 (gameplay; menu 194), two a blank, a blank
  waited for every second one, a new picture each (with #269's PSP timing, both merged: 30/20 fps, two flips and a
  wait a frame, as on the PSP); Death Jr. II's character screen: pictures the same, the VRAM in alternate frames and
  the RAM not (its handler's and main thread's reads before the sync find the old buffer). The first try (the next
  blank's start for every next-sync flip) also showed Death Jr. II's gameplay, Sonic Rivals, Disaster Report 3's
  intro and Twisted Metal's logos a blank late: now master's. In those 34 scenes no picture repeats where master's
  doesn't (Army of Two's intro does, in the library check: each of its video's pictures held two frames, master's one
  to three); no wait past the next blank. Games that read sceDisplayGetFrameBuf(0) after a next-sync flip are the
  ones that can change: Army of Two, Death Jr. II, Sonic Rivals here.
- **Library check** (the coordinator's: 266 games, Software and Vulkan-fast, the clock fixed, against `230c0a731`):
  0 newly failing; two real differences, in both renderers, this change's doing and not faults: Army of Two shows
  master's very pictures 4 frames later (its loading frames now wait for the blank, so EALogo.bik opens 58,550 us
  later; each video picture then held two frames); Sonic Rivals 2 presents master's frame buffer at all 3,600 frames,
  but its flip from a GE FINISH callback reads back as not shown yet, as on a PSP, so its second list is queued at
  the next blank and its animation sampled at other moments (1,739 pictures byte for byte master's; the boot
  notice's fade, frames 190-201, a frame ahead of master's; the rest the title screen's sub-pixel motion).
- **Checks**: tests/psp/run-tests.sh (sanitized): 577 groups, 0 failures; the new test fails with sync 1 taken at
  once. With master `dd63f4ffb` merged in: 580 groups, 0 failures (583 with `230c0a731`; 610 with `92eddbfea`
  and the review's fixes); the GE profile records each frame buffer call with the latch (Army of Two's two a frame,
  as the PSP's capture has them).
- **Review fixes** (review of `96383388d`: approve with should-fixes, no blockers): master `92eddbfea` merged, its
  two system.cpp conflicts keeping both sides (the frame from presentedFrameBuffer() under #272's renderer checks):
  Army of Two's first 400 frames and Sonic Rivals 2's first 600, Software and Vulkan-fast, #271's every picture with
  the GE thread off and on. New tests catch the review's four surviving mutants (the latch dropped from
  sceDisplaySetFrameBuf, sceDisplayIsForeground or events(), or made 0.3 ms before the sync) and system.cpp's two
  uses of presentedFrameBuffer() (a pretend GPU: the frame read back and presented); the new state fields are in
  "state fields", and the loader refuses one due past the next blank's start; round 8's display-start is cited.
- **Left out** (follow-ups): setframebuf's refusals and the start's format, with a library check of their own (round
  8's display-start: a PSP starts at address 0, width 0, format 3, Phobos at format 0; not in the foreground; every
  frame buffer set at once refused from there, 0x80000107, or 0x80000104 for the start's width of 0, the display
  left as it was; one for the next vblank taken); when a PSP takes one set as a blank starts (at that sync here,
  unrecorded: a set at a blank's start read back before and after the sync would say); perhaps the picture taken at
  the sync rather than as the blank starts.

## PSP GE on a thread of its own, exact: option "GE Thread" (Off, the default, and On; the app doesn't offer it yet), the emulation thread waiting only where the GE could still change what it reads — 2026-10-17

Branch `cursor/psp-ge-thread-2b67` (GE round 4, part B, first PR), on master `a8d5640f5` with `5442acb8b` (#265),
`03eca2a69` (#274), `a014706e9` (#266, part 103: with the thread On its GE profile records the same),
`dd63f4ffb` (#267, part 104) and `230c0a731` (#268, part 105: its skips through the GE's looks hold with the thread
On, no further than the GE time's bound) merged in;
docs/psp-core.md, part 109, has the evidence; the design (revision 4, approved by the independent review,
both conditions met) is summed up there. Clean room: no PPSSPP for GE or GPU code, JPCSP not read. Save states
unchanged (the same file with the thread on or off).
- **What**: the kernel's GE on the emulation thread runs every list's commands as before and hands the drawing to a
  render GE on its own thread as records; a model of what each PRIM may change has the emulation thread wait only
  where the GE could still change bytes it reads; its writes are made at once, their old bytes kept as versions the
  render GE reads and writes back through; the GE's clock folds the runs the thread counts. Exact by construction,
  checked by two debug checks on in every threaded test machine (the whole value rule, and G-interest).
- **The limit of this PR**: the frame is still shown from the emulation thread, which drains the GE thread at every
  frame's end, so the threads overlap within a frame only. The frame shown from the GE thread, bounded drains and
  unloading with it stuck, then Auto (design steps 3 and 4) are the stacked second PR's, with the app's option.
- **Off costs nothing**: the emulation thread's instructions a frame against master on the M1 (Vulkan fast, 300
  frames, 5 rounds): Super Monkey Ball -0.29%, Ghost of Sparta -0.02%, Chains of Olympus +0.05% (14 rounds +0.04%,
  master's own spread 0.45%); ms within noise.
- **Exact, the thread On with the checks against Off**: the 78 settled hashes (26 scenes x three renderers) all the
  same; 26 scenes' 600 frames with states every 150 all the same; schedule fuzzing (10 scenes x three ways) and the
  stress mode (a finish every 97th PRIM and kick end, 1x/2x on 26 scenes, 4x on 8) all the same; Ghost of Sparta
  from its start, 2,400 frames, every frame, Software and Vulkan fast, the same; the suite Off, On and On fuzzed three
  ways 0 failures (604 groups now: #265's and #266's merged in, the library check's two tests, the review's six);
  ThreadSanitizer over the suite with the thread On, and over every GPU group on MoltenVK with the thread On
  (pspsdk's samples included), no report, nothing suppressed.
  Targeted tests for the review's findings (R14-R16, R19, R22-R24, the hull rule) each fail with their rule out.
- **Found and fixed on the way**: a write whose own checks finish the GPU had the finish's write-back land over it
  (now versioned where its pages may be the GPU's); with the depth moves off a later depth put could change bytes the
  model held clean, at 1x too (R25: the put rule now holds there); ThreadSanitizer over the GPU groups found the GE
  thread starved while the emulation thread writes into pages it writes back (it waited for every version not yet
  ready; now only for one being copied) and a race in the tests' gate (they drain first now). The coordinator's
  library check with the thread On found two costs of the control pass that grew without bound (the pictures were
  Off's): the software renderer's textures read from memory kept one entry per PRIM until drawn and gone through at
  every report the emulation thread takes (each VRAM load, each list word in VRAM): Ghost of Sparta from its start
  crawled past the check's 15 minutes, Gran Turismo 226 s On against 14 Off (now kept by page: 15 s); and an indexed
  PRIM's vertices copied from the buffer's start to its most index (now least to most: Jeanne d'Arc 123 s → 36 s, Off
  30). A test each, failing without its fix. The independent review found: past 4,096 runs handed over before a
  FINISH (GU's direct lists: a run per draw) the thread's counts of later runs overwrote ones not yet folded, a race,
  the GE's time wrong (the kernel now folds the oldest at each run past 4,096); the render GE's reads of nothing went
  unreported, so the count a state keeps differed (now reported through the kernel's GE's memory, the count atomic,
  saturating at 16); and three rules no test caught (software textures' conflicts, software depth rectangles, the
  busy check for a clean read on a busy page). A test each now, failing with its rule or fix taken out.
- **Speed**: On is exact but seldom faster than Off on the M1, and in some games much slower (BlazBlue 4 s Off, 20 s
  On; SSX on Tour 8 s, 43 s): every CPU access to VRAM takes the slow path and each store is a record of its own (the
  design's narrower access records and run encoding aren't done yet), and games touching what the GE thread still
  draws have the threads take turns. Auto (step 4) keeps such games synchronous; On is for the tests and the runner.
- **The clock's go split is needed** (the review found the case: an alarm's call held back with interrupts off, mtic
  turning them on inside a go limited by the GE time's bound; without the split the call starts at the bound): kept,
  with a test that fails without it.
- **Next**: the coordinator's library check with `--ge-thread On` again; then design steps 3-4 and the narrower,
  batched access records in later PRs.

## Screenshots: a film-strip card pops out of the picture and hops into a folder, and a pill says where it went or why it didn't; named for the game and the time; the core lets the picture go before it encodes — 2026-10-17

Branch `cursor/screenshot-feedback-2b67`, on master `5442acb8b` (part 102, #265), master `03eca2a69` (#274) merged in;
draft PR #273, with the fix rounds of its review's two rounds (each: approve with should-fixes).
- **The report** (owner, 17 Oct): Shot in the pause menu gave no sign it worked, so he pressed it three times and fell
  back to Android's screenshot. It had worked: three files, each named for the ROM's file name and milliseconds
  (`God of War - Ghost of Sparta.chd_1792260870863.png`, 480x272).
- **The design** (owner, 17 Oct): a pop-out like Android's own screenshot, a photo reel going into a folder icon,
  "aesthetic, but performant at all costs", with fun and whimsy. It matches the coordinator's preview
  (scratch/screenshot-fx/preview.html): its layout, keyframes and CSS easings.
- **The effect** (ui/ScreenshotEffect.kt): `MainViewModel.takeScreenshot`, which the pause menu's Shot and the
  screenshot hotkey both call, in every core, takes one shot at a time (a press while one is being taken and written is
  dropped). `Screenshots.save` makes the file; `PhobosCore.captureScreenshot` takes the picture, which the core keeps,
  and hands back a copy at most 640 px either way, which goes straight to the effect as a `ScreenshotEvent`; then
  `writeScreenshot` writes the PNG while the effect plays, and the pill joins once `Screenshots.note` says how that
  went. Should that be late, the pill waits for it and the slide-off waits for the pill (the card stays in the folder),
  so the pill always slides in and stays up 660 ms before it all goes. While they wait the clock holds still at
  2196 ms, by when everything else has stopped, so nothing is drawn; after 8 s the held time stands for the note's
  (`runSavedShot`). `ScreenshotEffect`, after the pause menu in EmulatorScreen's Box, collects `screenshotEvents` (only
  the newest waits). Saved, over 2.7 s or a late note's more: a film-strip card holding the picture starts exactly over
  the game picture, washed white, and springs to the bottom-left corner as it develops (the top-left where the touch
  controls show in play, even when shot from the pause menu; clear of the system bars and the cutout); the film winds
  on two holes; a manila folder springs in beside it, its front panel tips open, the card hops in between its panels,
  the front snaps shut, the folder squashes, nine sparkles burst, and a pill says "Screenshot saved" over "Screenshots
  › PlayStation Portable" (in the app's own storage, which no file manager shows, where to choose a folder); then it
  all slides off to the left. On a narrow screen the pill keeps to its title, as the preview does, but for that hint or
  a failure, which wrap.
  No picture, or a folder that can't be written: the pill alone, error-tinted, with the reason, 3 s; a write that fails
  once the card is out: the pill beside the folder says so. A new shot restarts it. Remove animations: the card in the
  folder with the pill, still, 1.6 s from when the pill shows (a delay, which the animator scale doesn't shorten).
  TalkBack hears where it went once that's known.
- **Its cost**: nothing in composition while none runs; one clock (an `Animatable`, linear) and the note's time (a
  float state, set once), read through `ShotClock` (primitive floats, so nothing's boxed) only in `graphicsLayer {}`
  and draw lambdas, so a frame recomposes and lays out nothing (EmulatorScreen never recomposes for it; the pill is
  composed once, when its text is known). One `Layout` places fixed-size nodes once: the folder's back, the card, the
  front, the sparkles, the pill, each its own small layer, each fading on its own (`ModulateAlpha`: no offscreen
  buffer, which would cut off the folder's tab, drawn above its node; so the card shows through the front panel as
  they fade at the end). Paths and brushes cached; no blur; shadows only on the card (none while it covers the
  picture) and the folder's back (one corner radius, so its outline needs no Path). The effect's own code allocates
  nothing per frame; Compose UI 1.7.6 does, for each animated layer as its block runs (javap): the block's wrapper
  lambda and the layer's outline anew (a Rect and an `Outline.Rectangle`; for the card and the folder's back a Rect, a
  RoundRect and an `Outline.Rounded`), three or four small objects a layer, and the `Animatable` its own. The card's
  picture is the capture's copy (averaged down in native code) as a hardware bitmap, dropped at the end. No pointer
  input or focus: the touch controls, the game and the pause menu keep theirs.
- **The motion** (util/ScreenshotFx.kt, pure Kotlin, internal): `CssEase` (CSS's cubic-bezier, solved as browsers do),
  `Track` (keyframes as Web Animations hold them), `ShotGeometry` (the preview's layout in hundredths of the screen's
  width; a tall picture's card, the WonderSwan held upright, goes into the folder narrower than 80% so its bottom
  stays behind the front panel), `ShotTimeline`, `ShotErrorTimeline`, and `flapCorners` (the front panel's CSS
  `perspective() rotateX()`, drawn through `Matrix.setPolyToPoly`).
- **Native** (PhobosRunner.cpp, PhobosJNI.cpp): `takeScreenshot` is now `captureScreenshot` then `writeScreenshot`
  (both at once for the save states' previews and the desktop). Its general path held `windowMutex`, which `video()`
  takes for every frame, through the colour conversion of the whole buffer and the PNG encode; it now copies the frame
  under the lock and converts after. It takes the frame's own size, kept with `lastFrameBuffer` as `video()` writes it
  (`currentWidth`/`currentHeight` followed the window's target size, which a frame of another size can share, and are
  gone), and turns a WonderSwan held upright as `video()` turns it for the window (its PNG, and the card, were
  sideways). The last frame is let go as a game unloads. `writeScreenshot` says whether all of the PNG got there (the
  file's size, below). The JNI keeps the captured picture for `writeScreenshot` (or `releaseScreenshot`) and hands back
  its copy, each pixel the average of a square of the picture's; where even the copy's stand-in can't be allocated, it
  clears the out-of-memory error and there's no screenshot, and a path it can't read is not written. The N64 and PSP
  Vulkan paths are as they were.
- **The name** is the ROM's file name without its extension (`romTitle`, as the pause menu's title drops it) and the
  local date and time: `God of War - Ghost of Sparta 2026-10-17 14-15-45.png`. What FAT, exFAT and SAF's providers
  refuse (`\ / : * ? " < > |`) becomes `_`, control characters go, so do spaces around the title and dots before it,
  the title is cut at 150 UTF-8 bytes on a whole character, and it's "Screenshot" when nothing's left. Each file is
  made (`createNewFile`) before the core writes the picture into it, so a shot never replaces a file, and shots taken
  in the same second, even at once, get `-2`, `-3`. nall's PNG writer carries on past failed writes (a full disk), so
  `writeScreenshot` checks the file: the size the encoder writes, 63 + height x (6 + 4 x width) bytes, or, where nall
  then had `optipng` or `pngcrush` recompress it (a desktop's), smaller and still ending in IEND. A file not written
  whole, or left empty, is deleted and the pill says the folder can't be written. The folder (`<Screenshots Path, or
  the app's own>/<system>`) and the PNG are as before, but a WonderSwan held upright's is upright.
- **Why the core's failure means no picture**: `captureScreenshot` finds none (no frame yet; the PSP's or the N64's
  picture unreadable) before anything is written, and the file made first shows the folder takes one.
- **Tests**: `ShotTimelineTest` (at 0 ms the card's picture is the game picture's rectangle, for six screens; it
  rests in its corner, clear of the system bars; it ends in the folder between its panels with its top showing; the
  hop; the folder's opening, shutting and sparkles; at the end everything is off to the left and transparent; never
  NaN, opacities in 0-1; no jump where keyframes meet; Remove animations still, every value; the failure's pill; the
  easings against values solved apart; the front panel's perspective; 70 values at 20 mid-span times as the preview
  itself moves, sampled from its animations in Chrome: scratch/screenshot-feedback/golden; a note known by the pill's
  time changes nothing, a late one keeps the pill's whole slide-in and its 660 ms up before the slide-off, one after
  the on-time end still gets its pill, and none at all still ends; once settled nothing but the pill and the slide-off
  moves, and `runSavedShot` holds the clock still there while a late note is awaited, taking the held time for a
  note that never comes, but never for an on-time one). `ScreenshotsTest` (the name, the characters, long
  titles, never overwriting, the folder made, no picture, a picture not written whole or left empty, a folder that
  can't be written, the note: the old message's texts as the pill's two lines and TalkBack's, the hint).
- **Checks**: on the Mac, modern flavor: `ScreenshotsTest` 9/0, `ShotTimelineTest` 20/0, the suite 379/0 (50
  classes). The clock's hold tests fail when the clock runs on through the wait or the held time isn't taken on the
  timeout (each tried, then reverted). Seven mutations each fail a test, each reverted: the review's first four
  (each span eased with the next span's curve, the 330 ms pose changed, the hop eased out: the golden test; Remove
  animations' turn ignoring it: its test) and its second round's three spans that looked linear to it (the wind-up,
  the folder's last settle, the dip back: the golden test at 1115, 1880 and 1735 ms). The native files pass clang's
  syntax check for arm64-v8a with the build's definitions and include paths; CI compiles them in both flavors' APKs
  and the desktop builds. The PNG size `writeScreenshot` expects matches nall's own output from 1x1 to 1920x1088 (the
  review's, to 4800x2720), and must change with nall/encode/png.hpp: a larger file is refused outright. Not run on the
  RP6: the coordinator measures the effect there. Not seen drawn here (no device, no preview renderer): the
  `@Preview`s at 420, 1400 and 2000 ms and the failure show it in Android Studio. nall's PNG writer can't run
  `optipng` or `pngcrush` on Android (nall defines PLATFORM_ANDROID there, and its `execute()` does nothing): the RP6
  never forks for them; the Linux and macOS desktop builds do.

## PSP kernel: the GE's looks skipped through when a game polls them — sceGeDrawSync(1) and sceGeListSync(id, 1) as part 91's spins — 2026-10-16

Branch `cursor/psp-ge-sync-spin-2b67`, on master `a8d5640f5` (part 101, #263), masters `5442acb8b` (#264, #265),
`03eca2a69` (#274) and `dd63f4ffb` (#266, #267) merged in; draft PR #268. docs/psp-core.md, part 105, has the
evidence. Clean room: our code, uOFW for the GE driver's behaviour; no PPSSPP or JPCSP. Save states unchanged
(layout 32).
- **Why**: the GE timing design's first step (approved by the owner): part 76's GE timing made polling games slower
  on the host (Resistance 59.9 to 40.4 and Need for Speed: Shift 45.3 to 32.9 on the RP6 at 5x).
- **The change** (kernel/ge.cpp, spins.cpp, kernel.cpp, kernel.hpp): the looks (mode 1, or a mode refused, or a list
  refused in any mode) are calm; their spin handlers answer through the same functions as the calls
  (`geDrawLook()`, `geListLook()`, which never read the clock); a wait (mode 0) isn't a spin. Exact as part 91's: the
  GE's state changes only in a system call or between the CPU's goes, and a skip already stops before a list's
  FINISH falls due. The runner's `--spin-skip on|off` (on by default, off with `--ge-profile`, so a profile counts
  every poll as the PSP's plugin does).
- **Measured** (the review, #269's PSP timing, 3,600 frames): Resistance polls `sceGeListSync(id, 1)`: 592,699,906
  turns skipped, the state at frame 3,600 the same as with the skip off, host 24.8 s against 30.2 and 32.0 s. Shift's
  `sceGeDrawSync(1)` loop isn't skipped: each turn also takes a free semaphore (a `sceKernelWaitSema` that succeeds
  at once, not calm), which sets the detector back (57.8 s on, 59.0 s off).
- **Tests**: "the GE's looks polled, skipped as run" (both engines, the skip on and off, the whole state the same
  after every eighth of a frame: the two looks and a stalled list, its stalled phase skipped too; a loop counting its
  turns not skipped); "... behind a list, past a SIGNAL, with interrupts held off" (the review's experiments); "...
  under a GE profile" (every look recorded with the skip off); "ge FINISH after a quiet function" dispatches the looks
  and refusals as calm.
- **Checks**: tests/psp sanitized: 582 groups, 0 failures; CI green.
- **Left**: Shift's loop (a wait that succeeds at once and its signal in the same turn: no net change, no waiter) as
  a spin's turn is its own pull request, after this one.
  Done since: W1, in the entry "PSP kernel: the polling skip widened to the EA games' and Midnight Club's loops"
  (2026-10-21; part 116).

## PSP power: Activision Hits Remixed's clocks — the bus asked for alone up to half the PLL, a bus above half the PLL refused, and the owner's captures' 333/166, read before the game ran — 2026-10-16

Branch `cursor/psp-power-clock-2b67`, on master `a8d5640f5` (part 101, #263), masters `5442acb8b` (#264, #265),
`03eca2a69` (#274) and `a014706e9` (part 103, #266) merged in; draft PR #267. docs/psp-core.md, part 104, has the
evidence. Clean room: uOFW and ARK-4's sources; no PPSSPP or JPCSP. Save states unchanged.
- **The question** (part 103): the owner's captures of Activision Hits' boot read 333/166, Phobos 222/111.
- **The game** asks, as it starts, for a CPU of 333 and a bus of 166 with the separate setters, and nothing else in
  1200 frames (traced). uOFW's power service refuses both at a game's startup PLL of 222 (set by its Init callback
  before the game's program starts), as Phobos did. The captures' 333/166 are the reboot's clocks, read while Init
  was still loading the program (the plugin counts from its own start, before the program loads; the game's first GE
  call came 4.0 and 4.23 s in); boot1's capture read 222/111 in the copyright screen, whose lists took the same time
  throughout. ARK with its clock settings at Auto, as the owner had them, passes the setters through untouched (its
  clock code read). Phobos's 222/111 is the owner's PSP's.
- **The change** (power.cpp): `scePowerSetBusClockFrequency` up to half the PLL (was: nothing over 111), so at a PLL
  of 333 it now grants 112-166 and at 266 112-133, and at 190 refuses 96-111, which it granted;
  `scePowerSetClockFrequency` refuses a bus above half the PLL asked for. "power clocks" tests both, at 333, 266, 222
  and 190, and the game's start. The review replayed every power call the 266 games make in their first 1200 frames
  through master's and this branch's rules: no answer changed.
- **For the GE timing work**: the copyright screen is at 222/111 on both, compared unscaled; ARK's clock settings
  stay at Auto for the owner's captures.
- **Checks**: tests/psp sanitized: 579 groups, 0 failures; CI green.

## PSP GE: how the games drive the GE on a PSP and in Phobos — tools/psp-ge-profile's plugin on the owner's PSP, the same records from the core (the runner's --ge-profile) — 2026-10-16

Branch `cursor/psp-ge-profile-2b67`, on master `a8d5640f5` (part 101, #263), masters `5442acb8b` (#264, #265) and
`03eca2a69` (#274: the progress section, its chart of the GE's list time given Killzone's 24.3 ms) merged in.
docs/psp-core.md, part 103, has the evidence; tools/psp-ge-profile/README.md the owner's steps and how the plugin
works. Clean room: pspsdk's headers and libraries, uOFW, ARK-4's README, wiki, `systemctrl.h` and (for how it loads
plugins) its plugin loader's and launcher's sources; no PPSSPP, no JPCSP. Save states unchanged; nothing of the PSP's
behaviour changes.
- **Why**: Phobos's GE time (part 64's geTime()) is Sony's peaks, four pixels a bus clock; Chains of Olympus paces its
  loop by the GE and draws its menu's scene 2-3 times a vblank in Phobos, each drawn by Vulkan. Whether a PSP draws it
  once decides accuracy and that work. Nothing had measured a PSP's GE in a game.
- **The plugin** (`tools/psp-ge-profile/plugin`, 0.2.4): a kernel PRX for ARK-4 that patches the GE driver's and the
  display's syscalls (`sctrlHENPatchSyscall`) and records every call (entry and return times, thread, arguments,
  result, the GE's state), the FINISH/SIGNAL callbacks and the vblanks (the sub-interrupt monitor) into kernel memory,
  written after the capture; a dry mode that counts; notices on screen; `PHOBOS/trace.txt`. `decoder/geprof.py`:
  per vblank and per second, each list's GE time, the GE's busy share, the CPU blocked in syncs and waits, flips and
  displayed frames. `hello/`: a load test (modules that only append a line), used while bringing it up.
- **The PSP's answer** (the owner's PSP-1000, 6.61, ARK 4.20.69 r205; Chains of Olympus UCAS40198, an ISO; dry runs,
  10 s on the menus): 357 sceGeListEnQueue, 357 sceGeListSync, 357 sceDisplaySetFrameBuf, 600 vblanks, no vblank
  wait, no GE callback: 35.7 loops a second, 0.6 a vblank. All 17 hooks in, 24 ns a call idle, 1.85 us capturing.
- **The core's recorder** (`ares/psp/kernel/geprofile.cpp`, `GeProfile` in kernel.hpp; hooks in ge.cpp, display.cpp,
  threads.cpp, interrupts.cpp): the same records from Phobos's HLE in the PSP's time (a call from its start to its
  return, or to its thread going on after a wait; the callbacks as they run; the vblanks), plus the model's charges
  (kind 64: pixels, primitives, cycles, the GE's done time). Off with no profile: a pointer tested at each place, about
  ten instructions a profiled call (five rounds a side, the emulation thread's instructions a frame: coo-diff 101.625 M
  on master, 101.555 M on the branch, −0.07%; smba-story 416.008 M, 415.874 M, −0.03%; within the rounds' spread).
  The runner: `--ge-profile FILE`, `--ge-profile-from F`
  (tools/psp-runner/README.md). The decoder: the charges per list and over the window, `--counts`, the same tables
  for both.
- **Phobos's tables** (the six scenes from their states, software renderer, 10 s): none waits for the vblank; all draw
  as fast as the model's GE allows, busy 86-100%: coo-diff 159.5 frames a second (2.66 a vblank), gos-menu 247 (4.1),
  smba-story 435 (7.3), kz-gp 258 (4.3; one frame buffer), a2-gp 253 (4.2), ahr-legal's copyright screen 851 (14).
  Every charge set by its pixels.
- **The comparison** (the dry runs, then the owner's captures of all six scenes' games; Army of Two's of other
  moments than its state's): each game's loop is the same on both (Army of Two's but for the vblank wait the PSP's
  next-vblank flip brings, #271), but a PSP's GE takes 4.4 to 12 times what the model charges for a list (coo-diff
  27.6 ms against 6.24; gos-menu 29.6 against 4.05; smba-story 23.6 against 1.98; Killzone's scene list 24.3 or 27.3
  against 3.57; Army of Two 31.9 or 32.7 against 3.49; Activision Hits' copyright screen 4.46 against 0.59), so the
  PSP draws 0.4 to 1.8 frames a vblank where Phobos draws 2.7 to 14. A first fit over three scenes: 0.875 bus clocks
  a pixel and 104 a primitive (the model's peaks: 0.25 and 4.74), within 11% for those three. In the scenes that
  sync, the PSP's CPU part of a loop is 7 to 9 times Phobos's; it paces none of them (Killzone's GE is busy 89 and
  98% of its windows).
- **The review's fix round**: the decoder ends a FINISH's list at the queue's head (the driver is first in, first
  out; Killzone's FINISH can come from below its list, which the address matching misread), counts the lists queued
  before a capture by where their FINISHes fall, counts a one-buffer game's frames by its calls, and files interrupt
  handlers' calls under `interrupt`: of the owner's twelve captures and Phobos's six profiles, only Killzone's
  figures moved (above; Phobos's profile now has every list charged and 59.9 frames shown a second). Plugin 0.2.4
  (untested on a PSP, as 0.2.1 to 0.2.3): the captures' 192 KB buffer as the default, the boot capture from 4 s with
  128 KB, its result notice kept up, interrupt-context calls, files opened as new ones and removed if their writing
  fails, the trace capped at 256 KB. The recorder: a wait whose callbacks run first ends where its thread goes on,
  sceGeSetCallback's functions read only as the function reads them, a profile ends at a program's restart, the PSP's
  hooks word, no end of 0. The runner: `--ge-profile-from` alone is an error, a WAV failure no longer skips the
  profile.
- **Tests**: tests/psp 579 groups, 0 failures, sanitized (container overflows not checked, as part 101 ran it: the
  movie player's file read trips that check), with the new "ge profile" (the calls, the wait to its
  thread going on, the FINISH's callback, the vblank and the model's charge, record by record; the file byte by
  byte; nothing recorded without a profile, the machine ending the same to the cycle, its registers, memory and
  state; a state loaded mid-callback; sceGeSetCallback with no slot free), "ge profile: calls whose callbacks run in
  them" and "ge profile: a callback notified in the wait" (the review's), and the loadExec test's profile ending;
  three mutants of the recorder caught; the plugin's and the load test's host tests (a boot capture's notice through
  the control loop, interrupt-context calls, a failed write, the trace's cap); the decoder's 23.
- **Left**: geTime() unchanged here. Part 76's model, fitted to round 6's timings on this PSP and left unmerged, is
  the starting point: a design (that model checked against these captures, the GE's sync calls skipped through when a
  game polls them, a round 8 for what round 6 left) waits for the owner's approval. The CPU's 7 to 9 times is a
  question of its own.

## README: a development progress section, generated from docs/progress/data.json — 2026-10-17

Branch `cursor/readme-progress-2b67`, on master `5442acb8b` (part 102, #265); PR #274. Documentation and the script
that generates it: nothing the app, the cores or the desktop program build changes.
- **What**: README.md's new last section, "Development progress", between `<!-- progress:begin -->` and
  `<!-- progress:end -->`: four headline numbers, three charts (the 266-game library runs, the 22 slow games at full
  speed by build, pull requests opened per day), a four-period Mermaid `timeline` and the PSP releases. The full
  story is docs/progress.md: the seven eras (in two timelines) and their milestones, the firsts, all twelve charts (the
  5x runs on the RP6, PPSSPP and Phobos at 5x, the GE's list time on a PSP-1000 against Phobos's model among them),
  every release, and the setbacks with what changed after them. Download no longer names a latest release: it links
  releases/latest, which GitHub keeps on the newest tagged release (nightly pre-releases sit above it on the Releases
  page); v2.0.0 was the first with the PSP.
- **How**: docs/progress/data.json holds the curated data, each chart with its source note; tools/progress/render.py
  (Python 3's standard library only) writes docs/images/progress/<id>-light.svg and -dark.svg (for `<picture>` and
  prefers-color-scheme, about 880x360, a system font stack), docs/progress.md and the README section, replacing only
  what's between the markers. The same data gives the same bytes. `--check` writes nothing and exits 1 when a file
  differs from the data or a chart no longer comes from it. Headlines, alt texts and a few milestones name a chart's
  numbers (`{chart:series:selector}`) rather than copying them, so they can't drift from the charts. The timelines
  carry a Mermaid theme (dark fills, white text, gray lines) that reads alike on GitHub's light and dark pages, at
  most four periods each, since GitHub shrinks a wider one, text and all. For each release: update the data, run the
  script, commit the result (docs/development-process.md says so).
- **Sources**: the numbers are the ones docs/psp-core.md, docs/psp-compatibility.md, tools/psp-measure/README.md and
  the pull requests already report, and GitHub's pull request and release data (to 17 Oct, 2:38 PM). Private details
  are left out: no device serial numbers, local paths, session IDs, personal notes, or where any game came from.
- **Checks**: the script run twice, every output byte-identical (SHA-256); `--check` passes, and fails on a changed
  page and on a stray chart; all 24 SVGs rendered to PNG with headless Chrome, light and dark, and looked at (titles,
  axis labels, legends, captions; nothing clipped or overlapping); the timelines in plain `timeline` syntax (the script
  refuses `:`, `#` and `;` in their text) and rendered with Mermaid 11 and 10 on white and near-black pages;
  `git diff --check`; no line of the script over 120 characters; a privacy scan of every new and changed line. After
  the push (`3da67b189`), the branch's README and docs/progress.md screenshotted on GitHub in its light and dark
  themes with headless Chrome: each chart showed its matching variant, and all three timelines read in large white
  text on dark boxes, their gray axes visible on both.
- **Review**: an independent review of `a22b56455` passed the script, its determinism, the rendering (GitHub's light
  and dark themes and Mermaid included) and privacy, and checked every chart series and over 40 dates against their
  sources, but found five wrong public statements, all corrected: the Download line's "newest at the top" (nightlies
  sit above the latest release); 29 Sep's decision (the PSP went on the plan through PPSSPP, docs/new-systems.md; a
  core of Phobos's own was decided on 3 Oct, now that milestone's); two "firsts" that weren't (round 6 had timed test
  lists on the same PSP, so 16 Oct is the first timing of games' own lists; part 43 had run a scratch runner from adb
  on the RP6, so #255 is no first and its milestone says what it added, the runner built for Android); and #256's Hot
  Wheels figure (4.2%: 48.58 to 46.52 ms; 3.5% was Killzone's row). Its should-fixes, taken: the headlines dated and
  their methods said (the 11 Oct run; 150-second scripted runs; the 5x pass's two-minute runs from launch, mostly
  logos, intros and menus, on Turnip), "first code" for "first instruction"; the drivers in the PPSSPP chart (PPSSPP on
  Qualcomm's, Phobos on Turnip, with part 102's 2.7 against 12.9 fps on one scene); parts 51, 64 and 72 in the test
  chart, which now ends at master's 576 (draft #272's 595 in the caption), and draft #266 named as a draft. Its nits,
  taken: the README intro no longer repeats the first headline; the library headline agrees with the PSP section (the
  last game plays its opening a few seconds past the minute); Midnight Club 3's 24.2 to 32.3 fps credited to part 43's
  drawing off the emulation thread; the part count's note (parts 2 and 4 are under "The recompiler"); v2.0.0's #210
  and #211; era 1 ending 3 Oct; #265's 19.4 fps named as its final build's (the 19.3 is an experimental build's); the
  Mermaid theme; two lines over 120 characters. A CI lane for `--check` is left for a follow-up.

## PSP Vulkan at 5x: the depth-writing shader's depth declared never above the depth rasterized; another frame buffer's depth taken image to image where the device's copy leaves the stencil (Activision Hits' copyright screen at 5x on the RP6 2.7 → 19.4 fps) — 2026-10-16

Branch `cursor/psp-hires-2b67`, on master `a8d5640f5` (part 101, #263), master `7023fb080` (#264 and the PGD layer's
comments) merged in; draft PR #265. docs/psp-core.md, part 102, has the evidence. The nightly sweep's 5x collapse on the
RP6 (Activision Hits 57.6 → 2.9 fps, Chains of Olympus 59.6 → 9.8, Ghost of Sparta 36.8 → 10.8, Killzone 59.7 → 24.0),
found out game by game: GPU-bound everywhere, no CPU cause. Clean room: no PPSSPP for GE/GPU code, JPCSP not read. Save
states unchanged.
- **What grows**: the scene passes' fragments. Every depth-tested pipeline takes `draw.frag`'s `DEPTHS` build, which
  writes the GE's whole number as gl_FragDepth, so every hidden pixel is shaded (without it, timing only: Chains of
  Olympus at 5x +100% on the RP6, Army of Two +26%, Ghost of Sparta +20%). Activision Hits' copyright screen: 28
  whole-screen depth copies a frame through the staging buffer, which Turnip does slowly (331 ms a frame at 5x).
- **The changes** (vulkan.cpp, gpu.cpp, gpu.hpp, draw.frag, draw.vert, transform.vert): `DEPTHS` declares `depth_less`
  where the device clamps depth and the PRIM's corners' depths are in 0-65535 (3D's are held): those pipelines rasterize
  the depth a step and a half above the GE's ((z + 1.5) / 65536.5), the whole number written a step below it at least
  while the GPU's two interpolations part by less than a step. A 2D PRIM with a corner past 0-65535
  (`Pipeline::outside`, per primitive in `emit()`) and every pipeline of a device without clamping take the build
  undeclared, rasterized at the GE's: master's fragment SPIR-V word for word (the `DepthLess` mode taken out at start,
  `GPU::undeclared()`), the vertex shaders with `ABOVE` off (master's arithmetic, not master's SPIR-V); devices without
  clamping left out by caution (the margin would fit). Metal takes the declaration, Turnip in part (Super Monkey Ball's
  greater-or-equal scene: its passes 63.2 → 59.3 ms at 5x), Qualcomm's driver not. `takeDepth()`'s depth from image to
  image (`vkCmdCopyImage`, the depth aspect alone) where `depthAloneProbe()`, as the backend starts, finds the device's
  such copy leaves the stencil: a whole copy and an unaligned 45x27 part of pictures written from a buffer, every depth
  bit for bit, every stencil, inside and outside (Turnip's and Qualcomm's driver's do, with the whole copy and with the
  part: the fix round's build said "image copy 1" in every run on both). MoltenVK's and lavapipe's take it too (CI's
  lavapipe lanes caught it on the first push), so they keep the staging buffer.
- **Review round** (independent review, changes requested; 77143a9ff and 47e0c5d74): B1 the declaration false for 2D
  float depths near ten million (the review's quad lost 1 of 384 pixels at 1x on MoltenVK): those take the undeclared
  pipelines; B2 no clamping, no margin: no declaration there; S1 the image copy forced in gpuDepthOrders where the probe
  says no, its aspects counted; S2 `Backend::depthBuild()`, the declaration and the margin pinned together; S3 the
  probe's timeout as `wait()`'s; S4 the probe's partial, unaligned copy; S5/S6 the docs (MoltenVK's effect of `ABOVE` on
  2D depths outside 0-65535: those draws are master's now). Every one of the review's mutations, and the fix's own,
  fails a committed test now. Second round: approved; its should-fix, `emit()`'s test of every corner on either side of
  the range, pinned by two more pretend-backend keys (corners below 0 unseen, or only the first corner looked at, fail
  gpuDepthOutside now); its nits in the docs (the margin's size, which the M1 can't show; the hashes not reaching the
  outside pipelines, which the library check does) and the probe's device-lost return freeing its pictures and buffers.
- **Left out**: a "never below" build for less-or-equal tests (Chains of Olympus' and most scenes'): on MoltenVK a
  corner's depth rasterized below 0 moves the depth interpolated beside it (found by the new test), and held at 0
  the declaration isn't true; exact on any GPU only with a depth encoding with room below 0 (part 102's Left). Render
  areas over what a pass draws: the RP6 +0-1%.
- **Exact**: the 21 kit scenes and five more, 100 frames, pictures, VRAM, RAM and state: master's at 1x in all three
  renderers and at 5x (with Activision Hits' copyright screen) in both Vulkan modes, on 3c20bdbf5 and again on the fix
  round. Tests: gpuDepthEarly (the declarations; nothing rejected first that passes after; 2D, 2D past 65535, 3D with
  the depth range either way); gpuDepthOutside (the review's quad: its keys, and drawn twice at 1x-5x); gpuDepthBuilds
  (declaration and margin together exactly where they belong; renderers without clamping); gpuSharedDepth and
  gpuDepthOrders force the image copy where the probe says no (the stencil taken; swapped run offsets fail it), every
  copy of the depth's aspect alone. tests/psp 576 groups, 0 failures. On the RP6 (the image copy) the states saved at
  frame 120 at 5x by master's build and 3c20bdbf5's are the same file for Activision Hits, Chains of Olympus and Super
  Monkey Ball; the coordinator's targeted check of 3c20bdbf5's image copy over the library (237 pairs, from boot at 2x
  and 5x, Turnip, 18:50-20:28) found all 237 the same file, "image copy 1" in every run. The fix round's build
  (77143a9ff): the six scenes' states at frame 150 at 5x master's on Turnip and on Qualcomm's driver, and the targeted
  check's first 20 pairs 20 the same, "image copy 1" in every run on both drivers (the review's second-round should-fix
  met). The hashes don't reach the outside pipelines (no kit scene draws one); the coordinator's Vulkan library check
  does.
- **Numbers**: RP6, the fix round's build against master (the coordinator's run of `hires-fix-rp6.sh`, best of two
  rounds; the table in part 102; the first build's run the same at 5x): Turnip at 5x Activision Hits' copyright
  2.7 → 19.4 fps (1x 15.4 → 247.0), Super Monkey Ball +18%, Army of Two +6%, Ghost of Sparta +4%, Chains of Olympus +3%,
  Killzone 0; Qualcomm's driver at 5x Activision Hits +29%, Super Monkey Ball +6%, Army of Two +4%, Ghost of Sparta +2%,
  Chains of Olympus +1%, Killzone 0; at 1x on Turnip everything but Activision Hits within 1%. For scale: PPSSPP 1.20.4
  runs the 5x sweep's 11 slowest games at 59.8-60.0 fps at 5x on the same RP6 (the gap is Phobos's). Mac (MoltenVK: the
  staging buffer stays, only the declaration differs; best of three rounds in a window kept free of other sessions' runs
  and builds, loads 3.3-8.8): Super Monkey Ball at 5x 14.04 → 14.97 fps (+6.6%, GPU 68.9 → 65.4 ms a frame); Chains of
  Olympus, Ghost of Sparta and Activision Hits within 1% at 1x, 2x and 5x.
- **Next**: the shader's depth write on Adreno (exact designs in part 102's Left: flat draws without it, a
  raster-ordered early discard, Turnip doing more with the declaration); Chains of Olympus' 2-3 scenes a vblank are
  ours (geTime()'s peak rates, a lower bound; the owner's PSP-1000 draws its menus about 0.6 times a vblank, his
  captures of the scene itself to come): GE timing from a round of measurements on his PSP.
- **RP6 commands** (the coordinator's; the agent never runs adb). The fix round's builds against master (master's copied
  from `rp6/bin`, the fix's `hiresfix-77143a9ff-modern`), the speed table and the frame-150 states at 5x on Turnip and
  Qualcomm's driver: `cd ~/phobos-work/scratch/psp-hires/rp6b && QUALCOMM=1 STATES=1 ./hires-fix-rp6.sh` (about 50
  minutes; `hires-fix-rp6-table.txt`, its "driver of fix" lines giving "image copy" and the depth format;
  `hires-fix-rp6-states.txt`); the targeted image-copy check on the fix build, the six scenes and the 8 heaviest games:
  `./hires-takes-fix-rp6.sh` there (about 30 minutes; all 237 pairs with `LIST=../rp6/hires-takes-games.txt`, about 2.5
  hours). Both run 20:28-21:21, results above. Before the fix round (run 14:55-15:20, all 60 runs exit 0):
  `cd ~/phobos-work/scratch/psp-hires/rp6 && QUALCOMM=1 STATES=1 ./hires-pr-rp6.sh` (about 25 minutes: master's and this
  branch's measurement builds, `hiresprobe-a8d5640f5-modern` and `hiresprobe-3c20bdbf5-modern`, six scenes at 1x and 5x
  on Turnip, two rounds, Qualcomm's driver at 5x; `hires-pr-rp6-table.txt`, its "driver of pr" lines saying "image copy
  1" where the probe took the copy; `hires-pr-rp6-states.txt`, three scenes' states at frame 120 at 5x, "same" each).
  Qualcomm's driver with plain builds: `cd ~/phobos-work/scratch/runner-android && ./rp6-compare.sh --rounds 3 --label
  hires-pr-ahr5x a8d5640f5 3c20bdbf5 -- "Activision Hits Remixed" --frames 300 --renderer Vulkan-fast --late-frames
  --resolution 5 --load-state ~/phobos-work/scratch/psp-hires/scenes/ahr-legal/state` (and the same for coo-diff, its
  state in `~/phobos-work/scratch/cpu-round3/scenes`).

## Android input: Colin McRae's dropped held Cross on the RP6 — the app's input paths replayed and cleared, an opt-in input log for the device capture that decides it — 2026-10-16

Branch `cursor/held-button-2b67`, on master `a8d5640f5` (part 101, #263); draft PR #264. Not the fix: what's left
needs the RP6.
- **The bug** (owner, 15 Oct): in Colin McRae Rally 2005 Plus (PSP core), holding Cross to accelerate while steering
  with the left stick and pressing the D-pad, Square and L or R, the game sometimes stops seeing Cross until it's let
  go of and pressed again. Not noticed in other cores; not ruled out.
- **The core** (the session before this one, with a local trace patch kept at
  `~/phobos-work/scratch/held-button/psp-ctrl-trace.patch`): the game reads only sceCtrlPeekBufferPositive(count 1),
  once a frame, the newest sample, the front end's state that frame; scripted runs (Cross held 2,000 frames, the
  stick swept, Square, L, R and the D-pad tapped across it, a one-frame drop, a 70-frame release) show it follows
  Cross's level each frame. So the front end sends Cross released while it's held.
- **The app** (this entry): every writer of the buttons sent to the core read. With Cross bound to a key (the RP6's
  pad is an "Xbox Wireless Controller" in its Xbox layout: Cross is the bottom button, BUTTON_A, the A bit), only a
  key event for Cross's own key code that isn't a press (`setButton`), `releaseAllButtons()` (the game screen losing
  Compose focus, a pause, the activity's onPause among them, a PSP dialog) or `reset()` clears it. The axis loop
  rewrites only bits bound to an axis, the hat only the D-pad bits it set; a hotkey runs only on exactly the keys
  held, so none runs with Cross held unless it holds Cross; nothing in the game's screen asks for focus but itself
  and the PSP dialogs. Compose 1.7.6 (bytecode read) drops a key event while a focus invalidation is pending
  ("FocusRelatedWarning: Dispatching key event while focus system is invalidated." on System.out); those are
  scheduled and run within a composition's apply. A dropped event never lets go of a held key, but a dropped
  re-press (Cross lifted for a moment) looks to the player just like the report.
- **Replay** (`HeldCrossReplayTest`): the game screen's key handler, its hat listener for hotkeys and its focus hook
  moved, unchanged, from EmulatorScreen into `GameKeys` (input/GameKeys.kt), which the screen and the test both call;
  `GameInputState` sends through `coreInput` and takes moves through `onJoystickMove` (interfaces with primitive
  parameters, one reused reader for the event: the seams allocate nothing with the log off; master's own allocations
  on the path stay, the capture check's bound reference per move in MainActivity and the axis loop's boxed keys). The
  test checks the buttons and the sticks sent with every move. Cross stays in every state sent through 20 stages of 3,000 joystick
  moves (the stick swept, the hat in its eight directions, Square, L1 and R1 tapped and held, the triggers, Android's
  repeats of Cross), with the bindings the RP6 captures for its triggers and hat, with L and R moved to the triggers,
  with each PSP Right Stick choice and with focus moving inside the screen; hotkeys on Select and the Xbox button run
  without Cross held and don't with it. A let-go of everything, a release of Cross alone or focus leaving the screen
  gives the report's symptom exactly. Mutations, each alone, each caught: a key repeat releasing its key, focus
  inside the screen releasing everything, hotkeys matching a subset of the keys held, `push()` skipping updates
  whose buttons didn't change.
- **Left, for the RP6**: Android cancelling the held key (a KeyUp with FLAG_CANCELED: InputReader's buffer overrun or
  a device reset, the window losing focus), an activity pause and resume, a lost re-press, the pad's own firmware
  letting go of Cross (code 0x130, which getevent prints as BTN_GAMEPAD), or the owner's bindings or hotkeys.
  `InputLog` (input/InputLog.kt) tells them apart: `adb shell setprop log.tag.PhobosInput DEBUG`, then start the
  game (it's read as a game starts); it logs each key event the window and the game's handler get (flags, scan code,
  device, down and event times), each change of the buttons sent to the core, a let-go of everything with its caller
  and each reset, the window's, the activity's and the game screen's focus, and the bindings and hotkeys as the game
  starts. The capture steps, the reading guide and a safe install (never uninstall: it deletes saves, states and
  settings; check the installed versionCode, `-PversionCode` if it's higher) are in PR #264's description.
- **Review** (of `acd317dd9`): approve with should-fixes, no blockers: the tests ran a copy of the key handler, the
  hotkey test couldn't fail, nothing checked the sticks sent, two values in the reading guide were wrong and it missed
  a lost re-press, the install step could be refused (and the obvious way past it deletes data), and the joystick
  path boxed. All taken, with the nits on `coreInput`'s restore, `reset()` and the key lines.
- **Checks**: the JVM unit tests on the Mac: input 45/0, the suite 350/0 in each flavor (48 classes); CI runs both.

## PSP NPDRM: PGD files on the memory stick — a game's installed data read through the PGD layer (Gunhound EX, 7th Dragon 2020); a wrong key, a damaged header and a write refused as the PGD layer refuses them — 2026-10-15

Branch `cursor/psp-pgd-stick-2b67`, on master `6ff82f9f1` (part 96, #258), masters `462f8ff2f` (part 99, #261) and
`1c75c5600` (part 100, #262) merged in for the review's round; draft PR #263. docs/psp-core.md, part 101, has the
evidence; part 99's "Left" item, the PGD layer on stick files, is this one. The first work in the PGD/NPDRM code
since the owner opened it to agents (the standing note above). Clean room: tpu's decompilation of the firmware's PGD
layer (iofilemgr_dnas), uOFW's names, the games; PPSSPP's sceIo.cpp only as a description; JPCSP not read. Save states
unchanged.
- **The bug** (#261's install work): Gunhound EX's 797 installed PGD files and 7th Dragon 2020's installed GAME.DNS,
  opened with 0x40000000 and given their keys (ioctl 0x04100001), were refused with 0x8001b000 (io.cpp took no
  request on a file off the disc): Gunhound EX loaded from the disc, and 7th Dragon 2020 said "The installed data
  could not be read" and turned its install off.
- **What a PSP does**: its PGD layer takes any file opened with 0x40000000, on any device; its open refuses one opened
  so to write (0x80510201); its key answers 0x80510204 for a header cut short, damaged or not a PGD's, and 0x80510206
  for a key the header's MAC refuses (no key recovered); 0x04100010 gives the data's size, 0x80510206 with no PGD open.
- **The change** (io.cpp, pgd.cpp, pgd.hpp, kernel.hpp): a stick file opened with 0x40000000 takes the PGD layer's
  requests as a disc file does; the key checks the header at once on either device (`pgdUnlock()`: 0x80510204,
  0x80510206, the file left locked; pgd.cpp's key recovery only where no key is checked, since the review's round
  sceNpDrmEdataSetupKey's EDATA alone); a file without PGD's magic still reads as it is (0, as the translations
  need); 0x04100010; a file opened with 0x40000000 to write refused, so the PGD path never writes, and since the
  review's round the PGD requests on a descriptor open to write too (an older state's: 0x80510201); 0x04100002 as
  the PGD layer keeps it since the review's round (the same place changes nothing, another leaves no PGD open until
  a key opens one there; it used to mark the file and have its key recovered); and #259's leftover, the disc's seek
  request (0x01010005) in an unlocked PGD's data, its end the data's.
- **Games**: Gunhound EX reads 74 installed files from the stick (83 keys, each 0; 254 reads) and plays Mission 01;
  178 of its stick reads are master's disc reads of the same data, hash for hash, and the disc's own INSDIR copies,
  read through the disc's PGD path, give the stick's bytes, 74 of 74. 7th Dragon 2020 reads GAME.DNS from the stick
  (123 reads), shows no message and reaches the field; 118 of 119 reads are master's disc reads. The sticks stay byte
  for byte as installed. The seven UMD PGD games: six master's, traces line for line; Black Wolves Saga's INSTALL2.DNS
  refuses the game's first key and opens with its second (master's key recovery hid that), every capture master's.
- **Tests**: three new groups (stick PGD files through the PGD layer, their refusals, the disc's seek request) and
  three changed (a fake "\0PGD" now 0x80510204, a wrong key 0x80510206, part 97's EDATA emptied through a plain
  descriptor). On master the six fail, 152 checks. The review's round: two new (where a stick PGD starts, 0x04100002,
  three PGDs in a file; a readied EDATA's PGD start and a wrong key), and the refusals grown (a flagged stick file's
  disc requests, 0x8001b000; an older state's writer, 0x80510201); five mutations each caught (0x04100002 as it was:
  13 checks; the review's one-line fix alone: 7; every request on a flagged stick file: 5; no writer refusal: 3; a
  readied EDATA's key unchecked: 4).
- **Checks**: tests/psp 558/0 sanitized and unsanitized; tests/allegrex 78/0; tests/psp/ares 348/0; the 21 scenes in
  Software, every hash master's; batch 04's saves byte for byte master's. The review's round (`1d62fed2d`, #261 and
  #262 merged in): tests/psp 572/0 sanitized and unsanitized; Gunhound EX and 7th Dragon 2020 installed through
  master's own dialog from power on, on fresh sticks, then booted: the branch reads the installs from the stick (the
  same reads and traces as the first round), master falls back to the disc, the sticks byte for byte as installed;
  Black Wolves Saga the first round's; the 21 scenes in Software, every hash master's (`1c75c5600`).
- **Review** (of `eb48c3626`): approved with two should-fixes, no blockers (a lone 0x04100002 still unlocked a file
  with no key; nothing pinned a flagged stick file's three requests alone), and nits; all taken (above).
- **Left**: a file without PGD's magic answered 0 (the PGD layer says 0x80510204); disc files opened without the flag
  still take the PGD requests; the PGD layer's version, block-size and table-MAC checks, its eight-file limit, and the
  second fixed DNAS key it takes for a file opened with 0x04000000; a second key on an unlocked file; 0x04100002's
  argument read as a pointer (part 97's); the disc's requests on an unlocked PGD see its data (a PSP's PGD layer
  passes them to the disc's driver); Black Wolves Saga's install not run; part 97's EDATA items.

## PSP GE: #253's per-PRIM paths cheaper on the RP6's Cortex-X3 — a PRIM's kept settings compared where they are, fast mode's screen triangles without calls — 2026-10-15

Branch `cursor/psp-ge-round4a-2b67`, on master `cc0638372` (part 97, #259), masters `02cd1fa8c` (part 98, #260),
`6ff82f9f1` (part 96, #258) and `462f8ff2f` (part 99, #261) merged in; draft PR #262. docs/psp-core.md, part 100, has
the evidence. GE round 4, part A; part B (the GE's front end on a thread of its own, its design in
`~/phobos-work/scratch/ge-round4/DESIGN.md`) comes after. Clean room: no PPSSPP code read, JPCSP not read. Nothing of
the PSP's behaviour changes; save states unchanged.
- **The problem** (the coordinator's RP6 bisect): #253 (part 92) took 1.4% of Super Monkey Ball's instructions but
  added 3.3% cycles (Chains of Olympus −1.3%, +1.9%), the X3's frontend stalls 8.3% → 10.1% of its cycles.
- **Why** (master's X3 code, `llvm-objdump`, and the X3's own profile, simpleperf): the kept-settings check built a
  276-byte key on the stack at each PRIM and called bionic's `memcmp` through the PLT, inside a frame of twelve saved
  registers and a stack canary; each screen triangle made four calls (`meshState()`, the models' and indices'
  `vector::__append`, `meshDraw()`): 4% of Monkey Ball's cycles, and their code pushed the rest of the GE out of the
  X3's 64 KB instruction cache (its refills 60% fewer now, frontend stalls 12.6% fewer, most in code this round doesn't
  touch).
- **The change** (gpu.cpp, gpu.hpp): `kept()` compares the key's inputs where they are, word by word, with a branch at
  the end alone, `settings()` the work where they differ; `meshState()`/`meshDraw()` inline where kept and joining;
  the recording's models and indices are `GPU::Pile`s (an append a bound's check and a sum, the room grown out of
  line); `meshDrawable()` answered once for a PRIM's settings while they're kept; #254's blending by the frame
  buffer's alpha decided with the settings. The key starts zeroed: UBSan caught `kept()` reading it unwritten.
- **RP6** (the coordinator's session, 15 Oct, the owner's usual profile, 600 frames, Vulkan fast, three rounds; master
  → branch, CPU ms a frame): Super Monkey Ball −7.6% (cycles −7.9%), Ghost of Sparta −4.4%, Chains of Olympus −2.4%,
  Army of Two −1.0%, Killzone +0.2% (its runs overlap). The Mac: instructions −4.6%, −2.9%, −2.4%, −0.7%, −0.2%.
- **Diagnostics** (same session): settings never kept cost Monkey Ball 6.5% on the X3 (part 92's keeping is a win, its
  check's form wasn't); screen meshes off, on the branch: Monkey Ball −1.4%, Chains of Olympus −3.8%, Army of Two
  −0.5%, Ghost of Sparta +2.7%, Killzone +1.9%: kept on. What they still cost is the driver's and the GPU's (more page
  faults, longer waits for the GPU, backend stalls), not their recording: next, `rp6-profile.sh` on Chains of Olympus
  and Army of Two with and without them (page faults by call stack), and `--late-frames`.
- **Review** (independent, of `c2dc921fa`: approve with should-fixes, no blockers), taken in `3e819d440`: the
  review's test of the alpha caught up on a PRIM whose settings are kept; `gpuKeptInputs` (each of `kept()`'s inputs
  changed alone; each term left out alone fails it, but those that go with another input: part 100's table);
  `gpuMeshRefused` (a pipeline the driver won't make, after a mesh); KeptSettings' size and last place pinned;
  `GPU::Run` named `GPU::Pile` (#258's runs are depth runs). Master `6ff82f9f1` (#258) merged: gpu.hpp and this top
  met, nothing else; then `462f8ff2f` (#261): the docs alone.
- **Review round's checks** (`3e819d440` against master `6ff82f9f1`): settled hashes 93 of 93 the same (the 21 kit
  scenes, #258's Kurohyou 1 and 2, WipEout and IL-2, Chains of Olympus, Killzone, Hammerin' Hero, Jeanne d'Arc, Cars,
  MX, in the three renderers); tests/psp 557/0 plain and sanitized; the Mac's bench: Monkey Ball −4.6% instructions,
  Ghost of Sparta −2.9%, as before.
- **First round's checks**: the merged head's settled hashes against master `02cd1fa8c`, 75 of 75 the same (the 21 kit
  scenes, Hammerin' Hero, Jeanne d'Arc, Cars, MX, in the three renderers); tests/psp 550/0 sanitized and unsanitized
  (the Vulkan groups run), tests/allegrex 78/0, tests/psp/ares 348/0; CI: the draft PR's checks.

## PSP: the game data install dialog — the disc's INSDIR copied to the memory stick through a folder of its own; sceIoChstat (part 99) — 2026-10-15

Branch `cursor/psp-gamedata-install-2b67`, on master `cc0638372` (part 97, #259), master `6ff82f9f1` (parts 96 and 98)
merged in; draft PR #261. docs/psp-core.md, part 99, has the evidence. Clean room: pspautotests'
utility/dialog/sizes, io/stat and io/stat/readonly; players' reports of Kingdom Hearts' install failing on PSPs
(80111904); the games' code and
discs; PPSSPP's gamedata install dialog, error codes and sceIoChstat notes read only as a description; JPCSP not read.
Save states unchanged (layout 32: the copy isn't state).
- **The bug** (deep play batch 04's bug 3): Kingdom Hearts Birth by Sleep Final Mix's DATA INSTALL ended in "An error
  has occurred", and Gunhound EX's was cancelled: part 37 had made sceUtilityGamedataInstall a dialog that's cancelled.
  Behind it, Kingdom Hearts' own copy of BBS1.DAT calls sceIoChstat, which was missing (the 80111904 players saw on bad
  memory sticks is that path's message).
- **What it does now** (`install.cpp`): each Update copies 1 MiB of the disc's PSP_GAME/INSDIR (from the image or a
  host folder standing for the disc) into `<stick>/.phobos-install/<game><data>`, writes the progress (at 1372), and at
  the end writes PARAM.SFO (the request's titles; a PARAM.SFO there keeps its other keys) and renames the copy into
  PSP/SAVEDATA/<game><data>, or moves its files into the folder there, PARAM.SFO last. It writes over only what a PSP's
  dialog would: INSDIR's files and PARAM.SFO in that folder. Phobos stopped part way leaves no half folder: aborted,
  reset or a state loaded, the copy is taken away; killed, the whole `.phobos-install` is emptied at the next power on
  or install start (never following a link out of it). Programs can't see or reach `.phobos-install` (io.cpp's
  `resolve()` and sceIoDopen: any case, trailing dots/spaces, and by file identity, so links and 8.3 names too). The
  copy is checked before each piece (as the install left it) and at the end (every file the disc's length): a copy
  the host changed meanwhile gives 0x80111904, never a short install. Answers: 0; 1 (Abort; a disc with nothing to
  install, as before); 0x80111901 no stick;
  0x80111902 no space (chosen from the codes' pattern; io.cpp's 57,344 free clusters, never filling the host's
  storage, and the host filling during the copy); 0x80111904 the stick unwritable (also chosen for bad names, a
  folder name ending in '.' or ' ', a file in the folder's place, a leftover that can't be removed); 0x80111908 mode
  ≥ 2. The trailing '.'/' ' refusal is the install's alone: savedata's names (the shared `plainName()`) are unchanged.
- **sceIoChstat** (io.cpp, new): a memory stick file's read-only flag as io/stat/readonly recorded (the host file's
  owner's write permission); a folder's answered 0 and changed nowhere, the device's top refused (0x80010016, as
  sceIoGetstat). sceIoGetstat and sceIoDread give a stick file's attributes as io/stat recorded (0x20 a file, 0x10 a
  folder, 0x01 more when a file is read-only), where they had been 0x27 and 0x17.
- **The app**: in mode 1 (the PSP's own progress screen) a request of kind 7, made afresh as the progress moves on and
  taking no answer, shown by `PspInstallDialog` (icon, titles, size, "Installing… N%"; no button). Mode 0 shows
  nothing, as a PSP does (Kingdom Hearts draws its own screen).
- **Games**: Kingdom Hearts installs (INSTALLATION COMPLETE; BBS1.DAT, ICON0.PNG, PIC1.PNG byte for byte the disc's)
  and, booted again, reads BBS1.DAT from the memory stick (41 reads, none from the disc's copy) to its first gameplay.
  Gunhound EX installs all 797 files byte for byte; 7th Dragon 2020 its 1.5 GB. Both then read their install through
  the PGD layer (flag 0x40000000, ioctl 0x04100001), which io.cpp refuses on memory stick files (part 97's "left",
  its own task now), say so at boot and play from the disc. All 30 INSDIR discs in the library import the dialog.
- **Tests**: tests/psp/install.cpp's 9 groups (the whole install in both sizes, a host folder disc, every answer, cut
  short by Abort, a reset or Phobos killed, the copies' folder hidden and links in its place, its copy changed behind
  its back, a state saved part way, over an install there, the progress screen and its byte form) and files.cpp's
  sceIoChstat group; on master the first round's 8 fail, part 37's own group passing on both. Android:
  `PspDialogsTest.readsTheCoresInstallProgress`.
- **Review round** (review of `1fbabdf43`: approve with should-fixes): the leftovers' cleanup at power on and every
  install start; sceIoChstat files only, top refused; the copies' folder hidden; the cleanup's errors checked; names
  ending in '.'/' ' refused; a file in the folder's place found before the copy; a disk filling mid-copy 0x80111902;
  two doc wordings (a third didn't hold: Kingdom Hearts clears its folders' read-only flag only where sceIoGetstat
  says it's set, so with the PSP's 0x10 it changes none). The merge over an install there is kept (PPSSPP's
  description; no clean-room recording covers it). Re-review of that round: approved with one should-fix the first
  round had (the host could turn the copy into a short install), taken in a third round with its optional nit (the
  folder's other names on Windows and FAT).
- **Checks**: tests/psp 556/0 sanitized (`detect_container_overflow=0`) and unsanitized; tests/allegrex 78/0;
  tests/psp/ares 348/0; the runner's test; GCC 13.3 `-Wall -Wextra -Werror` on the changed files; Android 344 unit
  tests, 0 failures. The 21 scenes in Software: every hash master's, the state too. Batch 04's saves (7 games): the same
  files byte for byte and the same savedata requests as master's runner. Nothing run on the RP6. Second round
  (`063633d84`): tests/psp 557/0 sanitized and unsanitized, tests/allegrex 78/0, tests/psp/ares 348/0, the runner's
  test; the round's tests fail on `1fbabdf43` (4 groups, 45 checks) and the other 553 pass; Kingdom Hearts installs
  and boots again on a scratch stick (files the disc's, a planted leftover gone at power on, 41 reads from the stick,
  every frame the first round's); a disk image filled mid-copy answers 0x80111902; the 21 scenes' hashes master's.
  Third round (`d5771d7fb`): tests/psp 558/0 sanitized and unsanitized, tests/allegrex 78/0, tests/psp/ares 348/0;
  the round's tests fail on `4b671947d` (2 groups, 26 checks) and the other 556 pass; Kingdom Hearts' install and
  boot every file and frame the second round's; the 21 scenes' hashes master's. GCC 13.3 (Docker) couldn't be run in
  rounds two and three (Ubuntu's archive unreachable); CI's Linux build compiles with it.
- **Left**: the PGD layer on memory stick files (its own task now: Gunhound EX and 7th Dragon 2020 use their installs once
  it's there; done since, part 101's entry above); the dialog's own screens and timing, the words after the progress,
  and the no-space code aren't recorded; sceIoChstat's times and size aren't changed.

## PSP GE: the depth buffer in the order its frame buffer's format has it — the Kurohyou games' characters at the screen's middle (deep play batch 04, bug 2) — 2026-10-14

Branch `cursor/psp-kurohyou-2b67`, on master at e78b4a364 (#254), master 14b181707 (#256, #257) merged in for the second
round, cc0638372 (#259) for the third (the fourth on it too) and 02cd1fa8c (#260) for the fifth; PR #258.
docs/psp-core.md, part 96, has the evidence; docs/psp-gpu-renderers.md ("Filled from memory", "A depth buffer drawn with
two frame buffers") the GPU's side. Clean room: no PPSSPP code read, JPCSP not read. Save states unchanged (an old
state's 16-bit depth is in the old order: games that clear or redraw their depth each frame, all the kit's and
Kurohyou's, don't notice; one that drew it once and kept it would test against it scrambled until it drew it again).
- **The bug**: in Kurohyou 1 and 2, characters crossing x = 240 lost their left part under Software (a straight edge at
  240) and vanished under Vulkan (both modes); a faint seam in the floors at 240. Not the scissor, region, offsets or
  bounding boxes (none in the frame): the games draw their pre-rendered depth into the depth buffer's memory as a 5650
  frame buffer, take it back through VRAM's second copy and copy it into the depth buffer, which leaves the depth right
  only if a 16-bit frame buffer's depth is kept as the second copy shows it. Phobos kept every depth buffer in the
  fourth copy's order (round 3's measurement, made with an 8888 frame buffer), which scrambled it: depth (x, y) from the
  picture's column rot(x) — two squeezed copies of the street, 240-255 from undrawn columns (0). Kurohyou's depth of
  field reads that depth through the second copy too: its mask broke at 240 (the seam).
- **The rule** (`Memory::depthCopy()`): the GE keeps its depth buffer as VRAM's second copy shows it with a 16-bit frame
  buffer (5650, 5551, 4444), as the fourth does with an 8888 one. Unmeasured; a census of the 128 deep-play states
  agrees game by game, none against: 16-bit games read their depth in order through the second copy (Kurohyou 1 and 2,
  Genso Suikoden, Me & My Katamari, MediEvil's CPU), 8888 ones through the fourth (Cars, IL-2 Sturmovik, Killzone,
  WipEout), and the 8888 ones reading the second copy undo its spread with 16-texel strips (MX vs. ATV Reflex and
  Untamed, Cars Race-O-Rama). All the 16-bit evidence is 5551: 5650 and 4444 follow by pixel size only. Only Twisted
  Metal (a kit scene) draws one depth buffer with both sizes. Queued: round 3's depth-layout case drawn on a PSP with
  5650, 5551 and 4444 frame buffers.
- **Changes**: the software renderer's depth reads and writes (pixel.cpp, four.cpp) by the frame buffer's format; on the
  GPU (gpu.cpp, vulkan.cpp, both modes) the depth filled, watched and put into memory in the target's format's order, a
  finish before a PRIM's depth is filled from memory where rows it draws in come from another target's pixels on the GPU
  (Kurohyou's depth, drawn as colors), and a depth taken across the two sizes placed on the GPU by runs of 16, each
  where the other's order puts the same bytes, the staging buffer holding the target's columns alone (through memory, a
  wait, only where a run's bytes lie past the other's).
- **Second round** (the review's blocker): the first round's finish fired on page ownership alone; IL-2 Sturmovik
  finished 239 times more in 120 frames (601 -> 840 fast, 841 -> 1,080 accurate). Its bloom, drawn each frame in the 16
  KiB of its depth buffer's 273rd row, changes that row for every target over it, and its depth-of-field target and
  clear fill it whole (a trace: the rows filled, not just the pages, still had all 239). The finish now looks at the
  filled rows the PRIM draws in: IL-2 601 and 841 (master's), Kurohyou 1 601 and Kurohyou 2 901 as before, the page
  cache gone. IL-2's row 272 is filled from memory as on master (part 96's "still not done").
- **Third round** (the second review's blocker): the finish still came one to three times a frame in games that draw
  their post-processing into their depth buffer's memory (Brave Story, Brothers in Arms, Burnout Dominator, Chili Con
  Carnage, Crazy Taxi, Dirt 2, both Fate Extras, GTA Chinatown Wars, Jak and Daxter, WipEout Pure), pictures master's,
  fast mode 17-33% slower on the M1. A trace of each: seven clear their depth or write it with a test always true
  (nothing read), Chili's and Dirt 2's 2D sprites at 65535 "greater or equal" pass whatever they find, GTA's radar
  stamp lies in rows 186-262 below the 176 its bloom changed (its scissor said 272). Now the finish comes only where the
  PRIM's test reads the depth filled (not a clear, not always or never true, not decided by its depths), in the rows its
  pixels can be in (a 3D PRIM's vertices put on the screen by the GE, `GE::reachedBy()`, asked through
  `Renderer::begin()` only where its region's rows meet another target's pages). The review's sweep (all 128
  deep-play states, 60 frames fast, master cc0638372 against this): 127 with master's finishes, Kurohyou 2 its needed
  one more a frame (121 -> 181); the ten games' master's (Kurohyou 1's too, 121). WipEout's HUD is tested
  against the bloom over its whole depth buffer, which the PSP reads there (3-27 pixels a frame turn on it): its
  finish a frame stays (part 96's "still not done": the GPU move would take it, and Kurohyou 2's, away).
- **Fourth round** (the RP6's numbers on the third: WipEout's race 62.2 -> 55.6 fps headless, Kurohyou 2 65.8 -> 55.8,
  the finish and the CPU's work after it): the move on the GPU. Depth filled from memory where other targets drew
  those bytes on the GPU since the last finish is made what a finish then the fill would make it, on the GPU
  (`moveIntoDepth()` from `fill()`: a Move into the depth, move.frag's DEPTH build, each depth the two bytes of the
  other's pixel at the depth's bytes in its format's copy order, narrowed as memory would have it), at every such fill,
  so on the GPU's way a later PRIM that finds those rows filled reads them current too (the second review's five
  cases, now tests; not a target's own pixels over its own depth, which stay memory's: part 96's "Still not done");
  round three's rule, `GE::reachedBy()` and `begin()`'s argument went (the first two back for the fallback in the
  fifth round). A PRIM drawing again where a depth took from has that depth filled again (told rectangles too); a
  texture through the depth views finishes first where its decoding would anyway. Where the rows filled take the
  target's own pages as well as another's, a finish first (exact; its own pages alone, memory's depth as it is); where
  the GPU can't move at all, in this round a finish before every such fill. The sweep (the 128
  deep-play states, 60 frames fast): all 128 with master's finishes, Kurohyou 2's too (121; the third round 181). In 300
  frames, both modes: Kurohyou 1 and 2 601 (master's; the third round had Kurohyou 2 at 901), WipEout's race 1
  (master's; the third round 301). Instead, the Kurohyous move pixels into the depth once a frame and WipEout twice.
  Where those moves put pixels in every depth filled (each 16 KiB all another target's drawn full rows), the fill takes
  nothing from memory first (`depthCovered()`), and the moves' stencils go through the staging buffer in one copy: the
  first version's M1 bench showed WipEout's HUD depth filled on the CPU each frame and a copy region a row (MoltenVK
  records each as a blit), 27% more instructions a frame than master. A move's render pass is its rectangle's alone and
  its layout changes three barriers, not nine: GTA's one move a frame had cost 0.3 ms more waiting on the M1.
- **Fifth round** (the fourth's review: no blockers, its should-fixes; the owner's decision): without depth moves (no
  pipeline, or the start-up check's new case fails and it passes again without), round three's rule decides the finish
  again: not a clear's, nor a test always or never true, nor one its depths decide, and in the rows its pixels can be in
  (`GE::reachedBy()` back, which `begin()` asks with the PRIM's vertices and transform, not round three's
  `std::function` a PRIM); the rest is filled from memory as it is, master's accuracy, its stale depth included. The
  fourth round's fallback had been round two's slowdown (the review: Dirt 2 from 2 to 181 finishes in 60 frames, 62.5
  -> 33.8 fps; GTA from 1 to 31; Brothers in Arms from 1 to 70). Now it's round three exactly: the sweep's 128 states
  without moves have round three's pictures, VRAM and RAM in every frame and its finishes (the start-up check's second
  run aside: 3 a process), and so do Kurohyou 1 and 2, WipEout's race and IL-2 in both modes. The start-up log says in
  one line when the moves are off. The review's gap and straddle cases are tests now (83 in the formats' group; the
  straddle two-sided), catching the two mutations that had passed. With the moves on nothing changes: the sweep's
  counts and the hashes are the fourth round's.
- **Pictures**: both games' centre characters whole in Software, Vulkan accurate and fast, and the streets full of the
  pedestrians the scrambled depth had hidden; Vulkan against Software 0.20-0.90 (Kurohyou 1) and 0.35-0.48 (Kurohyou 2)
  at frames 30-300, no pixel more than 24 apart. The second round's 36 captures (both games, frames 30-300, all three
  renderers) are pixel for pixel the first's; the third round's too (Kurohyou 2's fast frame 90 taken a frame late in
  one run: a rerun's the same), and the fourth round's (that frame the rerun's). Sheets (before above after; Software,
  Vulkan accurate, fast): `~/phobos-work/scratch/kurohyou/png/` `pr-k1-30-full.png`, `pr-k1-30-centre.png`,
  `pr-k1-150-centre.png`, `pr-k2-90-full.png`, `pr-k2-90-centre.png`.
- **Hashes**: master 14b181707 against 318a7c5c2 (the kit's 21 scenes, Hammerin' Hero, Jeanne d'Arc, Cars Race-O-Rama,
  MX vs. ATV Untamed, IL-2 Sturmovik's play): Vulkan accurate and fast the same in all 26 (IL-2's pictures master's
  again); Software the same in 19, and in the 7 others (Colin McRae, both Need for Speed Most Wanted scenes, Need for
  Speed Underground Rivals, Disaster Report 3's play, Hammerin' Hero, Jeanne d'Arc: 16-bit with a depth buffer) every
  picture and the RAM the same, VRAM apart only in the depth buffer: all 130,560 drawn depths the same in the second
  copy's order (dumps after frame 100; nothing outside its 16 KiB blocks), the first round's counts exactly. Third
  round, master cc0638372 against d67ef308f's code: the same (Vulkan 26 of 26 in both modes, Software 19 and the same
  7); WipEout's race, added, every picture and the RAM the same in all three renderers, VRAM apart in its small 16-bit
  depth buffer (the same depths reordered, as the 7's) and, in Vulkan, in its bloom's memory, which the HUD's finish
  puts there each frame (after frame 100 nearer the software renderer's than master's: 43,289 bytes apart against
  180,130). Fourth round, master cc0638372 against this: Software the third round's hashes in all 27; Vulkan accurate
  and fast master's in 25 of the 26 and in WipEout's race, VRAM too. IL-2's play is apart (the RAM the same): its 3D
  tests its depth buffer's row 272, in its bloom's 16 KiB, which now holds the bloom. So in fast mode row 0 (the other
  frame buffer's first, which that 3D draws) is the software renderer's: pixels more than 24 apart in 100 frames 1,130
  -> 24, as accurate mode had it on master (23). Rows 8-11 are a level or two nearer it in both modes.
- **Speed**: Kurohyou 2's extra finish a frame is needed and costs: the review's M1 runs 41.7/39.3/31.6 fps on master
  against 33.9/28.2/27.7 (about 5 ms a frame more waiting), the RP6 0.64 ms a frame more GPU waiting (3.90 -> 4.54) and
  2.5% more CPU, still 77 fps headless. This round's M1 runs: about 1 ms a frame more GPU waiting in fast mode
  (4.6/4.0/3.6 -> 5.5/4.9/4.8). The first round's "no change" was wrong. The fourth round's GPU move removes it
  (below). Kurohyou 1: master's finishes (601), 5% less CPU on the RP6. IL-2: master's finishes. Twisted
  Metal: master's finishes; 5.5 million pixels staged in 300 frames (33.8 million in the first round). Third round: the
  ten games' finishes master's, Dirt 2, GTA and Brothers in Arms back at master's speed on the M1 (fast, three pairs in
  turns: within the runs' spread; the second review had them 17-33% slower); WipEout's race keeps a finish a frame (the
  review's M1 runs: fast 103-113 fps on master against 82-85). Fourth round (the M1, fast, 120 frames, three pairs in
  turns): WipEout's race 102.3-107.1 fps on master against 100.9-105.1 (0.45% more instructions; 0.5% in 1,500
  frames), Kurohyou 2 89.5-91.8 against 91.7-92.5 (5.3% fewer instructions); GTA, Dirt 2, IL-2 and Twisted Metal
  within the runs' spread; Kurohyou 1 53.0-54.1 against 50.2-50.6 and Kurohyou 2 accurate 57.8-58.2 against 55.9-56.5,
  1.0 and 0.7 ms more GPU waiting a frame, 0.8 ms of Kurohyou 1's round three's too: the pedestrians drawn. The RP6
  (the owner's profile, master against the fourth round): WipEout's race 70.7 -> 70.6 fps, Kurohyou 2 83.1 -> 82.7,
  Dirt 2 61.8 -> 62.6, GTA 92.9 -> 93.5, IL-2 34.9 -> 34.5, Twisted Metal 176.8 -> 175.9, Kurohyou 1 48.9 -> 47.7; the
  start-up check passed on its Adreno with the moves on. Fifth round without moves: round three's finishes, so its
  speed (Dirt 2, GTA and Brothers in Arms at master's finishes; WipEout's race and Kurohyou 2 one more a frame, the
  RP6's 62.2 -> 55.6 and 65.8 -> 55.8 on the third round).
- **Tests**: three groups (draw.cpp: the depth in each format's order through each copy, Kurohyou's round trip in
  miniature; gpu.cpp: the same on the GPU over two frames, the second taking its depth from an 8888 frame buffer whose
  rows the picture changed, a 5551 depth read as a texture, a depth buffer drawn by 8888 and 5551 frame buffers, 1x and
  2x, both modes, one depth-fill finish a frame counted), all failing on master's core; measure.cpp checks round 3's
  depth-layout files against the PSP's; the start-up check tests a sprite against a depth taken across the formats. On
  318a7c5c2: tests/psp 546 groups, 0 failures, sanitized (the container check off) and not, none skipped; tests/allegrex
  78, 0; tests/psp/ares 348, 0; CI green. Mutations: the finish taken out fails both frames, the changed rows not looked
  at fails the second. Third round: a gpu.cpp group for the rule (a clear and a test always true over the picture, no
  finish; sprites at 65535 and 65534, none and one; 3D quads below the picture's rows and over them, none and one;
  another picture, the screen's own changed rows alone finding it, two), 1x and 2x, both modes. On d67ef308f: tests/psp
  552 groups, 0 failures, sanitized and not, none skipped; tests/allegrex 78, 0; tests/psp/ares 348, 0; CI green.
  Mutations: each part of the rule taken out or loosened fails the new group alone (the own changed rows not looked at:
  5,187 bytes apart, one finish of two); the taken target's changed rows taken out, all pass. Fourth round, every group
  1x and 2x (or to 3x), both modes, moved and with the move off: the rule's group replaced by depth filled as a finish
  would leave it for every PRIM after (the second review's five cases among them, failing on d67ef308f: 4,688, 9,376,
  4,720, 4,720 and 9,376 bytes), other frame buffers' pixels as the depth in every pair of formats (80 cases: places,
  shapes, the CPU's bytes over them; every depth and VRAM byte the software renderer's), the formats' group both ways,
  and the start-up check's new case. On e78ccb495: tests/psp 553 groups, 0 failures, sanitized and not, none skipped;
  tests/allegrex 78, 0; tests/psp/ares 348, 0; CI green. Mutations: 17, 13 failing (nothing moved fails all four, the
  start-up check too), 4 passing by design (part 96). Fifth round: the review's gap and straddle cases in the formats'
  group (83 cases); the depth-current group's way without moves has round three's finishes (one, one and two in three
  cases, none in the rest), exact where round three is. On the merge of master's 02cd1fa8c: tests/psp 555, 0
  without the sanitizers and sanitized (run again: the first lost the Mac's GPU under load, a wait past five
  seconds); tests/allegrex 78, 0; tests/psp/ares 348, 0. With `begin()` taking the PRIM's vertices: tests/psp 555,
  0 both ways, and three scenes' hashes with the moves round four's in both Vulkan modes (God of War's menu 0.13%
  fewer instructions a frame than with the `std::function`: two fifths of round five's 0.3% over round four); CI on
  the pushed head. The library check in Vulkan of the
  fourth round's head (its way the fifth's with the moves): none apart from master's in every run, the only flag
  Monster Hunter 3rd HD's intro movie a frame apart in 3 of 5 runs.

## PSP: ticks as RFC 2822 and RFC 3339 text (MediEvil's save details), a list save to its list's names (Lumines II), the depth-view loads' test deterministic — 2026-10-15

Branch `cursor/psp-small-fixes-2b67`, on master `cc0638372` (part 97, #259); draft PR #260. docs/psp-core.md,
part 98, has the evidence (part 96 is #258's, still open). Clean room: pspautotests' `rtc/convert` (recorded on a
PSP) and uOFW's decompilation of the rtc module; PPSSPP's sceRtc only as a design cross-check; Vita3K's SceRtc read
(its four RFC functions empty stubs); JPCSP not read. Save states unchanged.
- **MediEvil Resurrection's blank save details** (deep play batch 04, bug 4): sceRtcFormatRFC2822LocalTime and the
  rest of its family (sceRtcFormatRFC2822, sceRtcFormatRFC3339, sceRtcFormatRFC3339LocalTime) weren't there. Now as
  `rtc/convert` recorded them on a PSP (`Mon, 30 Jan 2012 02:12:15 +0000`, `2012-01-30T02:12:15.00Z`; and its first
  version's four zone-256 calls, rebuilt as the chain they were: the zone as `+0416` and `+04:16`, the hundredths
  cut, the day in two digits and the year in four characters, years past 9999 too, which four not recorded), the
  tick microseconds since 0001-01-01; the LocalTime forms with the zone the settings give (`localZone()`, also
  behind sceUtilityGetSystemParamInt and sceRtcConvertUtcToLocalTime now), the kernel's addresses refused as uOFW's
  rtc refuses them. MediEvil's SAVEDATA_DETAIL `""` -> `Wed, 14 Oct 2026 05:46:56 -0400`; Cube's detail's second
  line, a stray byte, now its date.
- **Lumines II's list save** (bug 5): the automatic answer kept a save name the list doesn't hold (`GAME` beside
  GAME01-GAME03) and made `ULUS10183GAME` on a fresh stick, where a PSP's dialog shows and saves to the list's names
  alone. Such a name now names none where its save isn't on the stick, and the list's first name is taken (GAME01),
  or for focus 7 or 8 the new slot the dialog focuses; where that slot is there (every slot taken, or the first for
  the other focuses), or its own save is (an upgraded player's GAME), its own name is kept, as on master: no save is
  written over, and Phobos never makes a second folder.
- **The review** (of `2a45e6531`, changes requested): the first round redirected even onto saves that were there,
  so (A) an upgraded player got GAME01 beside GAME, and a stale GAME dated later was loaded and its next save sent
  over GAME01, and (B) a new profile after a broken GAME01 (focus 7) went over GAME01's intact data. Both fixed as
  above, both in the test, both walked with the game on scratch sticks. Its should-fix (the year's four characters)
  and nits (recorded, not chosen: the hundredths cut, the day's two digits; the four old lines quoted; Vita3K named;
  `localZone()` for the system parameters) taken. The re-review (of `bac0374f3`): both fixed, the year's change safe;
  one overwrite left, every slot taken (focus 7 or 8 falling back to GAME01) or GAME01 there for focus 0-6: now the
  redirect never lands on a slot that's there (`ce436b2e7`), tested and walked with the game.
- **The flaky test** (`ge loads through the depth buffer's views wait for the threads`, which failed on loaded Macs in
  the colinsave, npdrm-modules, round6 and cpu-round3 runs): `GE::Drawing::held` keeps the drawing threads from taking
  a batch; the test holds them till it has seen the batch still drawn, then lets them go as the loads come, or holds
  them through the loads, which then wait for the batch and draw it themselves. With a 200 ms sleep after the list,
  master's test fails 4 checks; the branch's passes.
- **Tests**: two new groups (the RTC texts; Lumines II's list saves, with the review's two cases) and the depth-view
  loads' group changed. On master they fail 125 and 11 checks, on the first round's head 5 and
  13 (the year's fifth character; the overwrites), on the second round's head 0 and 6 (every slot
  there).
- **Checks**: the third round (`ce436b2e7`): tests/psp 550 groups, 0 failures, sanitized and unsanitized;
  tests/allegrex 78/0; tests/psp/ares 348/0; the reviewer's all-slot walk with the game (a new GAME, byte for byte
  master's, no slot touched) and the four walks again (traces line for line the second round's). The second round
  (`0707dccc9`): tests/psp 550 groups, 0 failures, sanitized and unsanitized; tests/allegrex 78/0; tests/psp/ares
  348/0; GCC 13.3 `-Werror`; the 21 scenes in Software, every hash master's; MediEvil's save the first round's, byte
  for byte; the walks (the reviewer's upgrade sequence and case B with the game: no save written over). The first
  round: the same suites, and batch 04's saves of Gundam NEXT PLUS, Kurohyou 1, Lunar, Killzone, Lumines and La
  Pucelle byte for byte master's, their traces line for line.
- **Left**: what no PSP recording shows, chosen (which four digits a year past 9999 shows, a zone west of UTC as `-`
  and its size, the two zone forms' refusals); sceRtcParseRFC3339 and sceRtcParseDateTime not done (no game in the
  library known to call them); the automatic answer's first-name choice for list saves naming none, as before.

## PSP NPDRM: EDATA decrypted — the PGD past the EDATA's header, on the memory stick too, its size and end the data's; a PGD file's end its data's — 2026-10-14

Branch `cursor/psp-pgd-edata-2b67`, on master `14b181707` (part 95, #257, with #256); draft PR #259.
docs/psp-core.md, part 97, has the evidence (part 96 is #258's). **The owner's area** then (open to agents since
15 Oct: the standing note above): his PGD/EDATA proposal of 13 Oct (`~/phobos-work/scratch/pgd-offset/`: REPORT.md,
proposed-fix.patch), rebased at his request (14 Oct 20:30),
to merge once CI is green and an independent review finds nothing (his approval, 21:18). Clean room: tpu's
decompilation of the firmware's PGD layer and his edata.c and pgd.c, the PSP Developer Wiki, uOFW's names,
npdecrypter's behaviour on hardware; PPSSPP only as a design cross-check; JPCSP not read. Save states unchanged
(layout 32).
- **The bug** (part 88's review remark): DLC EDATA on the memory stick read raw, with the file's size. Stick files had
  no size, so sceNpDrmEdataSetupKey found no magic; the PGD's header was read at 0 and its data from 0x90 (in a
  download's EDATA they're at 0x90 and 0x120); GetDataSize gave the file's size. And a UMD PGD file's SEEK_END was the
  whole file's, headers and block MACs and all.
- **The change** (io.cpp, kernel.hpp): `pgdDataOffset` is where the PGD starts until its descriptor is read, then
  where its data starts (no new field); the header read there, on the disc or the stick; SetupKey takes the PGD's
  offset from the EDATA header's word at 0x0c and readies only over "\0PGD"; GetDataSize gives the data's size;
  0x04100002 is where the PGD starts, 0x04100001 looks for "\0PGD" there; an unlocked PGD ends where its data does,
  and every seek of it is clamped there (round 2).
- **The rebase** (part 95's meeting point; the patch applied 3-way with no textual conflict, reconciled by hand): a
  stick file's length for the PGD code is `fileLength()`'s, the PSP's, and `pgdFileRead()` refuses bytes past it, so
  an EDATA emptied through another descriptor isn't readied; `seek()` keeps both ends (a stick file's PSP length, an
  unlocked PGD's data size). A descriptor that emptied its file is never a PGD: SetupKey now readies only a file open
  to read alone, as the PGD layer's open does (it refuses one opened to write, 0x80510201; every EDATA open in the
  library's runs is read-only), and only a descriptor opened to write empties its file. The two ends do meet on a
  reading descriptor whose readied EDATA another descriptor empties: its end stays the data's while its reads find
  nothing. No layout change.
- **Review** (independent, of `8d3d08dbb`): no blockers. Its should-fix, taken in round 2: an unlocked PGD's seeks
  weren't clamped to its data's size, as tpu's decompilation clamps them (SEEK_SET 5000 on 3000 bytes of data gave
  5000, then SEEK_CUR -1000 4000 and an empty read; SEEK_END +100 3100; the asynchronous seeks the same). Now clamped,
  with the review's cases as a test group, which fails on `8d3d08dbb` (22 checks). Its wording points taken (the
  read-only check follows the decompilation; the two ends' meeting); part 97's "Not done" gains its three findings:
  SetupKey ignoring the 0x40000000 flag, 0x04100002's argument read as a pointer, SEEK_END before the key (master's).
- **Tests**: the proposal's three groups (an EDATA on the stick, a disc PGD past its file's start, PGD files' ends)
  and `psp_pgd::edata()`, and one for the meeting point (an EDATA beside files emptied as they're opened). On master
  they fail (4 groups, 25 checks); on the proposal applied 3-way without the reconciliation the meeting point's fails
  (3 checks: an EDATA the PSP sees emptied is readied, its size and end 3000 where they're 0); on the branch they pass.
  Round 2 adds the review's seeks past the data's end (a UMD file and an EDATA, sync and async).
- **Games** (scratch runners with part 95's file I/O trace and NPDRM's calls, the clock pinned): the five DLC games
  with the synthetic EDATA at their paths (Metal Slug XX, Disgaea 2, La Pucelle Ragnarok, SOCOM Fireteam Bravo 3,
  Super Stardust Portable) get its plaintext and the data's size, 16384, where master hands them the raw bytes and
  16736; SOCOM reads it once. The seven UMD PGD games: traces and captures identical to master's (1200 frames, and 3000
  for four of them), but for the SEEK_END answers of the three that seek to the end (Black Wolves Saga, Shining Blade,
  Valkyria Chronicles III); Naruto seeks from the start only. Two captures that differed once were master's own
  noise: its reruns give the branch's pictures byte for byte.
- **Checks**: round 2 (`3d500b9e2`): tests/psp 548/0 sanitized and unsanitized; tests/psp/ares 348/0; the 16 game
  runs again on round 2's runner, each trace and capture round 1's; the 21 scenes in Software, every hash master's;
  CI green.
  Round 1 (`61ddbad0e`): tests/psp 547/0 sanitized and unsanitized; tests/allegrex 78/0; tests/psp/ares 348/0; the 21
  scenes in Software: every hash master's, the machine's state too.
- **Left**: no real encrypted DLC tested (the library has none); the PGD offset 0x80 unsettled; drm types other than 1
  unsupported, as before; SetupKey's error returns and the licence checks not done (PGD ioctls on stick files and
  0x04100010 since done: part 101); and the review's three, none of which a game does (SetupKey ignoring the
  0x40000000 flag, 0x04100002's argument read as a pointer where the decompilation stores the value, SEEK_END before
  the key not refused).

## PSP I/O: memory stick files emptied as they're opened keep their bytes (LittleBigPlanet's install), and the runner's file I/O trace — 2026-10-14

Branch `cursor/psp-installs-2b67`, on master `dd37263c8`, merged with `e78b4a364` (part 93; only the docs met); draft
PR #257. docs/psp-core.md, part 95, has the evidence. Clean room:
PPSSPP's issue 579 and its directory file system's comments read as the hardware's description, none of its code;
JPCSP not read. Save states: layout 32 (31 and older load as before; `tests/psp/ares/system.cpp` now refuses 33).
- **The bug** (deep play batch 04's bug 1, high): LittleBigPlanet's first-boot install left GUI.ARC's first 4 MiB zero,
  and the game asked "Save data load failed because the data is corrupt. Retry?" for good.
- **The trace** (`tools/psp-runner --trace-io`, `Kernel::ioTrace`): each IoFileMgrForUser call with its answer, the
  descriptor's path and position, sizes, buffers and the bytes' hash, and asynchronous requests as they're done. It
  showed the game's file library copying GUI.ARC a MiB at a time, opening the stick's file with O_TRUNC (0x02000602)
  for each MiB, seeking to its place and writing it; each open emptied the file.
- **What a PSP does** (PPSSPP's issue 579, this game, tested on a PSP's memory stick): a file emptied as it's opened
  keeps its bytes for a write past its end ("test", then O_TRUNC, seek 4, "abc": "testabc"), and with nothing written
  it ends empty.
- **The change** (io.cpp, async.cpp, kernel.cpp, kernel.hpp, serialization.cpp, system.cpp, utility.cpp, dialogs.cpp):
  such a file isn't emptied on the host; its descriptor holds the PSP's length of it, from 0, which reads, seeks from
  the end, sceIoGetstat, a folder's entries and appends see; writes raise it. The host file is cut to it through a
  second handle held on the file itself (never by path), only while the file has the length the kernel left it, and
  only ever shorter: as soon as a write starts at or before the length (a save rewritten from its start), or as the
  file is closed (sceIoClose, sceIoCloseAsync, the kernel powering on, a state loaded over the program). sceIoRemove
  and the savedata utility's writes and deletes drop a cut on their files; sceIoRename moves descriptors with their
  file (PSP paths matched whatever their case). Files are matched by the host's identity of them, so two names for one
  file share its length and cut. A state keeps both lengths and the hash of the bytes a pending cut would drop, and a
  loaded one keeps its cut only while the host file has that length and those bytes.
- **The review** (round 2, changes requested on `300e03e14`: the delayed cut could reach save data master leaves whole):
  a cut by path (R1: a file renamed onto an emptied file's name emptied at the old descriptor's close; R2: an emptied
  file renamed while open never cut), a state's descriptor cutting a newer save or padding a shorter one (R4, R5), a
  state load dropping the session's cuts (R3), and Phobos killed between a rewrite's last write and its close leaving
  the old tail (R6). All fixed as above; R1 to R7 are tests now. The reviewer's NPDRM nit (a memory stick EDATA gets
  no PGD state, as on master) was the owner's area then, left to him (part 97 has since readied such an EDATA; the code
  is open to agents since 15 Oct). Round 3 (the second review approved `6b26d4b63`,
  leaving three small things): its P4 (a state's cut and a newer save exactly as long, which the length alone let
  through) closed by the tail hash; its P7 (a write through another name for the file lost at the close: part 95's
  claim was inexact) closed by matching files by identity; renames spelled otherwise than the open now carry the PSP
  path. P4 and P7 are tests; its P1 to P7 and R1 to R7 pass.
- **LittleBigPlanet now**: GUI.ARC byte for byte the disc's archives/gui.arc; one boot from a fresh stick reaches its
  first level, where Sackboy walks, and a second boot takes the installed data (deepplay/LittleBigPlanet:
  fixed-path.txt, sheet-fx4.png, z-fixed-install-to-level.png, sheet-fx4b.png; round 2: run-r2a, sheet-r2a.png).
  Its profile saves (18 files through its file library, each emptied as it's opened and written from its start): a
  second boot rewrites PS.BIN over the file there, a third reads it back, master's runner leaving the same files byte
  for byte at each boot (r2-profile-roundtrip.txt).
- **Kingdom Hearts BbS FM's DATA INSTALL** (bug 3, medium), traced, not fixed: it uses the system's game data install
  dialog (sceUtilityGamedataInstall), part 37's cancel stub, so nothing is copied and the game's clean-up finds no
  ULJM05775DATA folder. Needs that utility implemented (a feature of its own).
- **Checks** (round 2): tests/psp 541/0 sanitized (`detect_container_overflow=0`) and unsanitized; tests/allegrex
  68/0; tests/psp/ares 348/0; the runner's test passes; GCC 13.3 builds the changed files with `-Wall -Wextra -Werror`.
  Against master the review's R1-R6 pass and R7 fails (zeros where the old bytes come back); against the first round
  R1-R6 fail. The 21 scenes in Software against master: every frame's picture and VRAM, and RAM, identical; the
  machine's state differs only by layout 32 (the version, 17 bytes an open file: the branch writing layout 31 gives
  master's states byte for byte). Batch 04's saves (Gundam NEXT PLUS, Kurohyou 1, Lunar, Killzone, Lumines, La Pucelle
  Ragnarok, MediEvil): the same files byte for byte as master's runner makes (and as the first round's), and each loads
  on a second boot.
- **Left**: the game data install utility (KH BbS FM; Gunhound EX's DATA INSTALL cancels too). What a PSP's
  sceIoGetstat says of a file emptied so before it's written, and what a gap past its old bytes holds, aren't recorded.
  What the cut can't tell from a file left as it was (part 95): a newer save whose dropped bytes are the state's byte
  for byte, and within a session another host program's in-place write of the same length.

## PSP CPU: the FPU's flush to zero compiled, block endings in fewer steps, the run's count in a register, more of the VFPU compiled — 2026-10-14

Branch `cursor/psp-cpu-round3-2b67`, on master (`dd37263c8`, #253). docs/psp-core.md, part 94, has the evidence.
Clean room: PPSSPP not read at all, JPCSP not read. Save states unchanged (layout 31). Nothing a game sees changes on
clang's builds (Android, macOS); on GCC's (the Linux desktop) vfim of a NaN half may (part 94, change 6).
- **Measured first** (the M1; xctrace with round 2's block map, sorted over the whole thread by a new script; a probe
  build counting the interpreter's calls from compiled code): the GE is 36 to 84% of the emulation thread, compiled
  code 12 to 40%, the VFPU's helpers 1 to 13%. Hot Wheels: Ultimate Racing sets FCSR's flush-to-zero bit, so 660,000
  FPU instructions a frame went to the interpreter (a fifth of its thread). Colin McRae, Killzone and Hot Wheels ran
  14,000 to 16,000 VFPU instructions a frame through the interpreter (packing conversions, vcrs, vcrsp, vrot, vcst).
  A block's end stored and loaded the run's count, checked halted after any load or store, and loaded the target's
  page entry; a result into a register it read took its host register first and loaded the source again.
- **Changes** (recompiler*.cpp, allegrex.hpp, memory.cpp/hpp, system.cpp/hpp; five commits, each measured alone): the
  FPU's arithmetic and compares compiled for the flush bit as the block finds it (inputs and result flushed in integer
  registers, after the host's rounding as the interpreter does; checked per instruction); the registers an instruction
  reads kept for it (`pinSources()`); no halted check after a native instruction's way to the interpreter; no page
  check going on to pages the owner says never change (`Allegrex::steady`: main RAM, the scratchpad); r0 as the
  constant 0; the run's count in a saved register on ARM64 alone (held game registers 7 -> 6); vf2h, vh2f, the
  integer packings, vcst, vcmov, vcrs, vcrsp, vqmul and vrot compiled with the prefixes at rest. A sixth, main RAM's
  loads without the page table (d5c43999b), measured nothing on the Mac or the RP6 and is taken out (3081c98e2).
- **Numbers** (the M1, Vulkan fast, master -> 2ce7026e9 with the direct loads still in, emulation thread, medians of three rounds taking turns): Hot Wheels' race 340 -> 249 M host instructions a frame (-27%; 30.4 -> 24.3 ms), Colin
  McRae -8.7%, Disaster Report 3 -5.0%, Killzone's gameplay and Armored Core -4.5%, Army of Two and Killzone's menu
  -2.8%, Monkey Ball -2.0%, the GE-bound scenes (Ghost of Sparta, Chains of Olympus, MX vs. ATV, Death Jr. II) 0.6 to
  2.1%; the software renderer Hot Wheels -12%, the rest within 3% (its emulation thread draws bands too). Each change
  measured alone: part 94's step table.
- **Exact**: settled hashes, master against the branch (100 frames, every frame's picture and VRAM,
  then RAM and the machine's state): 78/78 identical on 2ce7026e9 (the 21 scenes in Vulkan fast, Software and Vulkan
  accurate, and Hot Wheels, Killzone's gameplay and menu, MX vs. ATV and Chains of Olympus in all three), 78/78 again on 622fd6a0f and on the last code (3081c98e2, against master with #254); tests/allegrex's new groups (fpu flushing, loads across pages, runs moved by syscalls; the recompiler
  cases of a page taken out unheard and of a halting syscall; 15 new kinds in vfpu compiled lanes); 43 broken versions of the new code, 39 caught, the other 4 equivalent (part 94; 52 and 48 with the direct loads'
  nine, all caught).
  vfim of a NaN half now gives halfToFloat()'s bits in both engines whatever the compiler (clang's builds gave that
  already; GitHub's GCC at -O3 quieted it in compiled code once this round changed its inlining).
- **Checks**: tests/allegrex 74/0 (sanitized and release on ARM64; built for x86-64 and run under Rosetta, debug and
  release), tests/psp 525/0 (sanitized, with detect_container_overflow=0, and unsanitized), tests/psp/ares 340/0;
  GitHub's checks on 622fd6a0f all pass (the PSP core's, desktop, Android).
- **Merged with master** (#254) as cd97a4630, the docs alone conflicting (part 93 before 94): the 26 scenes' hashes
  against that master 78/78 identical; tests/psp 528/0 (sanitized), tests/psp/ares 340/0, tests/allegrex 74/0.
- **Review** (independent, read-only, to 1c951a6f3): no blocker; its points taken in f055ec9f3: the count in a
  register on ARM64 alone (Windows' x86-64 had it, untested); tests/psp's machine sets the steady pages, so its groups
  take the skipped page checks (and took the direct loads while they were in), and the Allegrex harness's data is in
  a steady page; the review's probes as tests (the flush bit switched in a block and a delay slot, code rewritten
  after a link through each mirror, loads at RAM's end, runs of 2^62 to 2^64, r0 as every operand, threads switched
  by syscalls);
  emitFallback() keeps calls. Flush to zero from a cold boot measured: Hot Wheels from boot to its race sends no FPU
  arithmetic to the interpreter (sqrt.s and floor.w.s, which have no native code, as before). Mutations again: the
  same 48 caught, the same 4 equivalent. After it: tests/allegrex 78/0 (sanitized, release, x86-64 both), tests/psp
  528/0 (sanitized and not, nothing skipped), tests/psp/ares 340/0, the 78 hashes against master with #254 identical.
- **Left**: the GE (most of every scene's thread); jumps through a register (2 to 3%: a cache of the last target); the
  VFPU's NEON helpers and vfpuEach; vcmp, vdot and vfad with prefixes; the FPU's mode check once a block; sqrt.s.
- **RP6** (the coordinator's runs, master dd37263c8 against 622fd6a0f, the direct loads still in; Vulkan fast, 600
  frames, three rounds; emulation thread CPU ms / M instructions / M cycles a frame): Colin McRae -4.7% / -7.3% /
  -4.8%, Hot Wheels -4.2% / -10.8% / -3.8%, Killzone's gameplay -3.5% / -5.1% / -3.4%, Disaster Report 3 -3.2% / -4.9%
  / -3.2%, Monkey Ball -2.7% / -1.9% / -2.6%, Army of Two -2.2% / -2.9% / -2.1%, Ghost of Sparta -0.7% / -0.7% / -1.2%,
  MX vs. ATV -0.5% / -0.7% / -0.6% (waits on the GPU); nothing slower. The direct loads alone (9586b8c4e against
  d5c43999b): Army of Two -0.3% / -0.1% / -0.0%, Colin McRae -0.3% / -0.5% / -0.4%, Killzone +0.1% / -0.3% / +0.0%:
  nothing, as on the Mac, so they are out.
- **Direct loads taken out** (3081c98e2): loads compiled as on master again (S1 the page table; the region,
  directBase() and pageTable() gone), everything else kept; "direct loads" is "loads across pages", without the
  region's checks. After it: tests/allegrex 78/0 (sanitized, release, x86-64 both), tests/psp
  528/0 (sanitized and not, nothing skipped), tests/psp/ares 340/0, the 78 hashes against master with #254 identical,
  39 of the 43 mutations caught (the same 4 equivalent); the Mac's gains the same (master -> 3081c98e2: Hot Wheels
  -26.3% host instructions, Colin McRae -7.9%, Killzone -4.2%, Monkey Ball -1.6%; 0.4 to 0.6% more than 622fd6a0f).
- Scratch: `~/phobos-work/scratch/cpu-round3` (`NOTES.md`, `cats.py`, `mutate3.py`, `queue-hash.sh`, `queue-final.sh`).

## PSP Vulkan: the frame buffer's alpha as its stencil has it — MX vs. ATV Untamed's race (the haze over its hills and trees, the tree's shadow) — 2026-10-14

Branch `cursor/psp-vk-mx-2b67`, on master with #251 merged (`5893dbdc0`), then merged with #253 (`dd37263c8`, part 92:
conflicts in `settings()`'s end and `begin()`, both sides kept; `shaders.hpp` made again with compile.sh); PR #254.
docs/psp-core.md, part 93, has the evidence; docs/psp-gpu-renderers.md ("Stencil") the design. Clean room: no PPSSPP
code read (its GPU backend not opened), JPCSP not read. Save states unchanged. The software renderer untouched.
- **Merged with part 92** (#253): its kept settings return from `settings()` before `lagging` is
  worked out, which stays right (everything it's worked out from is among the kept settings); the setting and the
  noting are `begin()`'s for every PRIM, before its primitives go into a draw of their own or among fast mode's meshes.
  A test for those paths (sprites alike, the second's settings kept; a small PRIM among the meshes leaving the alpha
  behind, another blending by it), which fails with `lagging` reset before the kept return (2,304 bytes apart). On the
  merged code (78138561a) against dd37263c8: Software the same in 25; Vulkan in 23 each, the race's and Cars' hashes
  the pre-merge code's exactly; the alpha set as before (45, 50, 0, 0 per 100 frames); tests/psp 528 groups, 0
  failures, sanitized and not; tests/allegrex 68, 0; tests/psp/ares 340, 0; CI green.
- **Found** with part 90's PRIM log and dumps of VRAM, everything drawn settled, before chosen PRIMs of a frame: the
  race (Cars Race-O-Rama's engine) makes its alpha from its depth in strips, zeroes the near pixels' alpha by 15
  sprites whose stencil operation is ZERO where the depth test fails, and blends its sky in by the frame buffer's
  alpha. The GPU keeps that alpha twice, in the colors' alpha and the stencil, and writes the colors' alpha only where
  a pass's result is known: the ZERO reached the stencil alone and the GPU's blending read the colors' alpha (the sky
  over the near pixels whose depth fell in the strips' ramp: stripes along the depths, the trunk, the shadow).
- **Changes** (gpu.cpp, gpu.hpp, vulkan.cpp, copy.frag, compile.sh; both modes, any resolution): a target notes where
  a PRIM's stencil operations changed the stencil without the colors' alpha (`Target::lags`); before a PRIM blends by
  the frame buffer's alpha there, or a copy of the target is taken over it, the colors' alpha is set from the stencil
  on the GPU (`catchAlpha()`, `Command::Alpha`: `copy.frag`'s `ALPHA` build, a draw for each bit a read back takes,
  in a render pass over the rectangle alone; not for a copy whose alpha the PRIM's pixels don't use). Under the GPU's
  own logic operation the colors' alpha isn't written (Vulkan's takes the alpha, the GE's leaves the stencil), which
  notes the area too. Nothing waits.
- **Pictures** (mean channel difference from Software, #251 -> this, the kit's runners): the race at frames 100, 130,
  160 and 200 4.72, 5.20, 2.84 and 2.79 -> 0.16, 0.21, 0.21 and 0.22, accurate and fast alike (9-10% of pixels more
  than 24 apart -> 0.0-0.1%); Cars Race-O-Rama, the same bug, at 100, 150 and 200 1.19, 0.80, 0.76 -> 0.58, 0.54,
  0.43 accurate, 1.21, 0.82, 0.79 -> 0.60, 0.56, 0.45 fast. Sheets: `~/phobos-work/scratch/vk-mx/out/` (`mx-final-100.png`)
  and `kit/look/`.
- **Hashes** (the kit's 21 scenes, #251's three games and the race, master at 5893dbdc0 against the final code,
  bb6298e9a, 100 frames, every frame's picture and VRAM settled, then RAM and machine): Software the same in all 25;
  Vulkan accurate and fast each the same in 23, the others the race and Cars, whose hashes are the first fix's
  (ed3f121c6) exactly. The alpha set per 100 frames (both modes), before the review's points and after: the race 90 ->
  45 (once a picture), Cars 50 -> 50, Colin McRae's race 98 -> 0 (its pictures the same), the other 22 scenes none.
- **Speed** (Vulkan fast, frames a second, master / before the review's points / final, two rounds in turns, the
  Mac's load 9-13): at 1x the race 28.30 / 29.52 / 30.32, Cars 20.36 / 21.00 / 22.06, Hammerin' Hero 190.9 / 189.9 /
  193.6, Colin McRae 85.6 / 88.8 / 83.0; at 2x the race 21.24 / 20.99 / 20.64, Cars 15.41 / 15.80 / 15.66, Hammerin'
  Hero 107.3 / 106.2 / 114.7: within the runs' own spread (a tenth). The RP6's numbers are still to take.
- **Tests**: a GPU group (the alpha zeroed by a depth fail, inverted, by a 3D mesh's depth fail, then blended by and
  taken as a texture, in 8888, 4444 and 5551) and part 90's 5551 group taking all its rows, both modes, 1x and 2x; on
  #251's core they fail in every mode and resolution (10,752, 8,960 and 1,344 bytes apart). And a group for each other
  way a PRIM leaves the alpha behind (a stencil test failing, MASK_ALPHA in a PRIM and in clear mode, INCREMENT and
  DECREMENT, a REPLACE blended by its own alpha without dual-source blending, the GPU's own logic operation, a 4444
  REPLACE under a comparison): on master's core the first five fail (4,608-6,912 bytes apart), before the review's
  points the 4444 one (3,072) and, on CI's lavapipe (the M1 has no logic operations), the logic operation's (4,608).
- **Review**: an independent read-only review found no blocker and four points, all taken (part 93, "The review"):
  the GPU's logic operation's alpha, a 4444 stencil's top four bits, the cost on a tile-based GPU (the pass over its
  rectangle, no setting for a copy whose alpha isn't used), and tests for the other ways.
- **Found, not changed**: the GPU's corners' depths aren't whole numbers as the GE's are, so its depths come out one
  more at about half of a 3D surface's pixels (the race's 51.2% Software's; truncated in a scratch build 95.3%, its
  pictures unmoved). Its own change, as it moves every 3D game's depth on the GPU.
- **For the test plan**: on the Mac, tests/psp's GPU groups can stop on an AddressSanitizer container-overflow in
  `std::vector<u8>::resize` (seen in "gpu stencil formats" on #251's core, before this change): nall and sljit are
  built without the sanitizers, and the linker can keep their unannotated instantiation. A false positive; these
  runs set `ASAN_OPTIONS=detect_stack_use_after_return=0:detect_container_overflow=0`.
- **Checks** on the final code (bb6298e9a): `tests/psp/run-tests.sh` 523 groups, 0 failures, sanitized (the container
  check off, above) and not; `tests/allegrex/run-tests.sh` 68, 0; `tests/psp/ares/run-tests.sh` 340 checks, 0 failed;
  CI green.

## PSP GE: fewer GPU draws and cheaper PRIMs — the texels taken in the vertices, fast mode's small PRIMs among its meshes, a PRIM's settings kept — 2026-10-14

Branch `cursor/psp-ge-speed3-2b67`, on `af542113a` (#251's head), so its diff is this round's alone (draft PR to
#251's branch, to be retargeted once #251 merges). docs/psp-core.md, part 92, has the evidence. Clean room: no PPSSPP
code read (its GPU backend not opened), JPCSP not read. Save states unchanged (layout 31). The branch name isn't
`cursor/psp-ge-round3-2b67`: that's PR #132's, closed long ago.
- **Measured first** (the M1, the bench runner at -O3 with thin LTO, a build counting why each GPU draw couldn't join
  the one before): fast mode's draws alternated between meshes and the GE's own small PRIMs (Army of Two 3,144 draws a
  frame, 2,167 such breaks; Ghost of Sparta's menu 3,684 and 2,095); the push constant `held` split Ghost of Sparta's
  menu into 1,573 draws in accurate mode (1,213 breaks); MoltenVK's encoding at the submit was 7-16% of the emulation
  thread; Monkey Ball's 50,000 PRIMs a frame paid for their whole setup each.
- **Changes** (gpu.cpp, gpu.hpp, vulkan.cpp, draw.frag, draw.vert's comment, transform.vert, draw.cpp, threads.cpp,
  ge.hpp): the texels a PRIM takes of a copy in its vertices (flags' bits 8-27) and its Transformed block, not the push
  constants; fast mode's 3D triangles the GE transformed itself recorded among the meshes as models placed already,
  which transform.vert takes with draw.vert's own arithmetic, so meshes and the small PRIMs between them are one draw;
  a PRIM's GPU state kept where its settings are the last one's; the last texture looked at first; pipelines found by
  a hash; fast mode's models and corners written into room made once a PRIM; the software renderer's triangle jobs set
  up where the batch keeps them.
- **Draws a frame** (base -> branch): fast mode Ghost of Sparta's menu 3,684 -> 346, Army of Two 3,144 -> 642 (menu
  3,205 -> 541), Monkey Ball 2,119 -> 1,143, Hits Remixed 948 -> 472, Twisted Metal 208 -> 110, Sonic Rivals 445 ->
  255; accurate mode Ghost of Sparta 1,573 -> 348, Army of Two 963 -> 649.
- **Numbers** (the M1, MoltenVK, the emulation thread, base -> branch, two rounds taking turns at a load of 15-50;
  fps it allows; instructions steadier): Vulkan (fast) Army of Two playing 19 -> 21 (343 -> 300 M instructions a
  frame, -13%; cycles -12%), its menu 28 -> 32 (-16%), Ghost of Sparta's menu 35 -> 40 (-8%; cycles -11%), Hits
  Remixed 150 -> 167 (-7%), Colin McRae -6%, Monkey Ball -3%, Death Jr. II, Sonic Rivals and Twisted Metal -3%;
  Vulkan (accurate) Monkey Ball and Ghost of Sparta -4%, the rest within 2%; Software within its noise (the bands its
  emulation thread draws while it waits move its counts by a tenth between runs of one build).
- **Exact**: base against the branch from the 21 scenes' states, 100 frames, every frame's picture and VRAM, then RAM
  and the machine's state: identical in all three renderers (63 of 63, and 63 of 63 again on b3a208277). The one
  difference a GPU could see: draw.vert's
  position isn't declared invariant as transform.vert's is, so a driver compiling the same expression differently in
  the two could move a small PRIM's corner by a float's rounding in fast mode alone (on the M1, none: tested at 1x and
  2x). draw.vert, accurate mode's, is as it was.
- **Checks** (b3a208277): tests/psp 522/0 (sanitized, with `detect_container_overflow=0`: on this Mac base's GPU groups
  stop at an AddressSanitizer container-overflow in `vector<u8>::resize()` too, an unsanitized object's
  instantiation), and its GE's and GPU's 143 groups unsanitized 0 failures; tests/allegrex 68/0; tests/psp/ares
  340/0; the shaders' CI check (glslang 16.6.0 byte for byte, valid for Vulkan 1.0) passes. GCC 14 without the
  sanitizers (on 9c411d1ed) stops at "may be used uninitialized" in four.cpp (`downAxis`, `four.depth`), untouched
  here and met by the God of War software round on Oct 10 too; CI's GCC 13 with the sanitizers doesn't.
- **Review** (independent, read-only, to 4d26b6a98): no blocker; four points fixed in b3a208277: a mesh's models kept
  across its runs by the recording's generation (a near-plane cut no longer copies the PRIM's vertices again, nor can a
  hand-over in between leave a run's indices on placed models); the start-up check draws the GE's own 3D among the
  meshes, and where only that fails System turns `screenMeshes` off and checks again; kept settings let go after a
  texture the backend couldn't make; tests in a 480-wide frame buffer, with the shine apart, a near-plane cut, and
  sampling past what's taken (each of the clamp's three paths, broken, fails them; the kept settings' fix undone fails
  the pretend backend's test). Small points taken; draw.vert's invariance left to measure on the RP6. Instructions
  unchanged by them (both Vulkan modes to the percent, Software within its noise).
- **After an update** players' kept pipelines (System's "Pipeline Cache") miss once: draw.frag's push constants and fast
  mode's vertex layout changed, so the first session makes its pipelines afresh (a stutter at each).
- **What's left**: Monkey Ball's draws split by texture (descriptor indexing); a Look kept on the GE's side and the
  command loop (7%); fast mode's vertices read straight into models; 2D PRIMs between meshes; the software renderer's
  triangle setup. Measure on the RP6, where the driver's work is in the `vkCmd*` calls, not deferred to the submit.
- Scratch: `~/phobos-work/scratch/ge-round3` (`NOTES.md`, `patches/gestats-patch.py`, `stats-run.sh`, `hash21.sh`).

## PSP Vulkan: the depth buffer as the PSP's memory, a 5551 stencil bit — Hammerin' Hero's and Jeanne d'Arc's characters, Cars Race-O-Rama's race, Colin McRae's terrain shadows — 2026-10-13

Branch `cursor/psp-vk-models-2b67`, on master (`542da5a28`, #230), merged with `3228b825d` (#229, #237); PR #251.
docs/psp-core.md, part 90, has the evidence; docs/psp-gpu-renderers.md the design. Deep-play batch 03's bug 2 and batch
02's item 4. Clean room: no PPSSPP code read (its GPU backend not opened), JPCSP not read. Save states unchanged. The
software renderer untouched. Cursor ran a second copy of the agent for a while: its commit (5071b80b5) and design-doc
text are kept; the first holds the worktree (`LOCK-worktree`, `TWIN-NOTICE.md` beside it, neither committed).
- **Found** with a scratch runner logging every PRIM of a frame: the characters' PRIMs reached the GPU whole (skinned;
  not the cause), and failed per-pixel tests. Hammerin' Hero and Jeanne d'Arc draw their scene into the screen's 5551
  frame buffer, then their characters into one of their own (0xcc000), cleared without its depth, tested against the
  scene's depth (0x88000) and marked in the stencil by INCREMENT, then laid over the screen as a 5551 texture behind
  an alpha test. On the GPU each target's depth was its own (the characters tested against stale depth), and a 5551
  INCREMENT stepped the stencil 0 to 1 with the color's alpha left (the composite's alpha test dropped every pixel):
  each alone lost them. Cars Race-O-Rama reads its race's depth as a CLUT16 texture through VRAM's second copy
  (0x042cc000) for the alpha its sky is blended by, and the GPU's depth was never in memory: the sky covered the race.
- **Changes** (gpu.cpp, gpu.hpp, vulkan.cpp; both modes, any resolution): the newest depth drawn in a depth buffer
  taken into a target from the target over the same depth buffer (address and row width) that drew it, on the GPU
  (`takeDepth()`, `Command::Depth`: the depth aspect copied, nothing waits); on 5551, INCREMENT a REPLACE of 255,
  DECREMENT and a REPLACE of 0 a ZERO; a texture read through VRAM's depth views over a depth buffer the GPU drew newer
  depth in has that depth read back into memory first (`putDepth()`, `Backend::readDepth()`: a wait, once until more
  depth is drawn).
- **Pictures** (mean channel difference from Software at frame 100, master -> branch): Hammerin' Hero accurate 1.04 ->
  0.07, fast 2.14 -> 1.17; Jeanne d'Arc 0.27 -> 0.04 and 2.31 -> 2.10; Cars 37.08 -> 1.19 and 37.08 -> 1.21; Colin
  McRae's race (the kit's scene, the same bug: its terrain shadows, which part 83 left) 3.69 -> 0.22 and 3.93 -> 0.53.
  Sheets: `~/phobos-work/scratch/vk-models/png/`.
- **Hashes** (the kit's 21 scenes, master against the branch, 100 frames, every frame's picture and VRAM, then RAM and
  machine): Software the same in all 21 and the three games' (24); Vulkan accurate and fast the same in 19 of 21, the
  others Colin McRae's race (its shadows) and Twisted Metal's play (4 pixels a level apart, 2 now Software's; in fast
  mode 8, 2 Software's where 4 were).
- **Tests**: four GPU groups (a depth buffer two frame buffers draw with, a 3D mesh among them; a 5551 stencil bit by
  each operation, taken as a texture's alpha; a depth buffer read as a texture through the depth views, and memory's
  changes around it; the depth formats' conversions), in both modes; on master's core the first three fail on their
  pictures.
- **Review**: an independent read-only review (no blocker, no major point; nothing touching the software renderer):
  all its points taken but the clear's depth copy, measured instead (b042ddb15, e620356f1, 1c44ceab7). Its test of each
  frame buffer's own stencil caught MoltenVK's image copy of the depth alone taking the stencil too: the depth goes
  through the staging buffer now.
- **Checks** (1c44ceab7, merged with master at 3228b825d): tests/psp 513/0 (sanitized), tests/allegrex 68/0,
  tests/psp/ares 340/0; CI on 1c44ceab7 green. Hashes as above, against master as merged in.
- **Speed** (the M1, two rounds, master -> branch): Vulkan fast at 1x 2-5% slower in the four scenes (Hammerin' Hero
  168.9 -> 164.8, Jeanne d'Arc 277.3 -> 264.0, Cars 17.76 -> 17.06, Colin McRae 69.9 -> 68.5), at 4x about 7%
  (Hammerin' Hero 46.7 -> 43.6, Cars 8.09 -> 7.48): the depth copies, Cars' read back and wait, and what's drawn now.
  To measure on the RP6.
- **Left**: depth between depth buffers of other row widths over the same memory; the CPU's loads, block transfers and
  palette loads through the depth views, and a texture over depth through VRAM's plain copies; a 4444 frame buffer's
  INCREMENT and DECREMENT; a fail or depth-fail operation's stencil in a texture copied on the GPU.
- Scratch: `~/phobos-work/scratch/vk-models`.

## PSP: polling loops — the polled calls in fewer steps, and a loop that comes back as it was skipped to the go's end, each skip proved — 2026-10-13

Branch `cursor/psp-polling-2b67`, begun on `cursor/psp-cpu-round2-2b67` (#237 on #229), master merged in once both had
(`3228b825d`). docs/psp-core.md, part 91, has the evidence. Clean room: PPSSPP not read at all, JPCSP not read. Save
states unchanged (layout 31). Nothing a game sees changes: pictures, VRAM, RAM and the machine's state are master's.
- **What the loops call** (a scratch syscall trace, the loops read from RAM): both Need for Speeds' idle thread runs
  `while(*count < target) yield(0)`, the yield being sceKernelGetThreadId, sceKernelReferThreadStatus (itself) and
  sceKernelRotateThreadReadyQueue at its own priority, alone in its line: 29 instructions, 526,000 calls a frame;
  Twisted Metal's logo asks the movie ring's free packets and signals a semaphore already at its most (refused), the
  Armored Cores' intros the same with the ring asked twice, Disaster Report 3's intro the blank count and the ring:
  90,000 to 370,000 calls a frame. Nothing changes from turn to turn but the thread's run time in the status (never
  read) and the ready order's count (the rotation); every call quiet or calm, so each loop runs to the go's end.
- **Changes** (each its own commit, with tests): main RAM read, written and looked up without a call, copies' pieces
  without a std::function; polled calls dispatched in fewer steps (imports first, the FINISH and calls into the program
  looked for only where there may be one, the movie library's mark compared in place); spins skipped (`spins.cpp`):
  once a quiet or calm call's place has come round four times the same cycles apart, the kernel runs the next turn on
  a CPU of its own (the interpreter, its stores kept aside, its system calls worked out without being made) and, if it
  comes back with every register as it was and every byte it stored what memory holds (but the run time, never read),
  skips whole turns to two short of the go's end: their cycles to the clock, the ready order's count, the last turn's
  run time written where its status would write it.
- **Numbers** (Mac, Vulkan fast, the emulation thread's host instructions and ms a frame, medians of three rounds
  taking turns; the load 10 to 200, so the instructions carry it): Most Wanted's race 289 -> 38 M (-87%, 17.4 -> 3.2
  ms), its profile 301 -> 19 M (-94%), Underground Rivals' title 276 -> 9 M (-97%, 18.0 -> 1.3 ms); Twisted Metal's
  logo 286 -> 10 M (-97%, 14.6 -> 0.7 ms), Disaster Report 3's intro 149 -> 10 M (-93%), the Armored Cores' intros
  148 -> 13 and 140 -> 18 M (-91%, -87%); the per-call commits alone 18 to 20% of it in the Need for Speeds, 43 to 54%
  in the movies; scenes that don't poll 1 to 7% fewer instructions (main RAM without a call). The software renderer
  alike (the Need for Speeds -84 to -91%, the movies -87 to -97%).
- **Exact**: the 21 scenes' hashes identical to master's in the software renderer (100 frames, every frame's picture and
  VRAM, then RAM and the machine's state), Vulkan fast and Vulkan accurate (200 frames, the end's RAM and state), for
  the first code, after the review's fix and for the last commit (`3b2e44167`); tests/psp's new groups run each program
  both ways on both engines, the whole state compared after every eighth of a frame (skipped, refused, and skips made
  directly against the same turns run); 29 broken versions of spins.cpp, 21 caught, the other 8 changing nothing a game
  sees (guards that can't be reached or always hold). An independent review found one exactness bug (the last turn's run
  time rewritten from the status's size word as the turn left it: a status of 88 bytes got bytes 88-91 written), fixed
  with a test the first code fails, and its other points taken; a delta review of the fixes found no exactness bugs, its
  points (a guard, the run time pinned to the cycle, a sentinel, a ring whose library names none, comments) taken.
- **The library check's Platypus** (pictures other than master's from frame 90, in two runs of each runner): not the
  emulation. From boot with the check's presses every frame's picture and VRAM, then RAM and the machine's state, are
  master's through 3600 frames; the runner's PNG is the last picture the screen's thread presented, a frame or more
  older than its name on a busy host, and the branch's cheaper frames make other captures stale (master's at 90, the
  branch's at 320 and 480; two runs of master's newer runner differ at 10 of 360 PNGs too). The other 30 flagged games'
  every frame is master's too. A capture taken from the core itself, drawing settled, would make the check's PNGs exact.
  With master merged again (`2d6766628`): the 21 scenes identical in all three renderers, the suites passing (tests/psp
  517/0 with ASan's container-overflow check off: master's own suite trips it on the Mac), the speeds as above.
- **Checks**: on the last commit, tests/allegrex 68/0, tests/psp 512/0 (sanitized), tests/psp/ares 340/0; GitHub's
  checks pass (the PSP core tests built by GCC, the desktop builds, the Android APKs).
- **Left**: loops whose answers move on with the clock (polling the system time till a moment) can't be skipped so;
  only six functions are worked out (others to add as traces show loops of them: a library run with the spin debug
  patch would list proofs that fail); the per-call costs left (the scheduler's look through every thread at a
  rotation, the stub's `jr ra` looked up in compiled code) matter only for loops the skip can't take.
- **On the RP6, to measure**: master against this branch, Vulkan fast at 1x, part 79's scenes (nfsmw-race,
  nfsmw-profile, nfsur-title, tm-logo, dr3-intro, acff-intro, acsl-intro): the Need for Speeds, at 41-42 there held by
  the emulation thread (PPSSPP 60), should reach 60 unless the GPU's side holds them; the movies' thread a tenth of
  what it was; the rest as they were.
- Scratch: `~/phobos-work/scratch/polling`.

## Library: every style's own take on the Systematic consoles; the glyph packs gone — 2026-10-13

Branch `cursor/themes-console-icons-2b67`, on `cursor/themes-backdrops-2b67` (the entry below), at the owner's choice:
he likes BAXY Square's Systematic consoles and wanted each theme's icons to be those consoles in its own feel, not
the Phobos, Pixel and Manga packs' "little blobs". App only.
- **Match style** (now the default) gives every style a treatment of the same art (`ConsoleTreatment`,
  `ui/theme/ConsoleTreatment.kt`): Glass under no style (recolored, light along the top and left edges, a gloss, a
  soft shadow), Neon under Retrowave (primary tubes on the outline, secondary on the parts' edges, glowing; darker
  tubes with a tinted glow on light themes, where added light would wash out), Pixel under Pixel art (32 cells along
  the longer side, the theme's four tones and accents, a dark outline), RPG under 16-bit RPG (the same cells in six
  shades, cool shadows and warm lights, a black outline, a highlight, a drop shadow), XMB (one light color shaded by
  the art, the text's color in light themes, a gloss and a glow), Manga (an ink line, screentone in the mid tones,
  solid ink in the darkest) and CRT (the art's light and its outline in the CRT style's phosphor, scanlines and a
  bloom; on light themes, a dark phosphor ink). Neon's tubes and the phosphor stand out at least 3:1 against the
  tile. Systematic keeps the art as drawn.
- **The pixel art** is generated by `tools/console-art/pixelate.py` (cairosvg) into `ConsolePixels.kt`: each console
  without its halo, cropped, on the grid, each cell one of the SVG's own colors (the one covering most of it, or a
  darker one that covers a good share and stands out), its palette only the colors its cells took. At runtime
  (`toneLevels`) the grey covering most of a console takes the tone nearest its own lightness, the other greys
  covering a twentieth of it or more the tones either side in order (the run moved a tone if that sets more of them
  apart within 0.2 of the console's lightness, 0.12 for greys under 0.05 apart), and the rest the nearest one's. So
  black consoles (the Mega Drive family, the Neo Geos, the N64) stay dark with their parts apart; in Pixel's four
  tones a few near-black greys share one (the Atari 2600's, the 32X's). Mapping each grey's lightness straight to a
  tone made them blobs; centering the tones on a console's lightness drew them light. `PixelConsole` draws it at a
  whole number of pixels to the cell.
- **The other looks** are drawn from the SVG without its halo (`withoutHalo`, in step with the tool's) and finished
  in a Coil transformation (`ConsoleFinishing` over `ConsoleFinish`), cached per theme and look; the SVG is rendered
  at the largest size that with the finish's margin still fits the icon's box (`ConsoleFinish.inside`), shown 1:1.
- **Every listed system** has its console in every look (`consoleArtFor`, `ui/PlatformIcons.kt`): the set has no
  Super Game Boy, PC Engine LD or Pocket Challenge V2, which take the Super Famicom's, the LaserActive's and the
  WonderSwan's as before; the PSP is Phobos's own drawing in the set's style.
- **Settings**: Platform icons offers Match style or Systematic; a stored `PHOBOS`, `PIXEL` or `MANGA` reads as Match
  style, and so does nothing stored (Systematic was the default while Match style meant the glyphs).
  `PlatformGlyphs.kt`, `PlatformGlyphArt.kt` and `PlatformGlyphsTest` are gone.
- **LICENSE**: the Systematic notice names the derived art (`ConsolePixels.kt`, the tool, `ConsoleTreatment.kt`) as
  covered by it, all but the PSP's, which its own notice covers as derived from Phobos's drawing; the glyphs' notice
  is gone.
- **Previews** (outside the repo): `~/phobos-work/scratch/themes/previews/icons-<look>-<theme>.png`, every system the
  Library lists on its tile, for Systematic, Glass, Neon, Pixel, RPG, XMB, Manga and CRT in two to four themes each,
  light ones among them. A local JVM test ran the real Kotlin: the pixel art exactly; the other looks over the SVGs
  as cairosvg draws them at the size the app renders them (AndroidSVG draws them the same up to antialiasing).
- **Review**: an independent read-only review (a subagent) of the whole branch: NEEDS CHANGES, two Mediums, both
  fixed: the pixel looks still merged dark consoles' parts (a grey's tone came from its place in the console's range;
  now `toneLevels`, after a delta check caught a second pass drawing black consoles light), and Neon and CRT added
  light that washed out on light themes (now darkened tubes and ink with a tinted glow, under a contrast test). So are
  its Lows: the smooth looks resampled to the tile (now rendered to fit it 1:1), per-tile costs, tests for RPG's
  highlight, the neon's colors and every launchable system's console, stale KDoc and plan wording, and
  `PixelConsole`'s role. Its last delta checks passed.
- **For the owner**: Match style is the new default (Systematic was); the stand-ins (the Super Famicom for the Super
  Game Boy, the LaserActive for the PC Engine LD, the WonderSwan for the Pocket Challenge V2); on light themes Neon's
  two tubes can come out close in color.
- **Checks**: 328 host tests pass (`:app:testModernDebugUnitTest`); new `ConsoleTreatmentTest` (11, among them every
  system `LaunchSystems` names having a console, each console keeping its lightness and its parts apart in the tones,
  Neon's tubes and the phosphor standing out 3:1 on light and dark tiles, and the art rendered as large as fits its
  box finished), `AppearanceSettingsTest`'s icon pack check and `LicenseNoticesTest`'s Systematic notice check. No
  APK built locally (CI builds them); not yet seen on a device.

## Appearance: the Sakura cycle in phases, three Pixel art scenes gone, night city and XMB Deep redrawn, Manga and CRT redone, a magazine cover and Truck-kun — 2026-10-13

Branch `cursor/themes-backdrops-2b67`, on master (`1d9308714`, merged with `542da5a28`, #230, `3228b825d`, #237, and `19da506fe`, #238, since), from the owner's feedback on the styles and their
backdrops. App only (Kotlin; no native or core change). The console icons follow in a second branch,
`cursor/themes-console-icons-2b67`.
- **Sakura cycle** (`PixelBackdropScene.SAKURA_CYCLE`, `ui/theme/PixelSakura.kt`): the sun and the moon were up
  together, side by side, and popped in and out. `SakuraCycle` (plain math) runs a day and a night in 8 minutes (108 s
  before) in eight phases: sunrise, day, sunset, dusk, moonrise, night, moonset, dawn. A body rises one diameter (and
  its glow's reach), then follows an arc that climbs on at about the speed it rose at, and sets the same way; it's
  drawn a whole pixel at a time and clipped at the first row the far ridge covers in every column, so it climbs out
  from behind the mountains and sinks back behind them, its glow with it (`middleRow`'s reach). The sky and the light
  are eased between keys (dawn, day, sunset, night): the sky is full night before the moon rises and until it sets,
  dawn comes before the sun rises, and stars come out one by one at dusk (`StarField`'s `shown`). The landscape is
  painted again only when the moment changes. Sakura day and Sakura night are unchanged pixel for pixel (3,120 frames
  compared with the previous code: every theme, five sizes, live and still). With animations off the cycle shows the
  sun touching the mountains as it sets.
- **Pixel art scenes**: Space (with `Meteors`, `PixelScene` and `pixelSceneColors`), Underwater and Pixel castle are
  gone; the list is Sakura (now the default), Sakura day, Sakura night, Pixel plains, Night drive. Stored names read
  as the closest kept scene (`pixelBackdropNamed` and `screensaverBackdropNamed`, `data/SettingsStore.kt`): `SPACE`
  and `CASTLE` as Sakura night, `UNDERWATER` as Sakura day, the screensaver's `PIXEL_SPACE`, `PIXEL_CASTLE` and
  `PIXEL_UNDERWATER` the same way, the pre-split `SAKURA` as Sakura night; anything else unknown is Sakura (Match app
  for the screensaver). `AppearanceSettingsTest` writes the old names into a real settings file.
- **Night city** (`ui/theme/RetrowaveCity.kt`): the old row of dark boxes over the grid is now a dense neon
  skyline over the water (the owner's photo reference used for the mood only): five layers of towers fading into a
  lit haze, setbacks, needles, slanted roofs, lanterns and tapers, windows speckled or in lit floors, neon edges,
  rings, rooflines, glyph-like signs and scan-lined screens whose light spills into the air, a landmark tower, and the
  city mirrored and streaked in the water under a bright waterline. Neon from the theme's three accents and a violet
  from its background; light themes get it washed toward the background. It's drawn in plain Kotlin through
  `RenderedBackdrop` (new, `ui/theme/RenderedBackdrop.kt`: a still backdrop drawn off the main thread at its size, up
  to a 1080p screen's pixels, keyed by value on the theme's final colors; the last three pictures are kept, so it's
  back at once after a game or the screensaver; a new size's picture waits for the window to settle and shows the one
  before, cropped, meanwhile), 70-300 ms for 1920x1080 on the Mac's JVM. The glass
  search samples `CityColors` (white included, where the spilled light stacks up); panels come out about as opaque
  as over the sunset (0.92-0.98 in the retrowave themes). `GlassContrastTest` checks against the city as drawn.
- **XMB Deep** (`ui/theme/Xmb.kt`): it was Waves' three ribbons a little stronger. Now two sheets of fine strands
  (`Sheet`, 18 and 10) sweep and turn over a darker gradient and a pool of light, fanning out where they lie flat and
  gathering where they're edge on, with sparkles (`Sparkles`) drifting up through them; whole cycles of the 40 s loop.
  Each sheet is one path, so its strands don't add up where they gather. Waves and Calm are as they were (Calm's
  description no longer claims a deeper gradient). The glass search's `deepScene` samples the strands stacked to
  `WaveColors.STRAND_STACK` and a sparkle with its halo; panels over Deep 0.90-0.97 in the themes looked at (Waves
  0.80-0.93).
- **Manga ink** (its own commit, so it can be dropped): `ui/theme/MangaPage.kt`, through `RenderedBackdrop`, in the
  ink tone (`mangaTone`) on the paper and never denser than it: Screentone is a 45-degree halftone swelling from a
  third of the way down along a wavy edge, cut by scratched highlights; Speed lines are 520 tapering focus lines around
  a clear space; Splash page was an impact burst cut out of a screentone field (it was a grey star blob), and is now
  the magazine cover below.
- **Magazine cover and Truck-kun** (Manga ink, the owner's follow-up: Splash page "looks kinda lame", he wanted the
  cover of a manga magazine advertising Phobos, and a speed lines backdrop with Truck-kun): `ui/theme/MangaCover.kt`
  and `ui/theme/MangaTruck.kt`, drawn by `MangaPage` like the other two, in its ink and never denser than it.
  - *Magazine cover* (`MangaBackdropScene.COVER`): Weekly Shonen Phobos, an original masthead (no Jump name, logo or
    emblem): WEEKLY SHONEN on an ink tab beside PHOBOS on wide pages (above it on tall ones), PHOBOS inked, cut by two
    speed stripes, ringed in paper and outlined over a toned drop shadow; an issue box (2026, No.42, PRICE ¥0); a NEW
    SERIES! ribbon with sparkles; the cover lines "ares, now on Android!" (outlined) and "...and back to Windows and
    Linux!" (a caption box); a 30+ SYSTEMS! balloon; an EAN-13 barcode that reads back as its digits (491..., the
    magazine prefix, its check digit computed); and in the middle the app icon's moon, lumpy and cratered (craters
    foreshortened toward its edge, the wall toward the light in shadow), a d-pad on a cross-shaped hollow, bursting out
    of an impact burst with rocks flying, focus lines converging on it, over the curve of Mars (ares, the planet's Greek
    name) with craters seen edge on. On a wide page the masthead and the issue box stay in the top fifth, which the
    Library's cards leave showing.
  - *Truck-kun* (`TRUCK`): a cab-over truck charging at the reader, seen from low down (`Frame`, a perspective frame)
    and tilted: a toned cab with a light stripe lettered ISEKAI, a dark windshield with two reflections and wipers,
    mirrors, roof marker lamps, a slatted grille, round headlights blazing (a glow, flare lines and an eight-pointed
    glint, long sideways), a bumper with a 1-UP plate, tyres and its shadow on the road; 340 focus lines converge on
    the grille. VRRROOOM!! grows letter by letter, and the narration is a light novel's title: "That time ares got hit
    by Truck-kun and was reincarnated on Android". On a wide page both stay in the top fifth.
  - *Lettering*: Bangers (already bundled, OFL; it has no lowercase, so the lettering is in capitals) rasterized by the
    app's own code, so it's the same on every device and in tests: `TrueType` reads the font file (format 4 character
    map, simple and composite quadratic outlines, advances, GPOS pair kerning), `Lettering` sets lines and blocks
    (turned, slanted, growing), and `Raster` fills them by signed area per pixel. `Raster` is a modified Kotlin
    translation of font-rs's rasterizer (`draw_line` in its `src/raster.rs`, `accumulate` in `src/accumulate.rs`;
    Copyright 2015, 2018 Google Inc., Apache License 2.0), credited in LICENSE (font-rs's entry, which
    `LicenseNoticesTest` checks for) and in its KDoc. Outlined letters take their rings from an exact distance transform
    between pixel centers (`Mask.distances`, within about half a pixel of the true distance). The app reads the font
    resource once, on the render thread (`MangaLettering` in `Manga.kt`); if it can't be read or parsed (`letteringOf`),
    the failure is kept and the two scenes leave the words out, and a backdrop whose render throws keeps its placeholder
    (`RenderedBackdrop` logs it).
  - *Dark themes*: where the ink is lighter than the paper, form shading turns round (`Page.shade`), so the moon's lit
    side and the headlights are the light parts on both kinds of theme; lines and lettering stay ink.
  - *Stored choices*: `SPLASH` reads as `COVER` (`mangaBackdropNamed`, `data/SettingsStore.kt`), the screensaver's
    `MANGA_SPLASH` as `MANGA_COVER`.
  - *Cost* (the Mac's JVM, warmed up, the machine quiet): 1920x1080 in 0.37-0.44 s for the cover and 0.53-0.61 s for
    Truck-kun (Speed lines 0.31-0.37 s), allocating about 27 and 35 MB (Speed lines 17 MB); once per size and theme,
    off the main thread.
  - *Previews* (outside the repo, `~/phobos-work/scratch/themes/previews2/`): `manga-cover-*.png` and
    `manga-truck-*.png`, 1920x1080 and 1080x2400 in GitHub Light, Phobos light, Gruvbox Dark and Phobos dark, each also with a
    mock of the Library drawn over it (`*-library.png`), and `manga-cover-sheet.png` and `manga-truck-sheet.png`; from
    a local JVM test (not committed) running the real code and theme colors.
- **CRT terminal** (its own commit, so it can be dropped): `ui/theme/CrtScreen.kt`, through `RenderedBackdrop`: a tube
  at rest, the phosphor glowing in a pool below the middle (`bloomAt`, falling off over the shorter side so the top
  bar's title stays on darker glass), rounded corners, soft scanlines in threes, a faint aperture grille on dark
  themes, grain, a faint reflection, and boot text burned in at the bottom left in a 5x7 font. The scanlines and the
  grille are drawn over the picture at the screen's own pixels (the picture is capped at 1080p and may be scaled); a
  faint refresh band rolls down every 9 s (`RefreshBand`, 15 frames a second, off with animations off). The phosphor
  leads its color now (80% of it in the primary, 60% before) and the bloom is stronger (0.22 dark, 0.1 light).
- **Previews** (outside the repo, `~/phobos-work/scratch/themes/previews/`): `sakura-cycle-filmstrip-*.png` and
  `sakura-cycle-horizon-*.png` (every phase, and the sunset and moonrise 4 s apart), `pixel-*.png` (each scene),
  `retrowave-night-city*.png` and `retrowave-before-after.png`, `xmb-*.png`, `manga-before.png` and `manga-after*.png`,
  `crt-before.png` and `crt-after*.png`. A local JVM test (not committed) ran the real Kotlin: the Pixel art scenes,
  night city, Manga and CRT exactly; XMB and the kept Retrowave scenes through a Java2D port of their Compose drawing
  with the real geometry and colors.
- **Checks**: `:app:testModernDebugUnitTest` 318 pass (299 on master): new `SakuraCycleTest` (8: the phases' order,
  never both up, rising out of the horizon a row at a time and sinking back, the drawn landscape unchanged at each
  run's start and end, the arc, full night from moonrise to moonset, dusk and dawn without a jump, sunrise after
  dawn), `AppearanceSettingsTest` (3), `NightCityTest` (4),
  `XmbDeepTest` (3), `MangaPageTest` (3), `CrtScreenTest` (4); `PixelSceneTest` and `PixelArtTest`'s planet and
  shooting-star tests went with Space. Reverting the Manga or the CRT commit alone leaves the app compiling and the
  remaining tests passing (this entry then needs its bullet for it taken out). No APK built locally (CI builds them);
  not yet seen on a device. With master merged, the magazine cover and Truck-kun, and the review's fixes: 333 pass in
  47 classes (on `ac7b1c115`); `MangaPageTest` has 12 (every pixel between paper and ink on light and dark themes, down
  to 1x1 and on pages as narrow as 1x30, the same page with the font read again, every scene faint on average on wide,
  square, 4:3 and tall pages, the masthead lettered over the moon, the headlights light on both kinds of theme, the
  barcode read back by the published patterns for every leading digit, a letter's counter clear and a whole word's
  margin bare, ink growing with the square of the size, a composite glyph and a missing one, a font that can't be
  read leaving the words out, Euclidean distances), and `AppearanceSettingsTest` reads a stored Splash page, backdrop
  or screensaver, as the cover.
- **Review**: an independent read-only review (a subagent) of the whole branch: NEEDS CHANGES, no High finding. Its
  three Mediums are fixed: Deep's strands drew brighter than the glass search modeled where a sheet gathers (the fade's
  gradient had 16 stops; now 64, and each sheet is one path); `RenderedBackdrop` drew again from scratch after every
  game and the screensaver, stretched the old picture on rotation, let stale renders run and had no guard against
  running out of memory; the CRT's scanlines and grille, baked into a picture scaled on screens over 1080p, beat
  unevenly. So are its Lows: the glow appearing and vanishing at a run's ends, the cycle tested through a parallel
  model rather than the drawing, the plan row's order, `CrtScreen.REFLECTION`'s and `SolidUi`'s KDoc, Deep's pool
  under the top bar's title (`XmbContrastTest` takes it now), the band's cost (`CrtUi.animatedBackdrop`, 15 frames a
  second), and MangaPage's per-pixel `hypot`s and edge scan (a 1920x1080 page now 130-320 ms). `CrtContrastTest` also
  allows for a status bar above the top bar. The magazine cover and Truck-kun had an independent review of their
  own (the coordinator's; notes and probes in `~/phobos-work/scratch/themes/review/`): OK to merge after fixes. Its
  blocker: `Raster` was a translation of font-rs's rasterizer (Apache-2.0) without credit, and the docs called it
  plain Kotlin; it's now credited as above. Its should-fixes: the NEW SERIES! sparkle's clamp threw on pages under
  about a thirtieth as wide as they're tall (now clamped only where the page is wider than it; the every-pixel test
  takes 1x30, 2x3000 and 30x4000); a font that couldn't be read threw on every lettered render, uncaught (now read
  once, a failure kept as no lettering, and `RenderedBackdrop` keeps the placeholder if a render throws); the barcode
  test's parity for a leading 6 was wrong (fixed, and every leading digit is tested). Its nits, taken: square and 4:3
  pages in the faintness test (Truck-kun's mean ink is 0.38 at 1:1 against the 0.4 bound), the every-pixel test's
  comment (`PixelKit.mix` clamps, so it catches a NaN), tests for a composite glyph (É) and a missing character, the
  dark-theme KDoc, the unused `halo` and `Mask.at`, the migration test moved to `AppearanceSettingsTest` with an
  unknown screensaver value, `Box` renamed `PageBox` (it shadowed Compose's), the EAN-13 guards named, the
  lettering's strings in capitals as Bangers draws them, and `Mask.distances`' KDoc. Left as it is: an outline ring's
  width steps by a pixel along a shallow slope, leaving dashes of paper where two masthead letters' outlines nearly
  meet.
- **For the owner**: whether the Manga and CRT backdrops stay (the PR's description has the verdict and previews);
  Sakura as the Pixel art default now that Space is gone; the magazine cover and Truck-kun on the RP6.

## PSP GE: the front end faster — vertices read a part at a time, fast mode's corners four at a time in vectors, each corner placed once — 2026-10-13

Branch `cursor/psp-ge-headroom-2b67`, on master (with #233, #234 and #235 in it).
docs/psp-core.md, part 85, has the evidence. Clean room: no PPSSPP code read, JPCSP not read. Save states unchanged
(StateVersion 26).
- **Measured first** (the M1, the bench runner at -O3 with thin LTO and the emulation thread's counters, `sample`
  profiles by line): Army of Two's 650,000 vertices a frame went to placing fast mode's corners (17%, each corner
  projected again at every triangle) and reading vertices (18%); Super Monkey Ball's 50,000 small PRIMs a frame to
  their setup, the command loop and the guest's VFPU; Ghost of Sparta's menu to `transformAll()`, `GPU::own()` (10%)
  and MoltenVK's submissions.
- **Changes**: vertices read a part at a time from the host's bytes; fast mode's corners placed once a corner and four
  at a time in vectors (NEON), skinned ones after their scalar bone sums; fast mode's models not zeroed first;
  `GPU::own()` passing over pages already its own and busy, memory's changes over unwatched pages eight at once; a
  list's words from the host's bytes; the kept settings' bones only for skinning PRIMs; each 3D triangle's corners
  placed once a vertex (`Renderer::triangleAt()`). `transformAll()` four at a time was tried and taken back: timed
  alone it was slower than base's scalar code, which the compiler already vectorizes (gathering and spreading the
  fields cost more than the lanes saved); `meshTriangles()`, which keeps only each corner's place, gains from them.
- **Numbers** (the M1, MoltenVK, the emulation thread, base -> branch, two rounds taking turns at a load of 20-120;
  fps it allows; instructions steadier): Vulkan (fast) Army of Two playing 14 -> 19 (587 -> 369 M instructions a
  frame) and its menu 20 -> 27, Ghost of Sparta's menu 25 -> 29, Colin McRae 60 -> 68, Monkey Ball 15 -> 16; Vulkan
  (accurate) Army of Two 17 -> 19, Ghost of Sparta 25 -> 33, Monkey Ball 19 -> 21; Software Army of Two 9 -> 10,
  Monkey Ball 15 -> 16; Death Jr. II, Sonic Rivals and Activision Hits Remixed 1-9% fewer instructions.
- **Exact**: base against the final code from the 21 scenes' states, 100 frames, every frame's picture and VRAM, then
  RAM and the machine's state: identical in all three renderers. Lanes written in the scalar code's expression shapes
  (Clang's fused multiply-adds); skinned bone sums kept scalar; `transformAll()` bit-identical to base's, not-a-numbers
  included.
- **Checks** (the final code, #234's reconciled branch merged): tests/psp 496/0 (sanitized), tests/allegrex 60/0,
  tests/psp/ares 318/0; GCC 14 builds the GE and its tests with -Wall -Wextra -Werror.
- **Review**: a fresh read-only review; two blockers (a not-a-number's sign in the four-lane `transformAll()`, gone
  with it; an enumerator and an int in a conditional, an error under GCC's -Wextra -Werror: 0u) and minor points,
  all taken but an ARM64 CI job.
- **What's left**: Ghost of Sparta's 1,350 GPU draws a frame, 91% split by #234's push constant `held` alone;
  Army of Two's draws split by texture and its models built on the CPU; Monkey Ball's PRIMs' setup (a Look kept from
  the last PRIM) and its VFPU (the CPU's); `light()`, now Ghost of Sparta's biggest function; the software renderer's
  triangle setup; an ARM64 CI job.

## CI: the CPU's tests on an ARM64 host and built as the app is, the RSP's NEON checked on ARM64; the system's tests can be built so too — 2026-10-13

Branch `cursor/test-plan-06-2b67` (PR #247), stacked on `cursor/test-plan-04-2b67` (#246, on #245, on #241).
Package 06 of the 2026-10-13 test and performance plan (indexed in the implementation plan by package 01's PR). Test
scripts and CI only: the app's build and its flags are unchanged.
- **The gap**: the phones run the Allegrex recompiler's ARM64 code and the N64 RSP's NEON sequences, but CI ran only
  x86-64 (where neither is executed), and every host suite was an `-O1` debug build, the CPU's and the system's
  sanitized, while the app is `BUILD_RELEASE`, `SLJIT_DEBUG=0`, `-O3` with its math and loop flags (CMakeLists.txt).
- **ARM64** (GitHub's `ubuntu-24.04-arm`, free for public repositories): PSP Core Tests gains "Allegrex CPU tests
  (ARM64)", the suite sanitized and then built as the app is, each test on the interpreter and the recompiler (sljit's
  ARM64 back end: Linux's ABI, not Android's); a new workflow, N64 RSP NEON Tests, runs
  `tests/rsp-vu-neon/run-tests.sh 3000 0x5253505655` there on changes to the RSP, the harness or sse2neon. Their
  summaries name the host (`uname -m`, the compiler) and the commit.
- **Built as the app is** (`PHOBOS_TESTS_MODE=release` in `tests/allegrex` and `tests/psp`): `BUILD_RELEASE`
  (NDEBUG: the core's five asserts out, as in the app; no test uses assert), `SLJIT_DEBUG=0`, `-O3 -ftree-vectorize
  -funroll-loops -fno-math-errno -fno-trapping-math`, no sanitizers unless `SANITIZE` names some; not the app's
  link-time optimization (`-flto=thin` is clang's, CI's compiler is GCC). sljit's C file and the C++ files get the
  same mode. The default stays the sanitized debug build. CI runs it beside each sanitized run in both Allegrex jobs
  (a second step); the ARM64 lane fails unless its host is `aarch64`. `tests/psp` takes the same switch, but its
  lane is a draft of its own (#249) because GCC rejects four of the files it builds (below), so package 06's
  release lane is met for the CPU's tests only until those are settled.
- **A finding: `tests/psp` built as the app is doesn't get through GCC.** Its lane ("PSP system tests, built as the
  app is", run 38439997353) failed: GCC 13 at `-O3` with `-Werror` reports `-Wmaybe-uninitialized` in
  `ares/psp/ge/four.cpp` (lines 44, 231, 243, 262, through `pickLanes()`, `texelsFour()` and `spriteFours()`), for
  `Four::depth` in `spriteFours()`'s `Four four;` and for `downAxis[]`. They're false positives, which `-Werror`
  turns into an error (the review's reading): `four.depth` is read only by `writeDepth()`, which runs only where the
  depth is written (a clear's depth, or `depthWrite`), and `depthFirst()`, always run first, assigns all four lanes
  whenever the depth is tested, or cleared; `depthWrite` is only ever on with `depthTest` (`pixel.cpp:93`), which
  GCC can't see through two separate flags. `downAxis[]` is read only for a textured sprite, and every textured path
  assigns all three. Only sprites warn because `triangleFours()` already sets `four.depth = s32x4{};` before
  `depthFirst()`.
  Apple clang 21 compiles the same `ge.cpp` at the same flags with `-Werror` cleanly (silent even with
  `-Wconditional-uninitialized`), and the app is built by the NDK's clang with `-w`. The review's suggested fix,
  `four.depth = s32x4{};` in `spriteFours()` as triangles do and `s32x4 downAxis[3] = {};` (not `Four four{}`, which
  zeroes ten vectors per four), is `four.cpp`'s alone, outside #240's files; it isn't made here because it's the
  software renderer's innermost loop and wants its speed measured. `-Werror` isn't loosened: the lane stays red in
  its draft. That run stopped at the first failed compile, `ge.cpp`, the 8th file; #249 now compiles four at a time
  (`PHOBOS_JOBS=4`, where every file is compiled before the build fails), and its run 38462046887 has GCC rejecting
  4 of the 51 files. Beside `ge.cpp` (`four.cpp`, as above), three fail with `-Wstringop-overflow`, each reading as
  a false positive too (the review's reading, not checked with a compiler): `kernel.cpp` at `serialization.cpp:976`
  (64 times, "writing 2 bytes into a region of size 0"), a range-for over `Sample samples[64]` that writes in bounds,
  added by master's right stick (`2b5a9209a`, merged with #230 since; `kernel.cpp` compiled cleanly in the earlier
  runs, before that commit); `desktop/PspDiscInfo.cpp` in nall's string allocator (`memory.hpp:137`, 24 bytes into
  8, through `pspIconKey()`'s `Hash::digest()` and `hex()`), though `reserve()` allocates only past the 23-character
  inline buffer; and `tests/psp/crypto.cpp:29`, `put32()` inlined into `kirkPrivate()`, writing into `other`, a copy
  of `input` at least 0x8f bytes long (0x180 here), whose data GCC takes for address zero. The last two were there
  before, never reached. None is fixed here: `four.cpp`'s wants measuring (above), #229 is changing the kernel's
  serialization, nall is shared with every ares system, and a change to `crypto.cpp` alone wouldn't turn the lane
  green.
- **Settled** (#249, 2026-10-15, the owner's call: fixed in the code, `-Werror` kept): each is written so GCC follows
  it, with nothing it does changed. `spriteFours()` sets `four.depth = s32x4{};` before `depthFirst()`, as
  `triangleFours()` does, and starts each row's `downAxis[]` at `{}`; the loaded samples' right stick is centred in
  the samples' own loop in `serialization.cpp`, not in a loop of its own that GCC turned into 64 stores past
  `Controller::buttons`; `pspIconKey()` writes the SHA-256's bytes (`output()`) as lowercase hexadecimal itself, not
  through nall's string (nall unchanged; `iconKeys()` now pins a key's value, so the cache's file names stay as they
  were); `crypto.cpp`'s `put32()` stores through `at()`. With those four gone, the lane's next run (38801413445)
  found a fifth in `kernel.cpp`: `pools.cpp:216`, `-Warray-bounds` in a `std::vector`'s insert of an initializer
  list, which aarch64's GCC gives on master too; `poolStatus()` now keeps its words in a `u32[5]` with their count.
  Checks: GCC 13.3 (Ubuntu 24.04 in Docker, arm64: x86-64's packages couldn't be fetched) at the lane's flags builds
  all 52 files (master's `spins.cpp` the 52nd), as CI's x86-64 lane does (run 38803233475). The 21 scenes in
  Software, Vulkan fast and accurate, master's bench runner against the fixed one: all 63 sets of hashes master's.
  Army of Two's gameplay and menu in Software, its sprite-heaviest scenes (42% and 32% of their fours sprites'): the
  same instructions a frame within the runs' spread (+0.00% and +0.02% drawn on one thread, -0.05% and +0.05% on
  four), the times within their noise. tests/psp 555/0 sanitized, unsanitized and built as the app is (Apple clang),
  tests/allegrex 78/0, tests/psp/ares 348/0.
- **What the lane's first full run found, settled for GCC** (run 38803233475; the tests had never run built so on
  Linux): "ge vertices read in runs as one at a time" failed, 1,248 of its 3,000 rounds differing, with aarch64's GCC
  13.3 as well. A float vertex field holding a signaling NaN read quieted one vertex at a time (`readTarget()`: the
  number times its unit, 1 for a float, multiplied at run time) but as it was in runs (`readVerticesFrom()`: GCC,
  seeing the unit is 1 in `byFormat()`'s float case, drops the multiplication, as its default `-fno-signaling-nans`
  allows). Clang, Apple's and the NDK's, keeps the multiplication in both readers, so the app's builds read both the
  same. The owner's call (2026-10-15): GCC's builds read as clang's. Under GCC alone (`#if defined(__GNUC__) &&
  !defined(__clang__)`), both readers take a float through `vertexFloat()`, which sets a NaN's quiet bit in whole
  numbers, which no optimizer may drop; clang compiles master's reads, so the app pays nothing. (With the quieting
  on clang too, `003dff253`, the Mac's bench in Vulkan fast had Ghost of Sparta's menu at +1.24% emulation-thread
  instructions and Super Monkey Ball at +0.83%; `-fsignaling-nans`, experimental in GCC's manual and needed over the
  whole PSP core, wasn't taken.) Clang relies on its own multiplication, which a new test pins on the Mac (CI's
  system tests are GCC's): "ge vertex floats that are signaling NaNs read quieted, in runs and one at a time" reads
  twelve values, four of them signaling NaNs, in every float of a 3D vertex both ways and checks their bits. With
  the unit made a 1 that clang sees, so that it folds the multiplication away, it fails on the Mac (each signaling
  NaN read as stored, in all ten floats, both ways), while "ge vertices read in runs as one at a time" passes.
  Checks: clang's code for `ge.cpp` byte-identical with master's `vertex.cpp` and with this one (Apple clang at the
  lane's flags, the NDK's 28.2 at the app's, modern and legacy), its six vertex readers the same as master
  `1c75c5600`'s own build's; the 21 scenes in Software, Vulkan fast and accurate, master's runner against this one:
  all 63 sets of hashes the same; tests/psp 568/0 built as the app is, unsanitized and sanitized (Apple clang). CI's
  lane passed with this GCC code (run 38827011901, on `003dff253`).
- **Checks** (the Mac, arm64, Apple clang 21): `PHOBOS_TESTS_MODE=release tests/allegrex/run-tests.sh` 60/0 (7
  objects of its own, 18 s with the run); `tests/rsp-vu-neon/run-tests.sh 3000 0x5253505655`: 10,420,000 checks, no
  NEON or reference mismatch; actionlint on the workflows. CI on #247 (run 38439997353): Allegrex 60/0 sanitized and
  60/0 built as the app is, on x86-64 and on `aarch64` (GCC 13.3); RSP NEON (run 38439997492, `aarch64`): 10,420,000
  checks, no mismatch. Merged with the stack below: runs 38448910923 (`c2d54f724`), 38454946526 (`014c037b1`) and
  38461962140 (`c31d1f7b8`, master's #230 in), every job green; RSP NEON 38454946627 and 38461962244.
- **Negative controls** (draft PR #248, closed and its branch deleted; runs 38440061339 and 38440061297): EOR emitted
  with ORR's encoding in sljit's ARM64 back end: the ARM64 lane failed ("alu (recompiler)" and the recompiler against
  the interpreter, 0x16e mismatches; x86-64 doesn't build that back end, and passed); the RSP's NEON `ADD_4S` emitted
  as `SUB_4S`: "NEON differs from SSE" at VMADM, failed.
- **Review**: an independent read-only review checked the mode switch (the default's flags, so its objects, are
  unchanged), the workflows, the logs and both controls, and re-ran the release-built Allegrex suite, the RSP harness
  and `ge.cpp` at the release flags on the Mac. It found these notes wrong on `four.cpp` (they called the warnings
  indeterminate reads; above, corrected) and on the asserts' count, and the heading claiming the system's tests built
  as the app is; all corrected. Its suggestions taken: the ARM64 lane checks its host; the workflow's note on the kept
  results says which runs keep them. A delta review, after #249's run compiled every file, found the finding stale
  (it said GCC's other findings would show once `four.cpp` was settled; the four files above, from that review's
  reading of the log, checked against it) and "every host suite was a sanitized `-O1` debug build" wrong (the ares
  system's suite and the runner aren't sanitized); both corrected. The next delta review: PASS; its points, all
  wording, are taken (clears in `depthFirst()`'s condition, `crypto.cpp`'s reason, `other` named, which runs
  compiled `kernel.cpp`).

## Runner: a JSON report of what ran, a pinned start time, captures timed apart and stale ones said — 2026-10-13

Branch `cursor/test-plan-08-2b67` (PR #250), stacked on `cursor/test-plan-03-2b67` (#245, which changes the runner's
build script and smoke test; on #241). Package 08 of the 2026-10-13 test and performance plan, adapted (indexed in
the implementation plan by #244). The runner only: no core change; every option, output line and default as before
(but a malformed number is now refused, and a bare file name for `--wav` or a saved state, which ended the run in an
exception, now works), only new options and new lines where they apply.
- **The problem**: speed comparisons ran through private scratch patches; a run didn't record what it had actually
  used (a Vulkan renderer that failed falls back to the software one); the PSP's clock started at the host's time,
  so two runs of a game differed; the frame loop's time included its captures (waiting for each PNG's picture,
  writing it, the save state); and a PNG whose wait ran out was written as if fresh.
- **`--report FILE`**: the run as JSON, `"report": "phobos-psp-runner 1"` (README, "The report"): the runner's build
  (the commit, from `build.sh`; the compiler's version string; debug or release; FFmpeg or not), the game (path,
  kind, title, disc ID, size), the options given (null where not: renderer, resolution, engine, GE threads, late
  frames, frames, start time, state, presses, script, the PNGs' schedule and folder, WAV, state saved, memory stick,
  fonts, pipeline cache), what ran (the renderer the core used, the GPU's device, whether the GPU was lost during
  the run, the resolution, engine, GE threads, the clock it began at, `TZ` and the host's UTC offset then), the
  result (frames, the program's end, the loop's seconds and its captures', stale captures, clock restarts, each
  capture's frame and the frame running when its picture arrived, missing functions, last note) and the GPU's
  counts. Text stays UTF-8: a byte that isn't part of a UTF-8 character (a game's Shift-JIS output, a Latin-1 file
  name) is written `\u00XX`. The run's folders (the PNGs', the WAV's, the state's, the report's) are made before it
  runs, and one that can't be made ends it (exit 1, said, no scratch left); the report is written beside its name,
  closed, checked and renamed, so a failed write fails the run (exit 1) without leaving part of one.
- **`--start-time SECONDS`** (up to 4294967295; every number the runner reads, a press's frame too, is decimal digits
  within 32 bits, and a stick's value digits after an optional minus sign; anything else refused, not wrapped or cut
  short): the kernel's clock set right after power-on (`Kernel::power()` takes the host's time, the core's only read of
  the host clock; local time follows `TZ`), before anything runs. A game that starts itself again (sceKernelLoadExec,
  largeRestart) powers the kernel on again, which takes the host's clock again: the kernel's run returns as the new
  program is started, before it runs, and `System::run` calls it once a frame, so the runner sets the clock back at the
  next frame's top (and after the last frame), says so on standard error and counts it (`clock restarts`). A loaded
  state keeps its own clock (said on standard error); a restart after it takes the time asked for.
- **Captures**: the summary adds `capture seconds:` (their time, and the speed without them) where there were any;
  a PNG with no picture presented since the last one within 2 s is said on standard error and counted (`stale
  captures: N`); with no picture at all yet there's no PNG (a 0-by-0 one was written before), said and counted.
  Which picture a PNG takes is as before (the first presented since the last PNG): the coordinator's library
  comparisons keep their meaning. ares's screen thread drops pictures it doesn't take in time, so on a busy host a
  PNG can be older than its name; the report's captures give the frame that was running when each picture arrived.
  `--screen-stops-at F`, for the runner's own test, takes no pictures from frame F on. If the GPU is lost during a
  run, `gpu lost:` says the software renderer drew from then on. The new summary lines begin with words of their
  own, and only the speed's has "frames per second" in it: the coordinator's and agents' scripts find lines by those.
- **`build.sh`**: the commit described without `--dirty`, which rewrites the checkout's git index under its lock
  (another agent's `git add` in the same checkout could fail); `-dirty` comes from `git status` with
  `GIT_OPTIONAL_LOCKS=0`, tracked files only. Only this checkout's own repository is described (a copy inside
  another says `unknown`), and the name keeps only letters, digits and `._+-`.
- **Kept**: every line the speed kits' patches anchor on. Each kit's whole chain (bench-patch.py, then every other
  patch in its folder) applies to this runner exactly where it applies to master's (`cpu-round2`, `ge-headroom`,
  `reconcile`, `vulkan-speed`; `cpu-speed2`'s bench-patch.py already fails on master's runner).
- **`tests/psp/ares/runner-test.sh`** adds: the summary's speed on the one line with "frames per second" and the
  captures' time on `capture seconds:`; the report checked (JSON; the options not given null; the software renderer,
  the recompiler, no GPU lost; 60 frames, the program running on; both captures fresh, at frames 30 and 60, each
  picture arrived by its frame); three runs of cube.elf saving a state at frame 30, two started at 1234567890
  (identical states, no clock restarts) and one at 1234567999 (another); the first state loaded with another start
  time (the state's clock kept, said, not set again); a start time past 32 bits, a number with more after it, a
  press's frame past 32 bits, and a stick's value with more after it, a space before it or below -32768 refused, and
  the sticks' extremes taken; captures at frames 19, 20 and 40 with `--screen-stops-at 20` (19's fresh, as it waits
  for a picture before the screen stops; 40's stale; 20's either, as pictures came while 19's PNG was written; each
  said and counted as it was, each with a PNG; 40's PNG the same as 20's, the last picture before the screen
  stopped); a capture with no picture at all (said, no PNG, in the report, whose new folder the runner made); and a
  game's name with a Latin-1 byte (the report still JSON, the byte `\u00e9`), where the filesystem takes such a name
  (Linux's do; macOS's don't, and there it's left out, said).
- **Checks** (the Mac, `PHOBOS_JOBS=3`, its own TMPDIR): `runner-test.sh` passes (38 s with the core rebuilt after
  master's #230); the kits' patch chains as above. `jsonText()` alone, in a scratch harness, on 18 byte strings
  (UTF-8 of 2 to 4 bytes and U+10FFFF kept; Latin-1, Shift-JIS, overlong forms, a surrogate, past U+10FFFF, a lead
  byte F5, cut-off characters and lone continuation bytes escaped; quotes and control characters): each output
  valid JSON and equal to Python's strict UTF-8 reading. A real restart (a disposable patch filling the kernel's
  own sceKernelLoadExec request with cube.elf, so `Kernel::run` calls `loadExec()`): at frame 10, "frame 10: the
  game started itself again ..." and the kernel's clock 1234567890 at the end, two such runs saving identical
  states; with the runner's hold off, the host's clock at the end (1791923398) and another state; restarts at the
  last frame and at frame 1 (no cycles yet: seen by the clock's change) set back too; without `--start-time`,
  nothing said, `clock restarts` null. Negative controls, each in a disposable copy: the start time not applied,
  "runs started at different times saved the same state" (fails); the stale capture's detection removed, `stale
  captures: 1` missing (fails). After the delta review's changes: `runner-test.sh` passes again (13 s, the runner
  alone rebuilt), the kits' chains as on master's runner, shellcheck clean; the three-capture case alone six times,
  19 and 20 fresh and 40 stale each time. The review's suggestion for that case, frame 19 without frame 20, failed
  on its first run here: frame 40 found a new picture, which had come while frame 19's PNG was written. The restarts
  above again, the same, and one whose program can't start (bytes that aren't an ELF): "a program the game asked for
  couldn't start, which ended the game ...", the program ended, the clock still the one asked for. A report folder
  that can't be made: said, exit 1, no scratch left; malformed numbers (`12abc`, `+5`, a leading space, a negative
  that wraps to 1, a press's frame past 32 bits or `1x`) refused. After the second delta review's changes:
  `runner-test.sh` passes (14 s), the kits' chains as on master's runner, shellcheck clean; with the stick's old
  read put back (`std::stol`), the test fails ("not refused: --press 1:L-StickX:12abc"). After the third delta
  review's wording: `runner-test.sh` passes (15 s), the kits' chains as on master's runner, shellcheck clean.
- **Review**: an independent read-only review (it re-ran the smoke test and the kits' chains, read the restart
  path, and ran error cases) found two majors: the start time lost when a game starts itself again, while the
  report still showed the pinned clock; and text not always UTF-8 (a Shift-JIS note). Its minors: the report's
  write errors late or unseen (no folder made, the stream checked before closing); captures not saying which frame
  they show; `ran.renderer` saying Vulkan after the GPU was lost; `git describe --dirty` rewriting the index. Its
  nits: `nan` for the GPU's wait with `--frames 0`, a misleading message and a 0-by-0 PNG with no picture, the
  README's example and test sentence, line lengths, `asked` mixing nulls and defaults, the start time with a loaded
  state unsaid, `build.sh` with a quote in a tag or inside another repository, start times wrapping past 32 bits.
  All taken (above), and of its suggestions the compiler, build mode, FFmpeg, the UTC offset and the options given.
  Not taken: the source string out of runner.cpp's compile command (each commit compiles the runner again in a
  checkout's build folder; CI builds once; pruning old objects is the objects cache's, for every suite), a report
  when a run fails early, the driver's version (no accessor in the core), the program's hash, and a test program
  that reads the clock (none in the repository prints the date; building one needs pspsdk). A delta review (PASS;
  it re-ran the smoke test and the kits' chains, 16 runs through the restart path (controls without a restart among
  them) and `jsonText()` on 978,253 strings, and compiled runner.cpp with GCC 13.3 and 14.4 under `-Werror`) found
  one minor: the two new summary lines began with the words of existing ones ("frames per second, captures left
  out", "gpu: lost"), and scripts in the agents' scratch find lines by those (one would take the wrong line's speed,
  one would overwrite the GPU's counts, three greps print both); taken (`capture seconds:`, `gpu lost:`, and the
  test's check that one line has "frames per second"). Its nits, all taken: a press's frame still wrapping past 32
  bits and a number with more after it accepted (one digits-only reader for both now, and the test refuses both); a
  folder that can't be made ending the run in an exception (said now, exit 1, before the scratch is made); a
  capture's freshness read apart from its picture (now in the same lock); frame 20's fresh capture possibly missed
  on a starved host (the three captures above); the README's `asked` and title; a program that couldn't start
  called a restart (said as what it is). A second delta review (PASS; the smoke test again, the three captures 10
  times, real failed starts at frames 1, 10 and 30, four ways a folder can't be made, and the stale check removed,
  which the test catches) found five nits. Taken: a stick's value still read loosely (`12abc` ran as 12; now an
  optional minus sign, then digits, and the test refuses both kinds and takes the extremes); the bare file names
  above unsaid; frame 40's PNG, always frame 20's picture, now compared; "16 real restarts" and the README's `clock
  restarts`. Left: a scratch script outside the repository that diffs two runs' summaries would count `capture
  seconds:`, a time, as a difference (`scratch/dialogs/compare.sh`, if it's used again: one more pattern for its
  filter). A third delta review (PASS; 33 edge cases run against the runner, and no press script in the scratch
  folders refused by the strict read) found three nits, all taken: two lines left short, a run missing below, and
  `-0` read as 0 though the wording said a minus sign only below 0 (now an optional one; leading zeros are read as
  every number's are).
- **CI**: PSP Core Tests green on each earlier head (runs 38443751682, 38449049095, 38454982676, 38463437399,
  38468789060 and 38471152830, the runner's test passing; on Linux the last three ran the name not in UTF-8).

## Tests: a skip says what's missing, and fails where the lane must have it; the Allegrex measurements unpacked by content — 2026-10-13

Branch `cursor/test-plan-04-2b67` (PR #246), stacked on `cursor/test-plan-03-2b67` (#245, itself on #241). Package 04
of the 2026-10-13 test and performance plan (indexed in the implementation plan by #244). Test harness, test
scripts and CI only: no core or app change, no check loosened.
- **The problem**: with no Vulkan GPU, `tests/psp/gpu.cpp` said "skipped" once and every GPU group then printed
  "pass"; the groups needing the test programs or the GE's measurement rounds returned early, silently, without
  them. A green run didn't say what had run.
- **Now** (`tests/allegrex/harness.hpp`, both suites): `skip(capability, why)` marks the group: it prints `skip
  NAME (capability: why)` rather than `pass` (a failure still prints FAIL), and the last line adds ", N skipped"
  where any were (`497 groups, 0 failures` otherwise as before). A capability named in `PHOBOS_TESTS_REQUIRE` (a
  comma-separated list, spaces around a name left out) makes that skip a failure, once a group for each reason; a
  name no test skips for (a typo) fails the run before the groups. `PHOBOS_TESTS_REPORT=FILE` writes each group's
  result, group and reasons (what was required and missing first), a tab between, and a last `summary` line (a
  report without it is of a run that didn't finish). Skips declared: `vulkan` (no renderer: `renderer()` itself),
  `vulkan-fast`, and the GPU's capabilities the groups test for (`vulkan-transforms`, `-palettes`, `-moves`,
  `-reads`, `-unordered-reads`, `-2x`, `-3x`, every loop over the scales now saying so, and `-stencil-export`, where
  only one of a group's two ways ran); `programs` (`testPrograms()` and `testProgram()`, a program missing from the
  folder too); `ge-round3` (the variable unset, or a file of it missing, or no round 3 to compare with), `ge-round6`.
  The ares suite's run without programs fails where `programs` is required, and its last line says when their
  checks were skipped.
- **CI** (PSP Core Tests): the system lane requires `vulkan`, `vulkan-fast`, `-transforms`, `-palettes`, `-moves`,
  `-reads`, `-2x`, `-3x`, `-stencil-export`, `programs`, `ge-round3` and `ge-round6` (not `vulkan-unordered-reads`:
  lavapipe reads in order, the one skip its runs already printed); the ares lane requires `programs`. The Allegrex
  and system lanes keep their reports as artifacts.
- **The Allegrex measurements**: unpacked again when their packed file's SHA-256 (kept beside the unpacked file)
  changes, not by time: a tree copied with its times kept, as the agents' scratch copies are, can hold an older packed
  file than the folder. xz checks each packed file as it unpacks; a damaged one stops the run, naming it.
  `SHA256SUMS*` there are the sums of the PSP's own output files, of which 13 of the 25 packed here are kept whole
  (those, and three plain files the sums list, are now checked against them each run) and the rest in part (the 16k
  sets) or not at all; round 6 keeps verifying its own `SHA256SUMS` as before.
- **Kept**: every check and tolerance; the `main()` loop's first lines, which three agents' scratch filters patch;
  the PSP's optional pspautotests samples and `loader.cpp`'s own program check (#243 changes that file) stay as they
  were.
- **Checks** (the Mac): the changed test sources compile with `-Wall -Wextra -Werror` (syntax-only, both flag sets);
  `tests/allegrex` 60/0 with its report (60 pass lines and the summary line), a second run unpacking nothing, and a
  disposable copy with one packed file damaged: exit 1, "tests/allegrex/measured/vadd-spread.bin.xz doesn't unpack
  (damaged?)". After the review: `PHOBOS_TESTS_REQUIRE="vulkan, programs ,ge-round3"` taken (60/0);
  `"vulkan,Programs"` fails before the groups ("names Programs, which no test skips for"); an unpacked file kept
  whole changed after unpacking fails ("fpu-arith-safe.bin isn't the file the PSP made (SHA256SUMS3)"), restored
  60/0. CI on #246 (run 38439504812, at `60e8f1e14`): the system suite "497 groups, 0 failures, 1 skipped"
  (vulkan-unordered-reads: lavapipe reads in order), with every requirement met; the ares system 318/0; Allegrex
  60/0; the runner's test passing; the reports kept. After the review: run 38448816745 (`aa197ced0`, the stencil
  export required too: 497/0, 1 skipped) and run 38454904089 (`0c44ebd4b`, with #243's groups: "502 groups, 0
  failures, 1 skipped", the report 501 pass lines, 1 skip and its summary line).
- **Negative controls** (run 38439595119, dispatched on a disposable branch whose workflow ran the lanes these ways;
  deleted after): `cube.elf` missing from the programs while they're required: "497 groups, 3 failures, 1 skipped",
  each FAIL naming it ("programs is required here (PHOBOS_TESTS_REQUIRE), and cube.elf isn't in
  PSP_TEST_PROGRAMS"); the ares lane with no programs while required: "33 checks, 1 failed, the programs' checks
  skipped"; Vulkan off (`PSP_GPU=0`) with nothing required: passes, "497 groups, 0 failures, 45 skipped", each GPU
  group `skip ... (vulkan: PSP_GPU=0)`; Vulkan off while required: "497 groups, 45 failures" (every group that
  needs the GPU). The branch is deleted.
- **Review**: an independent read-only review (it re-ran the Allegrex suite and damaged, truncated and back-dated
  packed files, and linked a probe against the harness) found the major gap that `ge-round3` was required only as a
  variable: with the folder set but a file missing, `curves.cpp` went on silently, and `measure.cpp` only describes;
  and that the requirement list was matched exactly (a space, a capital or a typo dropped a requirement), the report
  left the reason off FAIL lines, some scale loops and the stencil-export way were dropped silently, and these docs
  were stale. All taken (above); its suggestion of checking the files kept whole against the PSP's sums too. A delta
  review passed it; its nit taken: `stencilFormats()`'s loop over the scales says its skip too.

## Tests: the host suites' objects kept by what they include and how they're compiled, a checkout's apart — 2026-10-13

Branch `cursor/test-plan-03-2b67` (PR #245), stacked on `cursor/test-plan-05-2b67` (#241, the CI lanes, so every
script changed here runs in CI). Package 03 of the 2026-10-13 test and performance plan (indexed in the implementation
plan by #244). Build scripts only: no core, app or test source changes.
- **The problem** (on a Mac five agents share): the four PSP build scripts kept objects in one folder per suite in
  `${TMPDIR:-/tmp}`, shared by every worktree, reused while their top source was older than them. A header edit,
  another worktree's newer object, another compiler or sanitizer setting, all reused the old object; libco's was
  never rebuilt; two worktrees' runs wrote the same test binary.
- **`tests/objects.sh`** (sourced by `tests/allegrex`, `tests/psp`, `tests/psp/ares`, `tests/ngcd` and
  `tools/psp-runner/build.sh`): an object is named by its compile command (the compiler as named, every word of CC
  or CXX, where it is and its `--version`, every flag, the source's path, and `CPATH`, `C_INCLUDE_PATH`,
  `CPLUS_INCLUDE_PATH`, `SDKROOT`, `MACOSX_DEPLOYMENT_TARGET`), so another checkout, compiler, sanitizer or define
  makes its own; it's kept while nothing on the compiler's own list of what it read (`-MD`, system headers included:
  ares's and nall's are included as system headers) is newer than it, or gone, compared with `find -newer`
  (nanoseconds, where macOS's bash 3.2 `-nt` takes whole seconds). An object takes the time its compile began, so a
  header saved during the compile is newer than it. Objects and programs are written beside their place and renamed
  into it, so runs at once are safe and a killed build leaves nothing taken for finished; a program's name carries a
  hash of its objects and link flags, so runs of two configurations in one checkout each run their own. Each
  checkout gets its own folder in the suite's (the cksum of its path, as the NGCD script already did; its
  `.phobos-checkout` file names the path), deletable between runs: per configuration a checkout keeps 21 MB
  (Allegrex), 34 MB (NGCD), 40 MB (the ares system) or 46 MB (the runner, with its copy) until then (`tests/psp`'s,
  the largest, not measured). What times can't tell is written down: a
  new header shadowing another, a file replaced by an older-dated copy. Every source is its own object now, the
  test suites' too (they were one compile each run), and each compile's line says whether it's sanitized (in the
  sanitized suites nall and sljit aren't). `PHOBOS_JOBS=N` compiles N at once (1 by default).
- **Kept as they were**: `TMPDIR=X tools/psp-runner/build.sh` still leaves `X/phobos-psp-runner/psp-runner` (a copy
  of this checkout's, which the build's last line names; `runner-test.sh` takes the latter); NGCD's build-dir
  argument; the FFmpeg cache; every flag. The round-6 and Allegrex measurements unpack into the checkout's folder,
  each file through a temporary one named by the run, a failed unpack stopping the run.
- **`tests/objects-test.sh`** (in CI's Allegrex job; seconds; 21 cases): a small C program built cold (2 compiles),
  warm (0), after its header changes (1, and the program sees it), and a header found as a system header (1), with a
  define added (2) and taken away (0: the first objects kept), with the compiler named with another word (`cc
  -DEXTRA=9`: 2, the word taken), the same compiler at the same path reporting another version (2), a header saved
  while its includer compiled (the compiler holding before it writes the object: compiled again next time), a header
  changed within the second (its time and the object's pinned at .2 and .7 of one second; said as a skip where the
  file system keeps whole seconds), killed
  mid-compile with the build (fails, then the same command compiles everything: nothing kept from the cut), with the
  first header on the include path gone (1, the next one taken), a compile failing among two at once (the build
  fails), a header found through `CPATH` and then another `CPATH` (compiled again), a program for each
  configuration, and two checkouts built at once into one folder (each its own program and folder). Negative
  controls, each a disposable copy of the library: the compiler's name, or its version, out of the key; the
  object's time its compile's end; `-MMD`; bash's whole-second `-nt` (failing every run, 3 of 3); a failed compile
  among several ignored; the old rule (the source's time alone); flags out of the key; the environment out of the
  key; one program name for every configuration; objects written in place; one folder for all checkouts; a gone
  file taken as unchanged: each of the 13 fails at least one case.
- **Checks** (the Mac, bash 3.2, Apple clang 21, `PHOBOS_JOBS=3`, a TMPDIR of its own): at `86a5a80d0`,
  `tests/objects-test.sh` 21/21, twice; with the scripts at `f1bc43c89` (before the delta review's changes),
  `tests/allegrex` cold 7 compiles (60/0), warm 0, after touching `ares/psp/cpu/allegrex.hpp` 4 (the four that
  include it; 60/0); `tests/ngcd` cold 13 (2835/0), warm 0; `tests/psp/ares` cold 25 (318/0), warm 0;
  `runner-test.sh` cold 42 s, warm 4 s, its program `psp-runner-<hash>` and the copy left in `phobos-psp-runner/`. CI
  on the reviewed scripts (run 38448696924 at `a476470ec`; Linux, GCC 13, bash 5): `tests/objects-test.sh` 18 cases,
  0 failures (the case within the second ran), Allegrex 60/0, the system suite 497/0 (every source its own object
  now), the ares system 318/0, the runner's test passing; NGCD 2835/0 (run 38448696864). CI at `86a5a80d0` (run
  38454888271): `tests/objects-test.sh` 21 cases, 0 failures (the case within the second ran), Allegrex 60/0, the
  system suite 502/0 (#243's five groups more), the ares system 318/0, the runner's test passing; NGCD 2835/0 (run
  38454887111). A second delta review passed it; its nit taken: the leftovers' pattern is the 16 hex digits and a
  process number, so a folder's own files of a like name aren't taken. At `fefce4e5f`, with that pattern: on the Mac,
  `tests/objects-test.sh` 21/21, and the pattern alone under bash 3.2 removing the six kinds of leftover
  (`psp-runner`, `allegrex`, `psp`, `psp-ares`, `ngcd`, the test's `program`) and keeping the eleven like names tried
  (the old pattern's false match; 16 letters that aren't hex digits; uppercase hex digits; 15 or 17 of them; none; no
  number after `.part-`; a dated file name with spaces; one under two hours old; one in a subfolder; a folder); CI
  run 38461715565: `tests/objects-test.sh` 21/0, Allegrex 60/0, the system suite 505/0 (master's #230 three groups
  more), the ares system 328/0, the runner's test passing; NGCD 2835/0 (run 38461715535).
- **Review**: an independent read-only review found a major hole (a word after the first in CC or CXX wasn't in
  the key: `CC="cc -DEXTRA=9"` after `cc` compiled nothing and ran the old program), and minor ones: a header edited
  during a compile never rebuilt, the stale-file cleanup reaching anything in a folder handed to NGCD, an exported
  `BUILD` overriding the per-checkout folders, four mutations the test let through, the disk use unsaid, the
  include-path variables not keyed, a failed unpack carrying on, programs per checkout rather than per
  configuration. All taken (above): the cleanup only in the objects folder, the folder override named
  `OBJECTS_BUILD`, the test's new cases each failing the mutation it was missing. A delta review found every fix
  right in code, and the whole-second mutation caught only by chance (the two times not pinned), the environment's
  keys and the per-configuration names untested, a cut-short link's leftovers no longer cleaned, and the docs behind;
  all taken: the times pinned, two cases added, programs' leftovers cleaned by their hashed names, the folder's file
  named `.phobos-checkout` (a folder handed to NGCD may have a `checkout`), the README's line and these checks. A
  recheck at `fefce4e5f`, after #241's merge of master's #230: PASS. Its two trivial nits: "the final scripts" for
  checks run before the leftovers' pattern changed (taken: each check names its commit now), and the pattern
  checking only the first character after `.part-` (left: such a name would also need 16 lowercase hex digits right
  before `.part-`, be two hours old and sit directly in the build folder: the suite's own, or the one
  `OBJECTS_BUILD` or NGCD's argument names).

## CI: the PSP core as an ares system, the headless runner and the Neo Geo CD's checks in CI; the triggers complete — 2026-10-13

Branch `cursor/test-plan-05-2b67` (PR #241), on master (`4788125f1`, #236). Package 05 of the 2026-10-13 test and
performance plan (an outside audit of master `b3507c1db`, triaged against `9333a8d7a`; #244, package 01, indexes it
in the implementation plan). CI only: no core, app or test code changes.
- **The gap**: three suites ran only by hand: `tests/psp/ares/run-tests.sh` (the whole PSP core as an ares system,
  318 checks on the last recorded runs), `tests/psp/ares/runner-test.sh` (the headless runner built and running
  cube.elf) and `tests/ngcd/run-tests.sh` (the Neo Geo CD's layout and state checks). PSP Core Tests' paths also
  missed what its builds compile: sljit and its configuration, Vulkan's headers, ares's framework beyond `types.hpp`;
  and the byte files `tests/psp/dialogs.cpp` reads from the app's test resources.
- **Now**: PSP Core Tests has two more jobs, "PSP as an ares system" and "Headless runner" (each with the FFmpeg
  build cached as the system job has it), and a new workflow, Neo Geo CD Tests, runs `tests/ngcd/run-tests.sh` on
  changes to the Neo Geo core, the 68000, the Z80, ymfm, ares's framework, nall, libco or the tests. PSP Core Tests'
  paths add `ares/ares/**`, `libco/**`, `thirdparty/sljit/**` and `thirdparty/sljit*`, `thirdparty/Vulkan-Headers/**`,
  `thirdparty/libchdr/**`, `tools/psp-runner/**`, `desktop/Platform.hpp` (which `desktop/PspDiscInfo.cpp`
  includes) and `android/app/src/test/resources/psp-*` (the last as #230 adds it, so that line's conflict resolves
  to this one); Neo Geo CD Tests' include sljit's (ares.hpp includes `sljit.h`). Both workflows also run for version tags (a tag's push takes no path
  filter). Every suite's last line, its count, goes in the run's summary with the commit; the suites' output goes
  through `tee`, so their steps run as `shell: bash` (pipefail) to keep a failure failing, and a suite that left no
  output says so in the summary. Both workflows' runners are pinned to Ubuntu 24.04 (ubuntu-latest moves to 26 on
  2026-10-19: a new GCC, a new FFmpeg build, maybe new `-Werror` warnings, taken on purpose instead), each job has a
  time limit (20 to 60 minutes), and only a PR's newer push cancels its run: every other run (master's, a tag's, a
  dispatch) is a group of its own, so none is cancelled or left waiting behind another.
- **Not done here**: a workflow whose paths don't match doesn't run, which shows as no check rather than as a passing
  one; and Android's release and nightly don't wait for these workflows (they are separate; package 20 or a later
  change can gate them explicitly).
- **Checks**: actionlint 1.7.12 on both workflows (clean). CI on #241 (runs 38439422378 and 38439422290, at
  `f1d3915f4`): Allegrex 60/0, the system suite 497/0, the ares system 318/0, the runner's test passing, NGCD
  2835/0, each count in the run's summary. A run's summary steps failed at `a2483e36c` (the suites passed): an
  apostrophe inside `${HEAD_SHA:+...}` that bash took for a quote; fixed in `f1d3915f4`.
- **Negative control** (a disposable branch from this PR's first commit `1bf7afd26`, on master `9333a8d7a`, hence
  491 groups, with one failing check in each suite: run 38436478270, dispatched, and draft #242 for the new NGCD
  workflow, which a dispatch can't run before it's on master; #242 closed and the branch deleted after): every lane
  failed: Allegrex "60 groups, 1 failure", the ares system "319 checks, 1 failed", the system suite "491 groups, 1
  failure", the runner's test on its frame count, NGCD "2836 checks, 1 failures" (each through `tee`, the step
  failing; the test steps are the same in `f1d3915f4`).
- **Review**: an independent read-only review (it scanned every source the five builds compile against the paths)
  found sljit's headers missing from NGCD's paths and `desktop/Platform.hpp` from PSP's, and this entry calling the
  control's branch deleted when it wasn't yet; all fixed, with its suggestions taken (the Ubuntu pin, time limits,
  master's runs not cancelled, the empty-log case). A delta review found that a concurrency group holds one waiting
  run, so a third quick push to master still cancelled the waiting one: non-PR runs now get a group each (the run's
  ID).

## Docs: the plan's opening and evidence brought up to date; the 2026-10-13 test and performance plan indexed — 2026-10-13

Branch `cursor/test-plan-01-2b67` (PR #244), on master (`4788125f1`, #236), with #243, #230, #229 and #237 merged in
since (`3228b825d`). Package 01 of the 2026-10-13 test and performance plan, an outside audit of master `b3507c1db`
given to the owner as a plan bundle (not kept in the repository). Documentation only.
- **The stale summaries**: the implementation plan opened with "Last reconciled: 2026-09-25" and "zero fresh native
  performance runs", and this file's entry point with September's next action, though later sections record the RP6's
  N64 measurements (from 2026-09-25), the 2026-10-02 presentation comparison with Mupen64Plus-AE, the desktop builds
  and the PSP core's parts (76 of them on master then, up to part 87; 80 now, up to 88, with #243's part 88, #230's
  part 81, #229's part 82 and #237's part 86). Now the plan's opening and its evidence boundary sort what's known by
  kind: implemented on master (some of it CI only builds), checked on CI's hosts (exactly what CI runs, and what it
  doesn't yet), measured on the RP6 (dated, linked: the N64's, and the PSP's speed in its parts), waiting for the RP6,
  on branches, proposed. The current priority (the PSP's throughput, fast-forward and Vulkan scaling, accurate and fast
  apart) is named, the N64 regressions kept, and no comparison is presented as new. Nothing historical is removed.
- **The plan indexed**: the implementation plan's "Test and performance plan (2026-10-13)" lists the 25 packages
  with their dispositions (eight now, 08 pulled forward from the triage's later ones; 15 later, each waiting on a
  named PR or package except 18, whose only wait, #230, has merged since; two not now), what each waits on, and its PR.
- **Baseline at triage** (master `9333a8d7a`): the libadrenotools submodule at `8fae8ce25`; open PRs #223, #229,
  #230, #231, #232, #236 and #237, and two agents' branches without PRs (GE headroom and NpDrm modules, since #240
  and #243); CI on `0a9a7fa75` (v2.2.0's commit): PSP Core Tests 60/0 and 489/0, Desktop Build green, Android Build
  cancelled in its SDK cache's post step after 37 minutes (package 02; v2.2.0's own tag build, run 38432072867,
  passed, its release complete). The Mac: macOS 27.0.1, Apple clang 21.0.0, glslang 16.6.0, JDK 17.0.20.1. Since:
  #236 merged (`4788125f1`, StateVersion 27), all three workflows green on it (PSP 60/0 and 497/0), then #243
  (`49fec7215`, StateVersion 28), #230 (`542da5a28`, StateVersion still 28), #229 (`7511c64c3`, StateVersion 31) and
  #237 (`3228b825d`, StateVersion still 31); the open PRs' heads when this entry was first written: #223
  `f12663c2d`, #229 `a31b2b3ae`, #230 `059a1bbfe`, #231 `5e6d8a5b1`, #232 `b4dc27e4e`, #237 `46f62e6bf`, #240
  `7aa4b8054`, #243 `e84596e63`, and this plan's.
- **Checks**: `git diff --check`; relative links and anchors in both files, 0 bad (as on master).
- **Review**: an independent read-only review found the CI line claiming more than CI runs (the ares suite, the runner,
  NGCD, ARM64 and the JVM tests aren't run) and merging "implemented" with "CI-verified", the PSP's RP6 speed
  measurements left out, the baseline's PR list not the one at triage, CI cited from an older commit, #237 named among
  the state-layout PRs (it doesn't change states), and the order of the CI packages unexplained; all taken. Its doubt
  about "up to 59 minutes" is answered by the run cited (38286832457's post step: 58.7 minutes). A delta review found
  "only built" not limited to CI (release builds run on the RP6; the AppImage and Phobos.exe were run on 2026-10-02,
  Phobos.app on 2026-10-06), the RP6 speed parts listed incompletely (53 and 54) and "gathered" too widely, and row 08
  marked done while its PR is open; all taken, with master's #243 merged in (row 12). A second delta review found the
  software renderer's RP6 parts incomplete (67 and 68, and its columns beside Vulkan in 52 and 53) and row 12 missing
  #223 (whose own layout 28 isn't master's); both taken, with its suggestions (the speed tables' parts, the tag build's
  note, the desktop runs' dates, one list of what CI doesn't run, the part count), and master's #230 merged in. A third
  delta review found two facts left stale by that merge: the part count (#230 added part 81) and rows 15 and 18 still
  waiting on #230; both taken, with its suggestions (#230 in this entry's opening and its "Since", the plan's waits
  dated, part 41's speed figures no longer called a table). Row 18 now waits on nothing: no open PR changes
  `PhobosRunner.cpp` against master (GitHub's diff of #229, then open, showed #230's comment at the axis slots only
  because it still compared #229 with `49fec7215`, and #229 had #230's branch merged). A fourth delta review found this
  sentence had first called that comment #229's own edit; fixed, with its suggestions (#243 in the opening, row 18's
  adaptation, clearer wording). A review of master's #229 and #237 merged in found the plan and this entry stale in
  five places: the state layout (31 now) in row 12, row 14 still waiting on #237, the plan's waits dated
  `542da5a28`, the part count, and this entry's opening and "Since". All taken, with part 86 added to the parts
  waiting for the RP6 and this sentence about #229 put in the past. Its delta review found row 06 still giving
  #249's first reason for being red (four files now); taken, with part 86's date in the plan and the entry point's
  line over 120 columns broken before its link.

## CI: the PSP renderer's shaders compiled again by the glslang that made them, and validated — 2026-10-13

Branch `cursor/test-plan-07-2b67` (PR #239), on master (`4788125f1`, #236). Package 07 of the 2026-10-13 test and
performance plan (an outside audit of master `b3507c1db`, triaged against `9333a8d7a`; #244, package 01, indexes it
in the implementation plan). CI and a check script only: `compile.sh`, the GLSL and `shaders.hpp` are untouched (the
sources' hash covers `compile.sh`, so changing it would have meant regenerating `shaders.hpp`).
- **The gap**: `compile.sh --check` (run by `tests/psp/run-tests.sh`) always compares the sources' hash, but compiles
  again and compares `shaders.hpp` byte for byte only where a glslang happens to be installed, and CI installs none.
  So CI never compared the SPIR-V with what its GLSL compiles to, nor validated it (the GPU tests run it on lavapipe):
  a changed word under an unchanged hash that those tests don't notice passed.
- **The pin**: glslang 16.6.0 makes `shaders.hpp` exactly, both Homebrew's build and Khronos's own macOS release
  (`compile.sh --check` with each: exit 0). Khronos's Linux release of it is
  `glslang-16.6.0-linux-x86_64-release.tar.gz`, SHA-256 `2c34071f…91ce`.
- **The check** (`.github/scripts/check-psp-shaders.sh`, in a new workflow, PSP Shaders, on changes to the shaders,
  the script or the workflow): glslang must be found and be 16.6.0, then `compile.sh --check` (the hash, then every
  shader compiled again and compared), and each of the 12 modules in `shaders.hpp`, as its bytes, through `spirv-val
  --target-env vulkan1.0` (compile.sh's target); every array `shaders.hpp` declares must be one it reads, and every
  word a 32-bit hex word. Both parts run and report; either failing fails. A glslang upgrade is a toolchain change:
  `shaders.hpp` made with it, and the script's version, the workflow's version and SHA-256 and the doc's line moved
  together (the workflow doesn't override the script's, so CI fails if they differ).
- **Negative controls** (a disposable copy, the Mac, glslang 16.6.0 and SPIRV-Tools v2026.4): the copy as it is
  passes (12 valid); GLSL changed without regenerating: the hash fails; one word of `presentSPIRV` changed (still
  valid SPIR-V: spirv-val passes it) with the hash kept: the byte comparison fails; `presentSPIRV`'s id bound set to 1:
  the comparison and spirv-val both fail (11 valid); no glslang in PATH: fails; a glslang calling itself 16.5.0:
  fails.
- **Checks**: actionlint 1.7.12 with shellcheck 0.11.0 (clean); the script on master's tree (pass); CI on #239 (runs
  38435975771, 19 s, and 38437980381 on the head merged with master, 16 s; ubuntu-24.04): the image has no glslang of
  its own ("none"), the archive's SHA-256 matched, and Khronos's Linux glslang 16.6.0 compiled `shaders.hpp` byte for
  byte; Ubuntu's SPIRV-Tools v2025.1 passed all 12 modules.
- **Review**: an independent read-only review (PASS; it reproduced the pin, both CI runs and 14 negative controls)
  raised seven points: an array declared another way (`static constexpr`, made with a changed compile.sh) went
  unvalidated (11 of 12); the version was pinned in two places CI couldn't keep equal; a malformed word was dropped;
  `--version`'s failure was unexplained; the first run's time was misquoted; "CI never checked the SPIR-V" overstated
  the gap; actionlint had run without shellcheck. All taken: the declared arrays counted, every word checked, the
  workflow no longer overriding the script's version (two pins CI now keeps equal), the error said, the runs cited,
  the gap reworded, shellcheck run; and its suggestions (`ubuntu-24.04`, a 10-minute limit, curl's retries). A delta
  review (PASS) found an array written without `=` still unread: arrays are now found with or without it, and the
  SPIR-V magic numbers in the file must match the arrays read; CI on that head (run 38455036248 at `17c21468e`): the
  12 modules read, compiled byte for byte and valid. A second delta review passed it.

## CI: the Android unit tests in every build, the releases waiting for them; the SDK cache gone — 2026-10-13

Branch `cursor/test-plan-02-2b67` (PR #238), on master (`4788125f1`, #236). Package 02 of the 2026-10-13 test and
performance plan (an outside audit of master `b3507c1db`, triaged against `9333a8d7a`; #244, package 01, indexes it
in the implementation plan). CI only: no app, core or test code changes.
- **Unit tests in CI**: `.github/workflows/android.yml` gains a job beside the APK build, "Unit tests (legacy +
  modern)": `./gradlew :app:testLegacyDebugUnitTest :app:testModernDebugUnitTest --continue` (the app's 42 test files,
  299 `@Test`s, which no workflow ran before), their XML and HTML reports kept as the `unit-test-reports` artifact
  whether they pass or fail. The release and nightly jobs now need it as well as the build, so a failing test stops
  `android.yml` publishing either (on a version tag, `desktop.yml` still makes the release for its own packages after
  waiting 20 minutes for Android's: whether it should wait for the tests too is the owner's call). It builds no APK
  and uploads none; no device or instrumented test is implied (none exist). Both jobs now have a time limit (build 60
  minutes, tests 30), so a hung step ends instead of running on.
- **The SDK cache removed** (the Android job's long post step): `Cache Android SDK` packed the runner image's whole
  SDK folder, 4.6 GiB. After a miss its post step tars all of it: on master's six runs before `0a9a7fa75` that step
  took 2 to 59 minutes (13 or more in four), master's run 38423938456 was cancelled in it after 37 minutes with every
  real step green, and the run for `9333a8d7a` was cancelled in it too by the next push (neither commit got a
  nightly; v2.2.0's own tag run finished, its release complete). Without it, the components install in 19-67 s (the
  platform and build tools are on the image; the NDK and CMake download). A hit was no better: 2 to 5 minutes, then
  tar failing to set the times and modes of the image's root-owned folders ("Failed to restore", run 38429067532).
  Two of its entries (9.2 GiB together) also filled most of the repository's 10 GB of cache, which GitHub trims by
  evicting the least recently used (the Gradle and FFmpeg caches).
- **Checks**: actionlint 1.7.12 on the workflow (clean); the job graph read back (release and nightly need build and
  unit-tests). CI on #238 (run 38437970240): the unit-test job passed in 3 minutes, 299 tests in each flavor, none
  failed or skipped (its reports). Negative control (run 38435701552, a disposable branch with one test asserting
  1 == 2, dispatched): the unit-test job failed, both flavors ran (300 tests each, that one failing in each; Gradle ran
  them side by side, so this run doesn't show `--continue` at work, which matters when one flavor fails to compile),
  and the reports were uploaded; the branch was deleted after. Release and nightly don't run on a branch, so their
  gating is the job graph above. The first build without the cache (run 38437970240) passed in 17.5 minutes.
- **Review**: an independent read-only review found the workflow right and its CI claims checked (counts, artifacts,
  job graph, invariants), and asked for the docs to be exact: the tag case of `desktop.yml`, the cache's real save
  and restore times (and that the "never finishing" runs were cancelled by the next push), `--continue`'s wording,
  this entry's base, "every commit". All taken, with its suggestions of time limits and the failed restores; left:
  deleting the two old 4.6 GiB cache entries once this is merged (open PRs on the old workflow make them again).
- **Left**: the unit tests run on the debug variants (release differs in `BuildConfig.DEBUG` and signing); the
  dialogs', settings' and launch paths' UI tests are later packages (15-17).

## PSP: the CPU, round 2 — the VFPU's adder in NEON, delay slots compiled each way, the rare ways out of line, prefixes compiled, blocks linked — 2026-10-13

Branch `cursor/psp-cpu-round2-2b67`, on master (`b3507c1db`, #233 merged; merged in again at `0a9a7fa75`, #234, and at
`4788125f1`, #235 and #236). docs/psp-core.md, part 86, has the evidence. Clean room: PPSSPP not read (its CPU, JIT and
IR code not at all), JPCSP not read. Save states unchanged by this branch (master's layout, 27 since #236). Every change
is the host's alone: pictures, VRAM, RAM and the machine's state are master's.
- **Where it went** (Mac, Vulkan fast, xctrace sorted by compiled block): compiled code was 20 to 42% of the emulation
  thread in the CPU-heavy scenes and flat (the busiest block under 1%): block endings and branches a third of it, loads
  and stores a third; the VFPU's matrix products through the scalar adder 15% of Super Monkey Ball's thread (one loop
  of eight vtfm.q), 25% of Disaster Report 3's with its prefixed compares through the interpreter.
- **Changes** (each its own commit, with tests): the VFPU's adder (vfpuDot) in NEON integer instructions, four lanes at
  once and bit for bit, for compiled vmmul, vtfm, vhtfm, vdot and vhdp on ARM64 (operands in place; inf and NaN lanes
  and other hosts the scalar way); a branch's delay slot compiled along each way it goes, pc and pd stored only where
  they're read; the rare ways (an access off the fast path, an FPU mode or NaN, an overflow) as cold code after the
  block, the FPU's keeping the registers held, loads and stores in 7 to 8 host instructions; the lane-by-lane VFPU
  instructions with their prefixes worked out as the block is compiled (vfpuEach), the math functions and vf2i at rest;
  blocks linked straight to the blocks they go on to, unlinked when those blocks' code goes.
- **Numbers** (Mac, Vulkan fast, the emulation thread's host instructions and ms a frame, medians of three rounds
  taking turns; the load 30 to 300, so the instructions carry it): Super Monkey Ball's story 636 -> 520 M (-18%, 60 ->
  49 ms), Disaster Report 3 playing 52 -> 38 M (-26%, 4.4 -> 3.4 ms), the Need for Speeds, Twisted Metal's logo and the
  Armored Cores' intros 8 to 11% fewer, Colin McRae, the Armored Cores and Twisted Metal playing 8 to 19%, the GE-bound
  scenes 2 to 5%; the software renderer alike where the CPU counts.
- **Exact**: the 21 scenes' hashes identical to master's in the software renderer (100 frames, every frame's picture
  and VRAM, then RAM and the machine's state), Vulkan fast and Vulkan accurate (200 frames, the end's RAM and state);
  the NEON adder against vfpuDot on 72 million random results; 62 broken versions of the changes, 59 caught by
  tests/allegrex, the other 3 changing nothing observable. pending (the whole library, before merging)
- **Checks**: tests/allegrex 68/0 (sanitized; and 68/0 built for x86-64, run under Rosetta), tests/psp 478/0
  (sanitized), tests/psp/ares 318/0. Merged with #234 (part 83): the hashes identical again in all three renderers,
  tests/psp 489/0, tests/psp/ares 318/0; GitHub's Linux build (GCC) found a missing `<cstring>`, added, and its tests
  pass. Merged with #235 and #236 (parts 84 and 87): the hashes identical again in all three, tests/psp 497/0,
  tests/psp/ares 318/0, GitHub's tests and builds passing. An independent review found no change to emulated results;
  its points (test gaps: the NEON vmmul's in-place cases, links never checked, code memory starting afresh mid-run, more
  delay slot kinds; small fixes and comments) were all taken.
- **Left**: the block endings' run count kept in a register (each ending's load and store of it chains the blocks
  through memory), a cache of the last target for jumps through a register (Most Wanted's import stubs: 57% of its
  compiled code is block endings), registers held across blocks with stores only where read, the VFPU's remaining
  helpers (vf2h, vh2f, the integer packings, vdot/vhdp/vcmp with prefixes, vcrsp/vqmul/vdet), the polled kernel's
  per-call work (Most Wanted: 72% of the thread), and the GE's display lists (another task's).
- **On the RP6, to measure**: master against this branch, Vulkan fast at 1x, part 79's scenes: Super Monkey Ball and
  Disaster Report 3 most (a fifth to a quarter fewer instructions on the emulation thread), Colin McRae, the Armored
  Cores and the Need for Speeds 8 to 11% (Silent Line playing 19%), the GE-bound scenes 2 to 5%.
- Scratch: `~/phobos-work/scratch/cpu-round2`.

## PSP: sound — every ATRAC ID's context kept current (Angus Hates Aliens past NEW GAME), and sceVaudio — 2026-10-12

Branch `cursor/psp-audio-b00-2b67`, on master (`b7ea296f4`, then `1d9308714`, #228). docs/psp-core.md, part 82, has
the evidence. Items 2 and 5 of the deep-play batch 00 report. Clean room: pspautotests' audio/atrac (the states its
tests force, sas), audio/blocking/vaudio and audio/sceaudio/reserve recordings, pspsdk's pspatrac3.h and pspvaudio.h,
pspautotests' sascore.h, PPSSPP read only as a description of the PSP (sceVaudio on the SRC channel), the games' own
code traced and disassembled; JPCSP not read. Begun by one agent; an independent review's points taken by another.
**Save states go to StateVersion 31** (sceVaudio's hold; 15 to 28 still load, 27 and 28 being parts 87's and 88's
layouts). **29 and 30 are skipped**: other open branches' layouts, refused here (as 32 is). Whoever lands one of
those branches after this one must renumber its layout past 31 and update `tests/psp/ares/system.cpp`'s list of
refused versions (29, 30 and 32) and `tests/psp/states.cpp`'s older layouts (part 82 says more).
- **Angus Hates Aliens**: its music goes through sceSas's ATRAC3 voices with a wrapper (the one pspautotests'
  audio/atrac/sas copies) that sets its ID's context state to 16 and, to stop the music (NEW GAME), unsets the voice
  only while the context still says 16, asking again for good when refused. `_sceAtracGetContextAddress` rewrote the
  whole context at every call, so the 16 went back to 2: the black screen, the runner's time all in that call. Now
  every ID's context is in kernel memory (0x08030000), written at power on and whenever the library changes the ID,
  only read by `_sceAtracGetContextAddress`; a state a program sets (8, 16) refuses the seven calls recorded so with
  the recorded errors, the other calls answering as before; `__sceSasSetVoiceATRAC3` keeps the context in the voice's
  word in the SasCore `__sceSasInit` set up (0 again on unset and on init), where the game's streaming reads it (the 16
  loads through address 0 at boot). The game now loads and reaches its first level. Still silent: sceSas's ATRAC3
  voices don't decode (they didn't before).
- **sceVaudio**: reserve, release, blocking output, the effect and level control setters, on the SRC channel, as
  pspautotests' vaudio recorded (an output returns its count; a reservation refused while Output2 holds the channel
  still holds sceVaudio; the release waits for a buffer playing); buffers copied as pairs into two kernel buffers
  (0x08031000); an output's volume 0 to pspvaudio.h's 0x8000, more refused. Two choices disagree with pspautotests'
  audio/sceaudio/reserve (mono refused there; a release freeing the SRC channel Output2 held): left for the owner, part
  82 says how; Battle vs. Chess reserves stereo (format 2) and its outputs' volume, fading in from 0, peaks at 0x35C1,
  so neither touches it. Battle vs. Chess's music thread plays its MP3 music through it now. **Its moves still aren't
  carried out**: not sceVaudio. Traced: the confirm reaches the game's pad code; its chess thread runs its engine only
  when a request word is set, which nothing sets on the confirm; nothing else in its memory changes but the lit squares'
  colours, and no system call differs (no file, thread or missing function). The prototype's own move code wasn't
  followed to the end (part 82 has the leads).
- **Checks** on the final tree (with the review's points): `tests/psp/run-tests.sh` (sanitizers) 479 groups, 0
  failures; `tests/allegrex/run-tests.sh` 58 groups, 0 failures; `tests/psp/ares/run-tests.sh` 338 checks, 0 failed
  (before it, 473 groups). Games with the branch's runner: part 82's last paragraph.
- **Library**: not run by the agents (the first agent's run stopped with its session); the owner runs the 266-game
  comparison for this branch and #230 together.
- **Review**: an independent review found NEEDS CHANGES (two state refusals with no check behind them; sceVaudio acting
  on a channel another caller held after its refused reservation; and lows: a bad rate leaving it held, contexts written
  on refused calls, the MAX values, untested refusals, wording), all taken in `930c6e2db`; a delta review passed, its
  two points (the SRC family's release of sceVaudio's channel documented and tested, a test's leftover) taken in
  `97fac6653`. A later independent review of the merged branch (five Lows), taken by another agent after the first
  stopped: sceVaudio's volume to 0x8000; the SasCore's word only for the core set up, cleared on unset and init; the
  older state's test; the context's state looked at by the seven recorded calls alone (Corpse Party's sound byte for
  byte master's); 27 to 30 noted. Part 82's "The review's points" has each.
- **On the RP6, to check**: (1) Angus Hates Aliens: NEW GAME passes the loading screen into the first level (black
  before); its music is still silent. (2) Battle vs. Chess: menu and board music plays (silent before); a move is
  still refused. (3) Save and load a state in a game playing music (any ATRAC game) and in Battle vs. Chess: it plays
  on; a state made by this build is refused by builds before it (StateVersion 31).
- Scratch: `~/phobos-work/scratch/hle-b00`; `~/phobos-work/scratch/review-b00` (the review's points: the runners, the
  runs in `runs/`, the WAVs in `wav/`).

## PSP: the host's right stick for patched games, the keyboard in a field's own characters, 7th Dragon 2020-II past its certificate — 2026-10-12

Branch `cursor/psp-input-b00-2b67`, on master (`6ab2c5583`, merged with `b7ea296f4`, #227, then `1d9308714`, #228).
docs/psp-core.md, part 81, has the evidence. Items 3 and 4 of the deep-play batch 00 report
(`~/phobos-work/deepplay/batch-00-report.md`). Clean room: pspsdk's psputility_osk.h and pspctrl.h, pspautotests'
ctrl/sampling, the games' own code traced with a scratch runner, PPSSPP read only as a description of the PSP (its
right-stick convention, its note on a negative ATRAC size); JPCSP not read. Save states unchanged (StateVersion 26).
Begun by one agent, finished by another (the first stopped on finding the second in its worktrees); both agents'
commits are on the branch, and a third's with an independent review's points.
- **Right stick** (the owner's decision: pspsdk's Rx and Ry, as a PSP Go with a DualShock 3, a Vita and emulators
  fill them; the PSP itself has no second stick): every
  SceCtrlData sample carries the host's right stick in bytes 10 (X) and 11 (Y), 128 when centred, in every game and
  every sceCtrl read, whatever the PSP Right Stick setting (Off, the default, then does nothing else); centred in
  digital sampling as the left stick is; the last four bytes 0; not in states. The core takes it from "R-Stick X" and
  "R-Stick Y"; the app hands the right stick to it in every choice (with Analog stick it is the PSP's stick and the
  patched games' second one at once: the setting's description says Off suits those games). The runner takes
  `R-StickX:` and `R-StickY:` in `--press` and `--script` (README). The four Armored Core True Analogs images read it:
  Formula Front International's camera now rests level behind the AC after "GO!!!" and turns and pitches with the
  right stick (it pitched up at the sky and the AC turned left on its own).
- **Keyboard**: `Kernel::keyboardConform()` (utility.cpp) gives text as a field of PSP_UTILITY_OSK_INPUTTYPE_* types
  and lines takes it (the other width, case or kana where one fits, else left out; spaces full-width only where only
  full-width types; line breaks only in fields of several lines; Latin letters ASCII's and U+00C0-024F's, every printed
  character without a class a Japanese symbol). The automatic answer's nickname goes through it: both 7th Dragon
  games' name fields are full-width letters only (0x0c00), so "PSP" is now U+FF30 U+FF33 U+FF30 (2020's hero was
  nameless); a field taking none of it gets "ぴーえすぴー", "ﾋﾟｰｴｽﾋﾟｰ", "ПСП", "피에스피" or "1", the first it takes
  whole. The app's keyboard shows what's typed as Android's keyboard composes it and enters it converted (a line under
  the field shows the form entered where it differs); the core converts the app's answer again. The game's own text
  left as the field showed it is entered as the game gave it, by both (Ridge Racer 2's "XEVIOUS", in a field of small
  letters alone, stays so). The Kotlin and C++ conversions run one table of cases
  (`android/app/src/test/resources/psp-keyboard-cases.txt`).
- **7th Dragon 2020-II's certificate** (found by the first agent): the name wasn't what froze it; the game hands
  `sceAtracSetDataAndGetID` the size 0xffffffff for a sound read whole into its buffer, which was refused. A size with
  its top bit set now reaches to the end of the buffer's memory (none: refused as before), taken in `atracSet()` and
  `atracSetAndGetID()` so that the sound branch (#229) merges with this one without a conflict in `atrac.cpp`. The game
  goes on into its story.
- **An independent review of the merged branch** (one Medium, four Lows), all taken by a third agent after the second
  stopped: the game's own text unedited entered as it is and a wider classifier (the Medium: OK on a default had
  entered a changed or empty name); the fallback nicknames; pspsdk's Rx/Ry in the comments and docs; one table of
  cases for both conversions, no half of a pair kept or cut in the core; `atrac.cpp` merging cleanly with #229.
  Part 81's "The review's points" has each, and what wasn't done (an edited default is converted whole).
- **Checks** on the final tree (with the review's points): `tests/psp/run-tests.sh` (sanitizers) 478 groups, 0
  failures; `tests/allegrex/run-tests.sh` 58 groups, 0 failures; `tests/psp/ares/run-tests.sh` 328 checks, 0 failed;
  Android `:app:compileModernReleaseKotlin :app:testModernDebugUnitTest` 304 tests, 0 failures (before it, 472 groups
  on `04b274924`). Games with the branch's runner: part 81's last paragraph.
- **Library**: not run by the agents (the second agent's run stopped with its session); the owner runs the 266-game
  comparison for this branch and #229 together.
- **Review**: the first agent's independent review (`~/phobos-work/scratch/hle-b00/review-pr-a.md`: the app's keyboard
  converting keys under Android's composition, a state test, comments, line breaks, a negative size on no memory) had
  its points taken in `04b274924`; a second independent review of the branch (PASS, no High or Medium) had its Low and
  Nit points taken in `e743fbcff` (all four ATRAC setters given the whole file take a negative size; the app's field
  holding nothing past its limit, and saying so when nothing will be entered; no control characters in a field of any
  type; comments and docs).
- **On the RP6, to check**: (1) An Armored Core True Analogs image (Formula Front International, 3 Portable, Last
  Raven or Silent Line Portable) with the PSP Right Stick setting on Off and a controller whose right stick is bound
  (R-Up/Down/Left/Right, as by default): in a battle, after "GO!!!" and with the right stick untouched, the camera stays
  level behind the AC and the radar's heading holds; right stick right and left turns the AC, up and down tilts the
  view (it stays where it's left, as the mod has it). With Analog stick chosen, the right stick moves the AC and the
  camera at once (expected). (2) 7th Dragon 2020 and 2020-II, a new game: on the name screen, NAME opens the keyboard;
  typing "Bob" shows "Bob" with "Ｂｏｂ" under it, OK enters "Ｂｏｂ", the game shows the name; 2020-II's certificate
  then passes into the story. (3) Japanese typed by romaji in a kana field composes normally. (4) Ridge Racer 2's
  driver name: OK on its default ("XEVIOUS" or another Namco title, in capitals) keeps it in capitals, with no line
  under the field; typing over it shows the small letters it will be entered in.
- Scratch: `~/phobos-work/scratch/hle-b00` (runners in `bin/`, the runs in `runs/`, the library runs `lib-*`);
  `~/phobos-work/scratch/review-b00` (the review's points: the runners, the runs in `runs/`, the scratch front end in
  `front/`).

## PSP: modules from downloads — sceKernelLoadModuleNpDrm, packed relocations, imported variables; God Eater 2 in English — 2026-10-13

Branch `cursor/psp-npdrm-modules-2b67`, on master (`0a9a7fa75`, #234), merged with master since (#235, #236).
docs/psp-core.md, part 88, has the evidence. Clean room: uOFW's module manager and loadcore, pspsdk's headers,
pspautotests' recordings, RFC 1321, PPSSPP's notes read only as a description of the PSP; JPCSP not read. StateVersion
28 (each module keeps its imported variables; 27 is part 87's). The PGD/NPDRM code (pgd.cpp, pgd.hpp, io.cpp's
sceNpDrm functions), the owner's then (open to agents since 15 Oct), is unchanged and wasn't needed.
- **Before**: God Eater 2 (NPJH50832) with RedArtz's English patch v2.0 and its folder `PSP/GAME/NPJH50832` on the stick
  stopped at the patch's "your DLC file is corrupted or missing": ModuleMgrForUser 0xf2d8d1b4 missing.
- **The folder**: its 45 `.sprx` modules are plain ELFs, its EDATs plain too: nothing needs NPDRM decryption (the
  owner's sceNpDrmEdataSetupKey leaves the plain PATCH.EDAT as it is, as before).
- **sceKernelLoadModuleNpDrm**: uOFW's checks and errors (ILLEGAL_CONTEXT from an interrupt, ILLEGAL_ADDR, a '%' is
  UNKNOWN_MODULE_FILE, options ILLEGAL_ADDR or ILLEGAL_SIZE, then sceIoOpen's own error); an NPDRM module as sold
  (EDATA) refused, UNSUPPORTED_PRX_TYPE (chosen), its key (npdrm's module key, derived from the EDATA and the licensee
  key, and the `~PSP` decryption with it) not derived: that's what's missing for store-bought download modules; any
  other file loads as sceKernelLoadModule's do (a plain one, as custom firmware and emulators load it: chosen).
- **The loader**: packed relocations (PT_PSP_REL2), read as uOFW's loadcore does, the steps its decompilation loses
  settled by the modules (all 459,660 relocations of the 45 land in their segments, every jump in its code); imported
  variables linked as modules load, unload and move (`linkImports()`, `relinkVariable()`), the program's included.
- **What the game asked for next**: sceMpegRingbufferQueryPackNum (packnum recorded), MD5 (sceMd5 and UtilsForUser's,
  the context as hash/md5ctx recorded), sceRtcConvertUtcToLocalTime and back.
- **PBOOT.PBP**: not booted by the core (it boots the disc's EBOOT.BIN); not needed here: the disc image already is the
  1.30 update, its EBOOT.BIN the patched one; the folder's PBOOT.PBP is Sony's encrypted program (tag 0x2e5e10f0, a key
  the core hasn't). Not done.
- **The game now** (fresh stick with the folder): title, the game's install notice in English, the Main Menu, Character
  Entry, the appearance screen, the first save, the opening with English subtitles, the update's English hints, and the
  base, where the player walks and Fran talks in English. A mission not reached (the story first sends the player
  about).
- **Checks** (`5a7c2452b`, merged with master): tests/psp 502/0 (five new groups; of two local runs one had the
  timing-dependent group "ge loads through the depth buffer's views wait for the threads" fail on the loaded Mac, as
  before; CI's passed), tests/allegrex 60/0, tests/psp/ares 318/0. God Eater 2 without the folder as before (the same
  pictures, its log but the loader's old note); twelve other games (nine PSN releases, three that load modules) with the
  base's runner and this one: the same pictures but animation steps and Kenka Bancho's movie timing, which two base runs
  show too; their logs the same. The game's chain again on the merged code: 108 of 111 captures as before, the rest a
  moment apart.
- **Review**: an independent read-only review found two blockers, both fixed (`1998c3014`, `5a7c2452b`): MD5 copying
  from an empty message's null pointer (GCC's UBSan stopped CI's first run), and master's font states test reading
  layout 28 as 26 with the variables count in; and minor points, all taken (the clock zones by the settings' minutes for
  every tick, as uOFW's rtc, tested in fixed zones; state fields; the kernel's own libraries' variables noted; counts
  and uOFW's slips in part 88; loadcore's segment run; unchanged words not rewritten; broken tables' reasons; relinked
  code run again). Its remark on the owner's EDATA path (io.cpp's `pgdDescriptor()` reads the PGD header at offset 0,
  sceNpDrmEdataSetupKey puts the data at 0x90) went to the owner (part 97 took it up).

## PSP: sceFont — Jeanne d'Arc's handle closed twice, Final Fantasy III's fonts in VRAM, Hammerin' Hero's spaces — 2026-10-13

Branch `cursor/psp-font-b03-2b67`, on master (`b3507c1db`), PR #236. docs/psp-core.md, part 87, has the evidence. Clean
room: the games' own code and traces, pspautotests' recordings; PPSSPP read only as a description of the PSP (its issue
5954, its sceFont's model of closing and of a character's memory), nothing copied; JPCSP not read. Save states: layout
27 (26 and older load, their fonts holding no glyph).
- **Jeanne d'Arc** (deep play batch 03, bug 1: "Loading..." for good at its first battle): two of its text objects close
  one font handle twice; the HLE refused the second close, and the game, which takes a lock of its own around each call
  into the library and gives it back only for a 0, then waited for that lock forever. A PSP answers 0 there (the game
  loads its battles on one), so sceFontClose of a library's handle that's closed already now does nothing and returns 0
  (a handle no library has is still refused). From its briefing state the first battle loads: its battle map, then the
  Combat tutorial.
- **Final Fantasy III** (batch 02, bug 2: none of its own text): its three fonts are in VRAM, which
  sceFontOpenUserMemory didn't read (RAM only), so every open failed and the game drew with handle 0. A font in memory
  is read from RAM, VRAM or the scratchpad now: from boot its name screen, battle windows and dialogue have their text
  (its play-tester's state keeps the failed opens it was saved with).
- **Hammerin' Hero** (batch 03, bug 3: words run together): the game spaces a line by its own rule, a space a quarter of
  the space's advance (1.7 pixels) or, when the space's details are refused, a quarter of 18 (4.5). On a PSP its words
  are spaced, so the details are refused there: a PSP's libfont reads each glyph into memory of the program's
  (fonttest: the record and the picture, 71 and 210 bytes for jpn0's 'H'), a space's picture is 0 bytes, and this
  game's alloc (a VPL) refuses 0 bytes. The HLE does the same now: a character's details and pictures read its glyph
  into two blocks of the program's, the font holding the last one read and giving it back when another is read, on
  closing, on sceFontFlush and with the library; a block not given fails the call (out of memory). Its text is spaced
  as on a PSP. Choices where nothing shows the PSP (the order of giving back, the flush, shadows and picture sizes
  reading nothing, opens keeping part 23's rule for a 0-byte block) are marked in the code and in part 87.
- **Regression check** (master's runner against the branch's, PNGs compared): Growlanser IV from its state in play and
  in its menu, and 21 more of the play-testers' sceFont games from boot (Gran Turismo's 5,508 glyphs read among them):
  every frame the same pixel for pixel but two, Crush's title background a step apart (the time its 811 reads take
  now) and Guilty Gear XX's intro a frame behind in one capture (the GE's threads: master's own picture of frames 598
  and 599; the same with no GE threads). Traced, no game but Hammerin' Hero has a glyph read refused, and none gives its
  library a cache.
- **Review**: an independent read-only reviewer found no high-severity bug; its points (the count above, a test of
  reads with another thread closing the font or the library meanwhile, three damaged-state checks, the choices marked as
  choices, the never-opened handle, small cleanups) are all taken.
- **Checks**: tests/psp 484/0 (six new font groups; the state fields' new ones; 495/0 merged with #234),
  tests/allegrex 60/0, tests/psp/ares 318/0; CI green on the merge (1d22b81c2).
- **Scratch**: `~/phobos-work/scratch/font-b03/` on the Mac it was made on (runs, traces, comparison sheets, the scratch
  tracer's patch), not in the repository.

## PSP: savedata's patterns — Dissidia's Load Game, Final Fantasy Type-0's list; Disgaea Infinite's New was Continue — 2026-10-13

Branch `cursor/psp-savedata3-2b67`, on master (1d9308714, #228). docs/psp-core.md, part 84, has the evidence. Clean
room: the games' own requests (traced), Disgaea Infinite's own code (a RAM dump read with LLVM's MIPS disassembler, a
scratch runner following its variables), pspsdk's psputility_savedata.h, pspautotests' utility/savedata/idlist,
PPSSPP's notes only as a description of the PSP; JPCSP not read. Save states are unchanged (StateVersion 26).
- **Dissidia Final Fantasy and Dissidia 012** (Load Game back to the title): their list load keeps "GameData*", the
  list mode's pattern, as its save name, and the kernel took a list only for a request naming no save, so it looked
  for a save named "GameData*" and said NO_DATA. Fix (`utility.cpp`, `savePattern()`; `dialogs.cpp`): a save's name
  with '*' or '?' is a pattern, naming no save (FAT holds neither character); in a list's load or save from a list it
  names none, as "<>" doesn't. Now: GameData00 loads and both games go on where the save was made.
- **The same mistake elsewhere**: every list load answered with no front end now takes what its dialog focuses, the
  request's own save name only naming focus 0's pick (Disgaea's Continue names DIP000 beside 32 slots: with its save
  moved to DIP003 it now loads it, where master said no data); a list save naming a pattern takes the list's first
  name; a pattern in a list is no save; with no list a pattern is a name no save has (a load finds none, a save is
  refused); no save is made under a pattern, and the app's dialog asks for none.
- **Final Fantasy Type-0** (its "GAMEDATA*" list answered with SYSTEMDATA): the list mode listed every folder of the
  game's. Fix (`savedataList()`, `patternTakes()`): the saves the pattern takes ('*' any run, '?' any one; idlist's
  "A?C" listed its one save, ABC; leaving out the rest is chosen); "" and "<>" still list every save. Now: listed 0,
  the game as before.
- **Disgaea Infinite** (New back to the title): not an emulator bug. Its title's cursor starts on Continue (index 1
  in the game's data; only Up and Down change it), so the play-tester's Cross was Continue: a list load of its slots
  on an empty stick, NO_DATA, and the game's code goes back to the title for any answer but 0, a cancel included. New
  (Up once) starts the story with master's runner as with this one. Nothing at boot writes its system data: its only
  saving requests are its save's (Start in the story: a list save to DIP000, then DIS000 autosaved), and Continue
  loads that save.
- **The #228 games** (After Burner, Brandish, 7th Dragon 2020, Colin McRae Rally 2005 Plus) save and load as before:
  every trace, file and capture the same with master's runner and this branch's.
- **Review**: an independent read-only review's points are all taken (part 84 lists them); left as before: a list
  delete answered with no front end and naming no save deletes the game's own folder.
- **Checks**: see part 84.

## PSP Vulkan: its waits for its own pixels gone — moves across row widths, palettes on the GPU, copies kept — 2026-10-13

Branch `cursor/psp-vulkan-speed-2b67`, on master (`1d9308714`, #228). docs/psp-core.md, part 83, has the evidence;
docs/psp-gpu-renderers.md the design. Clean room: no PPSSPP code read (its GPU backend not opened), JPCSP not read.
Save states unchanged (StateVersion 26). The software renderer untouched.
- **Measured first** (the M1, MoltenVK, a scratch build counting a frame's draws, passes, binds, barriers, uploads,
  copies, moves, read backs and each finish's stack, and the emulation thread's time in the GE, recording, submitting
  and waiting): the slow scenes waited for the GPU's own pixels. Frame buffers drawn at two row widths over one memory
  (Colin McRae, Dirt 2, Brave Story), palette indices over a frame buffer (Death Jr. II, Sonic Rivals, Activision Hits
  Remixed: 31 to 61 finishes a frame), 3D textures running past the picture into other frame buffers or past VRAM's end
  (Death Jr. II, Castlevania, whose blur didn't show), depth fills (Darkstalkers), a copy and a render pass for each of
  Killzone's 800 blur quads. The Armored Cores' intros are bound by the movie's polling in both renderers on the M1;
  Army of Two and Dirt 2 by the GE's front end.
- **Changes** (seven code commits, the last the review's points, with tests that fail without them): the GE's dither
  in the GPU's blending and depth as the GE's whole numbers (Death Jr. II's glow, Daxter's shadow); moves between
  frame buffers of other row widths; a texture past VRAM's end taken, depth changes kept for PRIMs that use depth,
  beside ranges past 16 kept and filled at the target's next use; palette indices over a frame buffer looked up on the
  GPU, rows not known cut at the next frame buffer, memory's rows decoded alone; a copy taken again only where a
  PRIM's texels meet the changes (the GE gives `holds()` the texels a PRIM may take, 3D triangles too), barriers only
  before overlapping reads; textures kept for the next of their size; the depth write its own shader build. All in
  both modes, but that accurate mode holds rows past a texture's frame buffer only where the PRIM takes none of them.
- **Numbers** (the M1, fast mode, base -> branch, at a load near 20): Death Jr. II's woods 9.7 -> 67.8 frames a second
  and its character select 21 -> 170, Activision Hits Remixed's room 10 -> 84, Sonic Rivals 14 -> 113, Castlevania
  103 -> 354, Darkstalkers 49 -> 122, Brothers in Arms 23 -> 39, Killzone 11 -> 20, Colin McRae 43 -> 57, Brave Story
  156 -> 216; the Armored Cores, Army of Two, Daxter as they were; the final code's round (load 30-130) in part 83, the
  same ratios. Accurate: Castlevania 58 -> 144, Killzone 11.7 -> 19.3, Death Jr. II's woods 4.0 -> 10.5 and Sonic
  Rivals 5.8 -> 11.5 (their palette passes held only where exact: 8 finishes a frame, from 31).
- **Checks** (the final code, 4b2665616): tests/psp 486/0 (sanitized; 11 new GPU groups), tests/allegrex 58/0,
  tests/psp/ares 318/0; CI on #234 green (its first run failed a dithered-blending threshold lavapipe misses: 75% now).
- **Review**: an independent read-only review (no blocker, three major points: accurate mode's held rows, texels a
  PRIM takes for sprites, beside fills inside memory's callbacks) and a delta review (its tests in 3D so they reach the
  held-row paths); all points taken (04e5d1c8a, 7099e35e3).
- **Library** (accurate Vulkan, 3600 frames, master against the final code): all 266 finish in both, 214 the same
  at all four captures, 32 within a level; of the 20 others, those nearer Software are the dither and depth fixes,
  those nearer master the same movie or demo a few frames apart (the clock follows the host's; part 83 lists them).
- **With #233** (part 79, merged in after it reached master; part 83's "With part 79"): the palettes on the GPU take
  Backbone's glow first; a texture the GPU doesn't hold is decoded as far as the frame buffer it's drawing, without
  waiting, and cached for the next tile through the same palette; part 79's box only where those rows would still
  wait, and its copies checked in the box alone (accurate mode: 27 to 56% fewer textures decoded than this branch).
  The merge's code as it was, part 79's two glow tests rewritten for it: tests/psp 489/0, tests/allegrex 60/0,
  tests/psp/ares 318/0; end states the same as this branch alone's in Software (every frame), accurate and fast.
- **On the RP6, to measure**: Software against Vulkan (fast) in the Armored Cores' intros and first menus (the M1
  can't say why the RP6 was slower there: the GPU's per-frame work is half a texture upload, a present and a shot),
  Colin McRae's race, Death Jr. II, Sonic Rivals, Activision Hits Remixed, Brothers in Arms, Killzone's menus,
  Castlevania, Brave Story and Darkstalkers.
- **Left**: Colin McRae's terrain shadows (depth from frame buffers of another row width); Killzone's blur still takes a
  copy at each turn from one half of its strip to the other (456 a frame); accurate mode without rasterization order
  splits overlapping blended primitives (MoltenVK: Colin McRae's race 0.7 frames a second); the GE's front end in the
  3D games (Army of Two, Dirt 2).
- Scratch: `~/phobos-work/scratch/vulkan-speed`.

## PSP: where the slow games' emulation thread goes — the GPU's palette pictures decoded where taken, code forgotten only where there is some, calm system calls, more of the CPU compiled — 2026-10-12

Branch `cursor/psp-cpu-speed5-2b67`, on master (`6ab2c5583`, #224 merged in). docs/psp-core.md, part 79, has the
evidence. Clean room: PPSSPP not read (its CPU, JIT and IR code not at all), JPCSP not read. Save states unchanged
(layout 26). Every change is the host's alone: pictures, VRAM, RAM and the machine's state are master's.
- **Where it goes** (Mac, Vulkan-fast, from save states; master with #224): in most of the owner's slow games the
  CPU's emulation isn't the bound. Backbone's games (Death Jr. II, Sonic Rivals, Activision Hits Remixed) finish the
  GPU and read its pictures back 31 to 61 times a frame, for a palette texture (the frame the GPU drew, as CLUT32
  indices) decoded whole for each glow tile; Super Monkey Ball, Army of Two and Ghost of Sparta spend 60 to 85% of
  the thread in the GE's display lists on the CPU (Ghost of Sparta 17% more forgetting compiled code that wasn't
  there); both Need for Speeds and the intro movies poll system functions 130,000 to 530,000 times a frame; compiled
  code is a fifth to a third where the guest runs millions of instructions a frame.
- **Changes** (each its own commit, with tests): a texture the GPU drew decoded only where each 3D triangle takes it
  (a box from the corners' texture coordinates, conservative for what the GPU samples), waiting only for the GPU's
  drawing in those rows, a cached copy drawn beside taken where its box's bytes are unchanged; memory's changes look
  only at sections with compiled code (a bitmap; a page's store entry restored where its watch ends); a thread-queue
  rotation or a semaphore signal that changed nothing to come leaves the CPU's go as it is (as #224's quiet
  functions); and the first session's: code forgotten only where there could be some, a taken FPU/VFPU branch chained
  to its target, more of the VFPU compiled with the prefixes followed through a block.
- **Numbers** (Mac, Vulkan-fast, the emulation thread's ms a frame and host instructions, medians of three rounds
  taking turns; the Mac's load 20 to 190): Death Jr. II playing 70 -> 33 ms (983 -> 388 M), Sonic Rivals 42 -> 19
  (641 -> 275 M), Hits Remixed 57 -> 26 (711 -> 268 M), Ghost of Sparta's menu 41 -> 30 (648 -> 429 M), Most Wanted
  racing 23 -> 18 (369 -> 319 M), Underground Rivals 23 -> 20, Twisted Metal's intro 21 -> 18, the Armored Cores'
  intros 6 to 7% fewer instructions, Disaster Report 3 playing 16% fewer; Monkey Ball, Army of Two, Colin McRae and
  the other play scenes within the Mac's noise (instructions 1 to 4% fewer). In the software renderer the polled
  calls show most (the Need for Speeds 17 to 23% less time, Twisted Metal's intro 16%).
- **Exact**: per-frame picture and VRAM hashes, then RAM and the machine, identical to master's in 21 scenes
  (software renderer, 100 frames); end states identical in the same 21 (Vulkan-fast, 200 frames). pending (the whole library, before merging)
- **Checks**: tests/psp 472/0 (sanitized; three new groups), tests/allegrex 60/0, tests/psp/ares 318/0. An
  independent review of all eight commits found no change to emulated results; its points were fixed (writable()
  keeps a page holding code out of the store table, which vramGuard could list again; the box's margin grows with its
  length; makeTexturePart refuses a part outside the texture; test gaps, one of which showed the calm rule wrong: a
  rotation handing the CPU to a thread woken for callbacks, which goes straight back to its delay, had counted as
  calm; now calm means no switch at all). A delta review of the fixes found no bugs; its test gap (vramGuard and
  code) filled. The box's margin rests on the rasterizer working a pixel's weights out from the corners it finds it
  inside by; a shader clamp to the box would make that not matter (left, below).
- **Left**: the GE's display lists on the CPU (Monkey Ball, Army of Two, Ghost of Sparta: reading vertices through one
  pointer, each corner placed once a PRIM, small PRIMs handed over in runs); the polled calls' remaining compiled code,
  dispatch and status reports; Death Jr. II's last 10 finishes a frame (its bottom tiles' box reaches the frame
  buffer's first row); the CPU's compiled code (results kept in host registers across a block, blocks linked
  directly, the VFPU in native code); a clamp of a part's fetches to its box in draw.frag, as a guard.
- **On the RP6, to measure**: master against this branch, Vulkan fast at 1x, the scenes in
  `~/phobos-work/scratch/cpu-speed2/scenes` (part 79's table): Backbone's games much faster, Ghost of Sparta's menu
  about a third, the Need for Speeds and the intros 10 to 20%, Monkey Ball and Army of Two about as they were.
- Scratch: `~/phobos-work/scratch/cpu-speed2`.

## PSP: the savedata trace; After Burner's save dates, the list modes' automatic answer — 2026-10-12

Branch `cursor/psp-savedata2-2b67`, begun on `cursor/psp-colinsave-2b67` (Colin McRae's memstick:, part 77) after its
merge of master, and merged with master again since (#227, then #226). docs/psp-core.md, part 80, has the evidence.
Clean room: the games' own requests (traced), pspsdk's psputility_savedata.h and psputility_sysparam.h, pspautotests'
savedata tests, FAT's format, PPSSPP's notes only as a description of the PSP; JPCSP not read. Save states are
unchanged (StateVersion 26).
- **The trace**: `tools/psp-runner --trace-savedata` prints each savedata request as the game made it (a list's
  focus and the save chosen from it beside it) and its answer, a line with its frame (`Kernel::savedataTrace`, empty
  unless a host sets it; the runner's README has the format).
- **After Burner: Black Falcon** (autoload "No game data was found.", and the loop back to pilot selection after its
  story): the kernel dated the stick's files in UTC against a clock in local time, so on a host west of Greenwich a
  save made in the last hours was dated in the future and the game took it for none. Fix (`io.cpp`, `writeTimes()`):
  local time, as a FAT memory stick keeps, and no date later than the PSP's own clock in local time (which falls
  behind the host's when paused, slow, or after a state loaded), nor before 1980. The system's time zone and daylight
  saving time settings follow the host's. Now: the story ends in the MAIN MENU, and the next boot autoloads.
- **Brandish: The Dark Revenant** (loads did nothing) and **7th Dragon 2020** (LOAD GAME): with no front end, a list
  load or save naming no save took the list's first name, there or not, and didn't write it back into the request.
  Fix (`dialogs.cpp`, `savedataUnshown()`): a list load takes the save the dialog would focus among those there (if
  that one is broken, the one the same focus takes among those that aren't), a list save the list's first name as
  before; the name written back either way, as a choice is. Now: Brandish loads from the title and in play; 7th
  Dragon loads a save that isn't its list's first.
- **CI and the reviews**: the first commit (926cf1214) failed CI's GCC build (a -Wmaybe-uninitialized false positive
  on the nall::string given to nall::inode::timestamp(); the times are now read with stat(), checked with GCC 13.3
  and CI's flags). An independent review's points are taken but one (a list save to the new slot its dialog focuses,
  left out on purpose), and a second review's of the follow-up; part 80 lists them. 7c44b0184's message describes only
  an earlier tweak, its review line another diff: its content is this follow-up's work in progress, committed by
  another process, finished and reviewed in the commits after it.
- **Not fixed**: 7th Dragon's progress save. The game never asks for one where the play-tester tried (its Option
  screen's Start saves the 26-byte system data, as asked); a save point further in wasn't reached.
- **Checks**: see part 80.

## PSP: anti-aliased lines — Dead or Alive Paradise's wireframe gone — 2026-10-12

Branch `cursor/psp-aalines-2b67`, on master (`529fbf403`, after #221, #222 and #225). The play-test's batch 1, item 1: every girl's model in Dead
or Alive Paradise covered in a coloured wireframe, both renderers. docs/psp-core.md, part 78, has the evidence. Clean
room: the owner's PSP's round 4 pictures, a trace of the game's lists, PPSSPP's rasterizer read for its description
of the PSP only; JPCSP not read. Save states unchanged.
- **Cause**: the game draws its models' edges again as anti-aliased 3D line strips, textured with the frame buffer
  they draw into, blended by alpha, alpha test above 0. The core drew anti-aliased lines aliased and opaque, each pixel
  at the model's own texture coordinates, so other parts of the screen showed along every edge.
- **Fix** (`draw.cpp`, `raster.cpp`): an anti-aliased line's pixels are the aliased line's, each with its coverage
  for its alpha (0 to 128, measured: round 4's `lines-smooth` matches word for word) and, textured, the texel at its
  own middle (unmeasured, PPSSPP's description); the GPU draws them from `linePixels()` alike. The edge pass now blends
  each pixel with itself and doesn't show. Chosen where unmeasured: the alpha replaced, not scaled; steep lines as
  shallow ones turned; clear mode as before.
- **Round 7** (`tools/psp-measure`, new first menu line, `pspmeasure.elf` rebuilt): `aa-slopes`, `aa-axes`,
  `aa-alpha`, `aa-blend`, `aa-textured`, `aa-3d`, for what's chosen. Not run on a PSP yet: the next thing to ask the
  owner for.
- **Checks**: tests/psp 468/0 (sanitized; two new groups, round 7 driven), tests/allegrex 58/0, tests/psp/ares 318/0.
  The beach, pool and volleyball scenes are clean in both renderers, software and Vulkan a mean of 0.1-0.2 apart. The
  whole library beside the final run: 265 finish (Killzone stopped at 900 s on a busy Mac); of the three games that
  draw anti-aliased lines, MotorStorm: Arctic Edge's menu panels' edges are now graded, the others unchanged.
  Again after the merge, one game at a time: all 266 finish, 28 differ from the final run, none from the lines but
  MotorStorm's frame 1200.

## PSP: memstick: is the memory stick — Colin McRae Rally 2005 Plus's save — 2026-10-12

Branch `cursor/psp-colinsave-2b67`, begun on `cursor/psp-dialogs-2b67` (#221) and merged with master (#221, #222, #224
and #225). docs/psp-core.md, part 77, has the evidence. Clean room: the game's own calls (traced with a scratch runner),
pspsdk's headers, pspautotests' savedata tests, uOFW's iofilemgr.c, PPSSPP's notes read only as a description of the
PSP; JPCSP not read. Save states are unchanged (StateVersion 26).
- **Before**: answering Yes to Colin McRae Rally 2005 Plus's "Do you wish to create an Auto-Save file?" gave "Save
  failed!", the save's folder holding an empty OPTIONS.DAT, and the next boot asked again. The game assigns memstick:
  to the stick's file system at boot (sceIoAssign) and, after the save utility's autosave (which gives no data: the
  folder, PARAM.SFO and icons), opens its own TIMES.DAT and CMR5TS.DAT through it; the kernel knew no memstick:, so
  every such open failed as no device (0x80010013), and the boot's look for CMR5TS.DAT never found a save.
- **The fix** (`ares/psp/kernel/io.cpp`, `deviceName()`): memstick: is ms0:, whatever its case, assigned or not (no
  assignments kept: they would be state). `tests/psp/utility.cpp` has the game's calls as traced.
- **The game now**: after the autosave it writes TIMES.DAT (3.9 MB) and CMR5TS.DAT, autosaves again with OPTIONS.DAT's
  228,760 bytes and goes on; the second boot says "AUTO-LOAD.", loads, and reaches the main menu. A stick holding the
  old broken save needs no clearing: the game asks again and Yes writes the whole save. Twelve early-saving games
  booted twice on one stick (five of which save in that time) leave the same files with the base's runner and this one.
- **Checks** on the branch merged with master: `tests/psp/run-tests.sh` 467 groups, 0 failures;
  `tests/allegrex/run-tests.sh` 58 groups, 0 failures; `tests/psp/ares/run-tests.sh` 318 checks, 0 failed; the game's
  two boots as before. (Before the merge, on the fix's first commit d63474454, 462 groups; of two runs one had the
  timing-dependent group "ge loads through the depth buffer's views wait for the threads" fail on the loaded Mac.)
- **Review**: an independent read-only review of the fix (d63474454) and the docs' draft found no blocker or major
  point; its minor points were taken in 7d16e1301 (the test opening files as the game does, a garbled comment) and
  69b0bfe45 (the docs' quote, base and numbering). The merged branch was reviewed again (minor: these docs, and the
  test's nits), its points taken in the commit that records it.
- **Not done**: no APK built, nothing run on the RP6 (the Mac's runner only). On the RP6, to check: Colin McRae Rally
  2005 Plus, Yes to the Auto-Save question, then a second boot shows AUTO-LOAD and reaches the main menu.

## PSP: the slowest games faster — render to texture decoded where it's taken, Killzone's blur from a copy, polled system functions cheaper — 2026-10-12

Branch `cursor/psp-speed9-2b67`, on `cursor/psp-merge-212-219-2b67` (`5b2a564fa`, master and #212 to #219, since merged
as #220). docs/psp-core.md, part 74, has the evidence (part 73 is the dialogs branch's). Clean room: no PPSSPP code
read, JPCSP not read. Save states unchanged by it (StateVersion 26, #221's). Every change is the host's alone: the
software renderer's every picture, VRAM, RAM and the machine's end state are the base's.
- **Where the time went** (24 scenes reached from boot and kept as states; the GE's work and every system call
  counted, macOS's sample): render to texture decoded whole for each small primitive taking a little of it (Death Jr.
  II's character select, Sonic Rivals' stages and Activision Hits Remixed's room, Backbone's engine drawing the screen
  in 32x34 tiles each through its own CLUT32 palette: 24 to 47 million texels a frame; MX vs. ATV Untamed's 2D strips,
  4.1 million); Killzone's menu blur, 800 3D quads a frame over their own texture, drawn at once a pixel at a time from
  memory on the emulation thread; and system functions polled 120,000 to 500,000 times a frame (the two Need for
  Speeds' idle thread, the movie intros' ring). Both God of Wars, Army of Two, Super Monkey Ball Adventures (its
  setup too), Hot Wheels and Fat Princess are bound by the GE's work (11 to 28 million pixels of boxes a frame), not
  touched here.
- **Changes** (each its own commit, with tests): a deferred texture decoded only where its jobs take texels (3D
  triangles with one q by their stepped planes' bounds, 2D sprites by spriteReach(), round the edges as texelAxis()
  repeats them); 3D triangles over their own texture decoded and batched where no byte they may write is one they may
  read; deferred copies decoded into the room of earlier ones; and the host's cost of a system call (compiled syscalls
  straight to SYSCALL(), status structures on the stack, no look through every thread and timer after twelve quiet
  functions that change nothing whose time comes).
- **Numbers** (the Mac, base -> branch, medians of three rounds taking turns; frames a second, and the process's CPU
  ms a frame, which the Mac's shifting load doesn't move): Killzone's menu 1.9 -> 8.9 (318 -> 180 ms), Activision Hits
  Remixed's room 11.4 -> 49.6 (143 -> 40), Sonic Rivals' stage 22.4 -> 79.4 (61 -> 21), Death Jr. II's character
  select 28.4 -> 188 (57 -> 10), MX vs. ATV Untamed's ride 28.3 -> 43.1 (43 -> 30), Need for Speed Most Wanted 24.1 ->
  42.0 (42 -> 29) and Underground Rivals 22.5 -> 47.2 (46 -> 31), Disaster Report 3's intro 64.8 -> 117 (12.9 -> 8.4),
  Twisted Metal's 44.2 -> 54.2, the Armored Cores' intros 61 -> 66 and 70 -> 96. The GE-bound scenes' process time
  within 3% of the base's.
- **The owner's RP6 renderer runs** (given after): Vulkan brings the GE-bound games up; Sonic Rivals, the Need for
  Speeds, Super Monkey Ball, Twisted Metal, Me & My Katamari, Disaster Report 3 and Activision Hits Remixed are the
  same in every renderer (CPU-bound); the Armored Cores and Colin McRae slower in Vulkan. The system-call change holds
  in every renderer; the decode changes are Software's batches only (Vulkan decodes a texture it doesn't hold, every
  row its primitive may reach, at each texture-cache miss, the next place to look for Backbone's games there).
- **Checks**: tests/psp 452/0 (sanitized; three new groups, which broken versions fail), tests/allegrex 58/0,
  tests/psp/ares 307/0, and with master merged in (#221, #222) 469/0, 58/0 and 318/0; arm64 with NDK 28.2 and GCC 11
  compile it as they do the base. Every frame's picture and VRAM hash, then RAM and the end state, identical to the
  base's in every scene at 7 and 1 threads. The library the report's way: all 266 finish, none missing a function, 188
  with the final run's pictures at all four captures; the other 78 differ as runs of one runner do (the clock, which
  starts at the host's time, and the runner's PNG a picture behind under load): with the clock pinned 50 match, and the
  other 28 hash identical to the base's at every one of 3600 frames. Review: no bug; its test gaps and nits fixed; a
  delta review passed, its points fixed; a delta review of the last commit and the docs passed, its points fixed.
- **Merged with master** (#221, #222): round 6 holds a pixel's coordinates at the last texel where q is 0 or below,
  which the reach didn't know; the commit after the merge decodes such a triangle's texture whole and draws it at once
  over its own texture (checks in the two new texture groups fail without it). The merged branch hashes identical to
  master (`caee1a26f`) in all 24 scenes at 7 and 1 threads; the library runs are of the branch before the merges.
- **On the RP6, to measure**: base `5b2a564fa` (2.0.0-test-1's code) against this branch, Software at 7 threads, the
  scenes in `~/phobos-work/scratch/speed9/scenes` (a scene's `state` is an app save state's bytes; part 74's table says
  where each is from boot): expect Killzone's menu, Death Jr. II's character select, Sonic Rivals' stage, Activision
  Hits Remixed's room, MX vs. ATV Untamed, the two Need for Speeds and the movie intros faster; the GE-bound games as
  they were. In Vulkan too for the Need for Speeds and the movie intros (the system calls).
- **Left**: the GE-pixel-bound games (the rasterizer itself next, or Vulkan); MX vs. ATV Reflex's races and On the
  Edge's slow scene not reached; Chinatown Wars' long frames not seen on the Mac (to trace on the RP6).
- Scratch: `~/phobos-work/scratch/speed9`.

## Version codes for 2.1 — 2026-10-12

Branch `cursor/version-base-21-2b67`, on `master` after #221 (the system dialogs) and #222 (round 6's pictures), before
the v2.1.0 tag. The version code is now 210000 + the commit count (`android/app/build.gradle.kts`): test builds on the
owner's RP6 went up to 205231 (the dialogs' 205223, then 205230 with round 6's time model and 205231 with the dialogs
alone, built with -PversionCode), level with what 200000 + the count gives master around the tag (205,229 to 205,232),
and Android refuses a lower code over a higher one without an uninstall (which loses the app's data). Every earlier
build, release, nightly or test build is below 210000, so nobody's update becomes a downgrade; nightly tags become
`nightly-215xxx` and up. README.md and the implementation plan's updater row say so. A later release can raise the
base again the same way if a stray build gets ahead of it.

## PSP: round 6's pictures — lighting's two multiplies, the reciprocal's chords, patches along v — 2026-10-12

Branch `cursor/psp-round6-fits-2b67` (#222), on master (`2a7019c84`). The owner ran round 6 of `tools/psp-measure` on
his PSP (a PSP-1000, ARK on 6.60/6.61, the console of every round); the GE's pictures now match it. docs/psp-core.md,
part 75, has the evidence, every picture before and after. Clean room: no PPSSPP code read, JPCSP not read. The GE's
time and the save-state layout (25) are unchanged: the time model fitted to round 6's timing files is a branch of its
own (`cursor/psp-round6-time-2b67`, not ready to merge: on the RP6 it slowed polling games and made Killzone and Super
Stardust Portable draw far fewer frames, unconfirmed on a PSP).
- **Data**: `tests/psp/measurements/ge-round6` (all 36 files packed with xz, 1.5 MB; run-tests.sh unpacks and checks
  them), and `tests/psp/measured6.cpp`, five groups comparing the core's run of round 6's pictures with them.
- **Lighting**: a light's color times the material's is made a level, then times the cosine's share, a level too
  (floor(256 x cosine), at most 255), then times the fading's: three multiplies of (2a + 1)(2b + 1) >> 10.
  `light-products`, `light-sum`, round 5's `light-share-*` and round 2's point light and spotlight are exact; the eight
  sweeps keep 2123 pixels, the 37 of 8192 normals whose cosine the PSP rounds the other side. The Vulkan fast mode's
  vertex shader does the same.
- **Reciprocal**: its 128 chords read whole (16383 qs): each the straight line of least worst error, chord 0 starting
  at 1. A q of 0 or below takes the texture's last texel. 3D sprites divide as triangles do, texture coordinates are
  read out to 2^22 texels, through mode keeps 15 significant bits. Every reciprocal picture is exact, round 5's
  perspective pictures too but `persp-w3`, worse (the right chord shows the stepper's shortfall over a long run).
- **Patches**: PATCH_CULL_ENABLE changes no pixel; a patch's own normals don't count, its slopes make them. Along v any
  count is drawn, by 64ths past 64 from both ends; a points patch draws the strips' vertices. Along u past 64 hangs.
- **Checks**: tests/psp 454/0 (sanitized; the Vulkan groups on MoltenVK), tests/allegrex 58/0, tests/psp/ares 307/0.
- **Library** (beside the final run, run twice, and the base's runner for every game that differs): all 266 finish,
  none missing a function; 155 the same, 75 within 1 of 255, 36 more. Of the 111 that differ, 61 differ between this
  runner's own two runs, 19 are the base's frames, and 31 are this part's pictures (a level or a texel in lit 3D and 3D
  sprites, at most 1.32 of 255): no game's pace or presses change.
- **Review**: an independent read-only review found no bug; its points (filtering and integer coordinates said to be
  unmeasured, a program that won't load reported, stale comments, `.gitattributes`, the unpack's messages, a curves
  check without data) are fixed. The measurements' README gives every round's console as a PSP-1000.
- **Round 7** (part 75's list): the stepper over a long run, positions divided by w, the cosine's last bits,
  non-power-of-two divisions, q of 0 with a clamped texture, through mode's shared exponent, chord 127's start.

## PSP: the system's dialogs shown by the app — messages, the keyboard, saving and loading — 2026-10-12

Branch `cursor/psp-dialogs-2b67`, on `cursor/psp-merge-212-219-2b67` (master with #212 to #219, PR #220, which master
has since merged with the same tree). docs/psp-core.md, part 73, has the evidence. Clean room: pspautotests'
recordings, pspsdk's headers, PPSSPP's dialog code read only as a description of the PSP's dialogs, Kenka Bancho's own
code; JPCSP not read.
Save states go to StateVersion 26 (15 to 25 still load).
- **Before**: the kernel answered every dialog at its first Update: messages Yes, the keyboard the field's own text
  (or "PSP"), the save dialogs that ask on their own (a list's first name), so a player couldn't type a name, choose a
  save or say no.
- **The kernel** (`ares/psp/kernel/dialogs.cpp`, the option "Dialogs"): a message, the keyboard and the save modes
  that ask the player (load, save and delete from a list, list all delete, and one save's load, save and delete) post
  a request (`dialogID()`, `dialog()`, `dialogBytes()`) and run until the front end answers (`answer()`), the game
  updating every frame meanwhile; the answer finishes the dialog with the PSP's results (the message's button by its
  size's rules, the keyboard's texts and CHANGED or CANCELLED, the chosen save loaded, saved or deleted, its name
  written back, LOAD_NO_DATA and DELETE_NO_DATA after "There is no save data.", CANCEL for backing out, a list delete
  going on until nothing's left); messages a PSP doesn't show finish with BADOPTION or ERRORCODEINVALID at their 9th
  Update, as recorded. Each request has its own number; answers to another, or not among its choices, are refused. An
  abort, a reset or a loaded state withdraws it, a dialog still running asking again at its next Update. Without the
  option (the runner, the tests, the desktop and libretro hosts) every answer is as before.
- **Kenka Bancho's empty stick**: its LOAD GAME is a list load of "SGL" (SGL001 to SGL007); its code takes 0 as a
  load, 1 back to its title, anything else as its "An error has occurred". No recording lists nothing to load, but a
  PSP's neighbouring cases (list deletes that emptied their list, a single load of nothing) and PPSSPP's notes give
  the group's NO_DATA after the PSP's own "There is no save data.": so LOAD_NO_DATA is kept (it was right), and what
  was missing, the PSP's notice, is now the app's. The game's error then follows, as on a PSP.
- **The app**: `ui/PspSystemDialogs.kt` draws them as the app's `PhobosAlertDialog`s over the running game (every
  theme, retrowave too): a message with Yes/No (No first where asked), OK or no button, and Back; the keyboard as text
  fields with Android's keyboard, the field's description as the title, its length limit and digits-only fields kept;
  saving and loading as a list of saves (ICON0, save title, detail, date, size; "New Data" with the game's icon), with
  the PSP's questions (overwrite, delete), "There is no save data." and "The data is corrupted.". The D-pad moves, the
  mapped PSP buttons confirm and back out with circle confirming where the game says so (its buttonSwap 0), Start
  enters text, the pause hotkey opens the pause menu (the dialog waits behind it); touch works throughout.
  `MainViewModel.followPspDialogs()` polls the request's number every 50 ms; JNI `pspDialogId()`, `pspDialog()`,
  `answerPspDialog()` carry the core's byte form (`util/PspDialogs.kt` parses and answers it). Each dialog takes the
  focus from inside its own window's content, as a controller's keys need.
- **Checks**: `tests/psp/run-tests.sh` (sanitizers) 461 groups, 0 failures; `tests/allegrex/run-tests.sh` 58, 0;
  `tests/psp/ares/run-tests.sh` 318 checks, 0 failed; Android `:app:compileModernReleaseKotlin
  :app:testModernDebugUnitTest` 299 tests, 0 failures; the touched native files built with NDK 28.2 for arm64. Without
  a front end, nine runner runs (Kenka Bancho, Gundam vs. Gundam NEXT PLUS, Valkyria Chronicles II twice, Ridge Racer
  2 twice, Dissidia) leave the same notes, dialog-call logs and memory-stick bytes as the base's runner, the clock
  pinned. With a scripted front end, Kenka Bancho, Ridge Racer 2 (named, saved, backed out of), Daxter, Gundam, Space
  Invaders Extreme, Disgaea Infinite, Ys Seven, The Sims 2 and Bubble Bobble Evolution each go on once answered. A
  scratch Robolectric test sends a controller's keys to each dialog's window: all answered. No device used.
- **Review**: an independent reviewer found NEEDS CHANGES (a list mode without a list offered nothing; the dialogs
  took the focus too early for a controller; the docs' checks; the comparison's build; and lows), all resolved; a
  delta review then asked for two statements corrected and five nits, all taken, and a third passed. A later fix (a
  single save's overwrite and delete questions now start at No, as on a PSP) had its own delta review (psp-core.md
  part 73, "Review"); the verdicts are in the commit messages.
- **APK for the RP6**: the release build of these sources, version code 205223 (above the installed 205182 and
  master's nightly-205221), named 2.0.0-psp-dialogs, signed with the repository's release key, so it installs over
  the current app keeping its data. SHA-256
  cd8f6b307d90292540bb325f7cb830badc4d03a31d894e137bdf0dbe65e80779; on the Mac (not published) in
  `~/phobos-work/scratch/dialogs/apk-tree/android/app/build/outputs/apk/modern/release/`.
- **On the RP6, to check**: (1) Kenka Bancho, LOAD GAME on an empty stick: "There is no save data." appears over the
  game; OK (or the back button) closes it and the game shows its own error, Cancel and return goes to the title.
  (2) Ridge Racer 2's first boot: "Driver Name (7 CHAR MAX.)" opens with XEVIOUS and Android's keyboard; no more than
  7 characters go in; OK keeps the typed name (seen in its profile), Cancel goes back to its title; then "Do you want
  to save this data?" for its first save, Yes chosen first; if a later save asks "Do you want to overwrite this
  data?", No must be chosen first. (3) Daxter (Start, then Cross at its menu) or Resistance: Retribution: a
  list of save slots, all "New Data"; choose one, then on the next boot the same list shows the save with its icon,
  title, detail, date and size, and choosing it asks "Do you want to overwrite this data?" with No first; No goes back
  to the list. (4) A list load with saves (Disgaea Infinite, Ys Seven or Cladun after saving): the list, the newest
  first, D-pad up and down, confirm loads. (5) A message: Dissidia's "This game has an autosave function." with OK; a
  Yes/No question in any game, the D-pad moving between them. (6) The confirm button: Gundam vs. Gundam NEXT PLUS
  (Japanese, its dialogs set circle to confirm) asks for a name early on (an 8-character keyboard): its foot shows
  "○ Enter ✕ Cancel" and circle enters; in Ridge Racer 2 (English) cross enters. (7) The pause hotkey with a dialog
  up opens the pause menu, and resuming brings the dialog back; Save State while a dialog waits, then Load State: the
  dialog comes back. (8) The
  dialogs in two or three themes (retrowave, a pixel theme, a light one), full screen and not, portrait and
  landscape; touch alone. (9) A game with no dialog runs as before (nothing appears). (10) The controller first:
  as each kind of dialog opens (a message, the keyboard, a save list), don't touch the screen; the D-pad must move
  the ring and confirm and back must answer at once, and the keyboard must open Android's keyboard by itself. If the
  controller does nothing, logcat (tag Phobos) says "PSP dialog N: no focus, so no controller".
- Scratch: `~/phobos-work/scratch/dialogs`.

## Merged into master: #212 to #219 — 2026-10-11

Branch `cursor/psp-merge-212-219-2b67`: master plus #212 (speed 8), #213 (64 controller samples), #214 (PSP pause-menu
section, right stick), #215 (Crash's save dialog), #216 (the game's name in the pause menu), #218 (savedata files mode,
PARAM.SFO), #217 (a module's stop gives 0) and #219 (vnop keeps the VFPU prefixes), merged in the docs' part order.
Conflicts were docs only, plus #214 and #216 each adding a line after `_loadedGame.value = null` (both kept): the
handoff entries newest first, parts 68 to 72 in order, and one summary table in docs/psp-compatibility.md with columns
for parts 70, 71 and 72 (197 menu or gameplay, 68 movie, 1 black; README the same). The code is the same as the test
build installed on the owner's RP6 as 2.0.0-test-1 (205182): suites 449/0, 58/0, 307/0 on it; on the RP6 MACH, Lunar,
Crash Tag Team Racing, Kenka Bancho, Final Fantasy IV and both Metal Gear Acids pass the screens that stopped them,
Valkyria Chronicles II and III and Shining Blade find their saves on a second boot, and the pause menu shows the game's
name, the PSP section and Restart to Apply.

## PSP: the Metal Gear Acids' titles and menus — vnop leaves the VFPU's prefixes — 2026-10-11

Branch `cursor/psp-mga2-2b67`, on master (`3cae0f907`, v2.0.0). The owner's run of 2.0 on the RP6 left Metal Gear Acid
2 on a still yellow-and-black picture after the Kojima Productions logo, every press seemingly ignored; investigated
and fixed on the Mac alone (the RP6 was busy). docs/psp-core.md, part 72, has the evidence. Clean room: pspdev's VFPU
documentation and its tests, the game's own code; no PPSSPP code read, JPCSP not read. Save states unchanged (25).
- **Cause**: the game wasn't waiting for anything: it took Start at its title and opened its menu, but drew none of
  its sprites. It clips each one with `vpfxt; vcmp; vpfxt; vnop; vnop; mfvc; vcmp`, the second prefix meant for the
  second vcmp; both CPU engines used it up at the vnop (as pspdev's documentation says, against what the game's code
  needs), so every sprite inside the screen read as left of it and was culled: the logo, PRESS START, the menus, the
  Solid Eye notice's text. Only full-screen pictures showed.
- **Fix** (`interpreter-vfpu.cpp`, `recompiler-vfpu.cpp`): vnop, like vsync and vflush, leaves the prefixes for the
  next instruction. This reverses part 3's reading of the documentation (its tests never showed vnop using them up;
  PPSSPP keeps them too).
- **The game now**: the notice, the logos, the title with its logo and PRESS START BUTTON, the main menu on Start (Cross
  closes it, Circle confirms), and with Start then Circle a new game's carry-over prompts and its tutorial. Metal Gear
  Acid, which clips the same way, shows its title, menu and first scene where its row had a "static picture".
- **Checks**: tests/allegrex 58/0 ("vfpu more" now pins all three prefixes through vnop, vsync and vflush, a vnop in a
  delay slot, and the game's clipping sequence, on both engines), tests/psp 438/0 (sanitized), tests/psp/ares 307/0.
  The whole library the report's way beside the final run: both Metal Gear Acids move from movie to menu (menu or
  gameplay 194 -> 196, movie 71 -> 69); every other game shows the final run's scenes, a fade, an animation step, a
  movie's frame, Battlefront II's random map or PixelJunk Monsters' clock-coloured sky apart where they differ; all 266
  finish, none missing a function. Counted, only four games run a vnop with a prefix pending at all: the two Metal
  Gear Acids, and Portable Ops and Portable Ops Plus, whose frames don't change; the other differences are the runs'
  own. An independent review found no bug; its test gaps (rt's and rd's prefixes
  through the three, a delay slot, vflush in the random programs) and stale comments were fixed; a delta review found
  no bug either, and its points (the report's summary column, README's counts, wording) were fixed.
- **Left**: not checked on a PSP; rs's and rd's prefixes surviving vnop is the same rule, not seen. The report's summary
  gains a Part 72 column on the final run's numbers, and README's counts follow it (196 and 69); parts 70 and 71, on
  their own branches, add columns too (part 71 moves Final Fantasy IV to menu), so whichever merges later sums them.
- Scratch: `~/phobos-work/scratch/mga2`.

## PSP core: a module's stop gives 0 — Kenka Bancho past its intro movie — 2026-10-11

Branch `cursor/psp-kenka-2b67`, on `master` as v2.0.0 shipped (`3cae0f907`). docs/psp-core.md, part 71, has the
evidence. Clean room: pspautotests' modules/unloadfill and its PSP recording, uOFW's module manager, pspsdk's error
codes and the game's own code; no PPSSPP or JPCSP source read. Save states unchanged (StateVersion 25). Docs part 71,
after the two branches in flight that take parts 69 and 70 and StateVersion 26 (`cursor/psp-hangs-2b67`,
`cursor/psp-savename-2b67`), so the three merge in any order but for the docs' part numbers.
- **The bug**: the owner's RP6 test of v2.0.0 found Kenka Bancho: Badass Rumble black for good after Start skipped
  its intro (60 fps, idle). When its movie ends, skipped or played out, the game's module thread stops the movie
  player's two modules (Sony's PSMF.PRX and LIBPSMFPLAYER.PRX, which the kernel stands in for) once a frame until
  `sceKernelStopModule` gives exactly 0, then unloads them. The kernel gave the module's ID, then ALREADY_STOPPED, for
  good. A PSP gives 0 (unloadfill recorded `sceKernelStopModule: 00000000`; uOFW's stop command has 0 too), the ID
  only for a start and an unload.
- **The fix** (`ares/psp/kernel/modules.cpp`): a stop's caller gets 0, whether the module stopped at once (a
  stand-in, no module_stop) or after its module_stop ran while the caller waited; module_stop's result still goes
  where asked. The game now reaches its title from about frame 2900 with Start at 2700 (frame 7800 with the movie
  played out, black from 7500 for good before), its main menu, and NEW GAME's difficulty, name entry and hometown map.
  LOAD GAME on an empty stick shows the game's own "An error has occurred" (the savedata list load answers
  LOAD_NO_DATA at once: the savedata utility's part).
- **Final Fantasy IV: The Complete Collection** had the same bug (the same module thread): white for good after its
  title movie, now its title, the collection's menu and Final Fantasy IV's opening.
- **Checks**: tests/psp 440/0 (sanitized; `master`'s 438 here and the new "modules stops give 0", the game's sequence
  on both engines, and "modules state while stopping"), tests/allegrex 58/0, tests/psp/ares 307/0; with the old
  kernel the two new groups and the three updated ones fail. An independent read-only review: correct on every path,
  its nits taken (a state saved mid-stop tested, comments, part 19's lines).
- **Library** (the report's run against the final run): all 266 finish (Killzone, killed at 900 s with the Mac at a
  load of ~220, finishes run again), no missing functions; Final Fantasy IV movie -> menu; the 70 others that differ
  show the same scenes a few frames apart (the runner's PNG a picture early under load): 64 never call
  sceKernelStopModule, and the six that do act alike on both runners. Counts: menu or gameplay 195, movie 70.
- **Left**: by uOFW a module_stop returning non-zero keeps its module running (the caller told CAN_NOT_STOP or the
  value), and a module_start returning NO_RESIDENT or an error gets its module unloaded; here neither, as before (no
  recording, no game seen needing it).
- Scratch: `~/phobos-work/scratch/kenka`; nothing run on the RP6.

## PSP: savedata's files mode and PARAM.SFO — the second boot that called its save corrupted — 2026-10-11

Branch `cursor/psp-savename-2b67`, on `master` (v2.0.0): the "<>" fix (`5fd162d75`), then this (`340649d14`).
docs/psp-core.md, part 70, has the evidence; docs/psp-compatibility.md's rows are updated. Clean room: pspautotests'
recordings, pspsdk, PPSSPP's savedata code read only as a description of the PSP; JPCSP not read.
- **The fix** (`utility.cpp`, with tests): the files mode (12), which answered 0 with an empty list, fills it, secure,
  normal and system files apart (utility/savedata/filelist); every save mode writes the PARAM.SFO a PSP writes (4912
  bytes: titles, SAVEDATA_FILE_LIST naming the secure files, hashes zero) and the icons it's given; loads fill sfoParam
  from it, the list mode dates saves by it; loads, reads and erases answer as loadbroken, deletebroken and
  loademptyfilename recorded (FILE_NOT_FOUND, DATA_BROKEN, 0 with no file named; erases in the read/write group); the
  sizes modes count a secure file 16 bytes longer, as a PSP stores it. Saves made by v2.0.0 (no PARAM.SFO) still load,
  their files normal (chosen: a PSP calls such a save broken), so a game checking its files' kinds calls one corrupted
  once. No new state (StateVersion 25).
- **Games**, two boots on one stick: Valkyria Chronicles II and III and Shining Blade find their saves on the second
  boot (they called them corrupted, deleted them and made them again); Space Invaders Extreme, Hot Shots Golf 2, Def
  Jam and Mega Man Powered Up load theirs, as before; Monster Hunter Portable 3rd makes no save in the runs; Crash Tag
  Team Racing waits at "Checking Memory Stick Duo" on every runner, its one savedata call answered rightly (another
  agent's).
- **Library** (the report's run, this branch's runner beside the final run's `lib-top`): every game's scenes and
  categories as there (menu or gameplay 194, movie 71, black 1), no function missing. 79 differ at some capture, all
  as two runs of one runner differ: run again with both runners at the same time, 41 of 77 match exactly, and the six
  closest to splitting by runner mix both runners' frames over three more runs each. Ghost of Sparta and Killzone, at
  the 900-second limit with other work on the machine, finish alone with the final run's frames.
- **Checks**: tests/psp 443/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0; ares/psp/psp.cpp built with NDK
  28.2 for arm64. Review: NEEDS CHANGES (a crafted PARAM.SFO's value rooms unbounded: a write far past a buffer), fixed
  with its minor findings; three delta reviews, the last PASS with nothing left.
- **Left**: no encryption (these saves don't load on a PSP; encrypted ones copied in aren't read); loads don't fill
  the icon buffers; DELETEDATA's no-save answer (outside this change).
- Scratch: `~/phobos-work/scratch/savename` (two-boots.sh, the runners with a [save] trace, review1-4, the library
  runs).

## Pause menu: the running game's name beside "Emulation Paused" — 2026-10-11

Branch `cursor/pause-title-2b67`, on `master`, at the owner's request, so the pause menu always says which game is
paused. `MainViewModel.loadedTitle` is set when a game loads and cleared with `loadedGame`: `gameTitle()`
(`util/RomNames.kt`) gives the disc's own title when the library read one (a PSP disc's PARAM.SFO title) and the file's
name without its extension otherwise; a game started from outside the library (a frontend's intent) has no disc title,
so it shows its file's name. `EmulationMenu`'s `PausedHeader` puts it beside the heading when the menu is at least
480 dp wide (one line, ellipsized) and below it in narrower menus (portrait, split screen); TalkBack reads both as one.
Independent review: no High/Medium; its Lows on right-to-left placement, narrow windows and accessibility are taken.
Unit tests: 275 passed (two new in `RomNamesTest`); not yet seen on a device.

## PSP: the RP6's two stuck games — Crash's save dialog closed from a worse thread, DBZ loading behind a copy — 2026-10-11

Branch `cursor/psp-hangs-2b67`, on master (`3cae0f907`, v2.0.0). The owner's run of 2.0 on the RP6 left Crash Tag Team
Racing at "Checking Memory Stick Duo..." and Dragon Ball Z: Tenkaichi Tag Team at "Now Loading"; investigated on the
Mac alone (the RP6 was busy). docs/psp-core.md, part 69, has the evidence. Clean room: pspautotests' recordings, PPSSPP
read only as a description of the PSP's dialog threads, JPCSP not read; the PGD code untouched. Save states unchanged
(25).
- **Crash** (fixed, `utility.cpp`'s `dialogShutdown()`): its main thread (priority 0x3f) closes its save dialog and
  starts the next one at once, without looking at the status; the kernel kept a save dialog closing for 2 ms whoever
  closed it, refused that start (INVALID_STATUS), and the game polled for good. A PSP closes a save dialog in the
  utility's threads, last at 0x20, without waiting (pspautotests' utility/savedata/shutdownstatus: callers at 33-36
  and 111 read NONE at once, 32 SHUTDOWN): a caller worse than 0x20 and the dialog's graphics and access threads now
  finds it closed at once. Crash reaches its title, attract race, menus and name entry, and makes and loads its save.
- **DBZ** (no change): not stuck. Its "Now Loading" is the title menu's attract demo loading (about 570 frames in the
  runner, then the demo); on the RP6 it ran at about a sixth of the PSP's speed, frames of up to 20 s with the
  emulation thread blocked, while phase B's other 236 games were being copied onto the same SD card (every game run
  during the copy shows such frames, none after it). Its 600 frames by the run's end reached about the runner's frame
  7620, the black before the demo. New Game, its first save and a stage's loading all go on in the runner.
- **Checks**: tests/psp 439/0 (sanitized; master's 438 here and a new group, "utility save dialog closed by priority",
  which each broken version of the fix fails), tests/allegrex 58/0, tests/psp/ares 307/0. The whole library the
  report's way beside the final run: 81 games close a save dialog from such a thread now; Crash's row moves (its
  attract race at 3600, still "menu or gameplay"); every other game's frames are the same scenes as before, a fade or
  an animation step apart where they differ (three titles a step apart from the closing taking no time now); none
  missing a function. Super Monkey Ball Adventures hit the 900 s limit with the Mac shared with other jobs; run again
  with no limit its frames match, and beside master's runner it runs at the same speed. An independent review of a
  first version (the caller waited the 2 ms) led to closing at once, invalid priorities ignored, the comment's evidence
  split from PPSSPP's description, and two more test cases; a delta review found no new bug and corrected the docs'
  figures.
- **Left**: the dialog threads' borrowing of the volatile memory (PPSSPP's description, unrecorded); DBZ to run again
  on the RP6 with nothing copying onto the card.
- Scratch: `~/phobos-work/scratch/hangs`.

## Android: a PSP section in the pause menu, and the right stick — 2026-10-11

Branch `cursor/psp-pause-menu-2b67`, on `master` after v2.0.0 (`3cae0f907`), at the owner's request from playing PSP
games on the RP6. App only: no native or core change.
- **Pause menu**: a PlayStation Portable section while a PSP game runs (`PspSection`, `ui/EmulationMenu.kt`), with
  Settings → Emulation's PSP Renderer, Resolution, Drawing Threads and the new Right Stick: the same items
  (`ui/Components.kt`) and the same global settings, as the N64's, the PS1's and the Neo Geo CD's pause-menu options
  are (only the ZX control scheme and the Controller pages keep a game's own). The core takes the renderer, resolution
  and drawing threads only as a game loads (`PhobosRunner.cpp` hands them over as `option()`s before
  `PlayStationPortable::load`; the menu's Reset is `power(true)`, which keeps them), so the view model keeps what the
  running PSP game started with (`pspStartSettings`; `PspStartSettings` in `util/PspVideo.kt`). Once Settings' differ
  (a resolution under the software renderer, which draws at Native, doesn't count), a Restart to Apply row appears
  ("Reset keeps the ones it started with"). After a warning that progress since the last save is lost, it reloads the
  game as the Reload hotkey does, but without Auto-Load State's state (`reloadGame(context, autoLoad = false)`): that
  state is from the last quit, and a game carrying on from it could save over newer memory-stick saves.
- **Right stick** (`input/PspRightStick.kt`, `GameInputState.updateSticks`): Off (as before); Face buttons (up
  Triangle, right Circle, down Cross, left Square: the Y, B, A and X bits `resolveButtonBit` reads them from), for
  twin-stick shooters such as Super Stardust Portable; D-pad, for a camera on the D-pad; Analog stick (the right stick
  is the PSP's stick, and the left takes the right's place, which the PSP doesn't read). The directions come from
  rx/ry as the user's bindings give them (the R-Up/Down/Left/Right rows, at any level), each latched with the N64
  C-buttons' hysteresis (one `axisPressed`: press past 0.5, let go at 0.4), so a diagonal presses two. The stick's
  buttons are a mask of their own (`rightStickButtons`), OR'd with the pad's and the touch controls': a button held on
  both lets go only when both do; pausing (`releaseAllButtons`) and `reset()` release it. A changed choice applies at
  the stick's next move. The PSP touch layout has no right stick, so the touch controls are unchanged (its one stick is
  the PSP's in every choice). Stored as `psp_right_stick`, an enum name; one this build doesn't know reads as Off.
  Global, like the other pause-menu options; a game's own choice could come later, through the Controller pages.
- **SettingsStore** takes its DataStore (`SettingsStore(context)` opens the app's, as before), so a JVM test runs the
  real store on a file.
- **Checks**: `:app:compileModernReleaseKotlin`; `:app:testModernDebugUnitTest` 289/0 (16 new): `PspRightStickTest`
  (each choice, diagonals, the thresholds, the pad's and the touch controls' presses with the stick's, a choice changed
  mid-hold, other systems, reset), `PspSettingsTest` (the store on a DataStore file, reopened: the default, each
  choice, an unknown or mistyped value, every PSP setting), `PspVideoTest` (when a restart changes how the game runs).
  Three deliberate breaks (the right stick left out of the settings read, the stick's buttons folded into the pad's
  mask, no release threshold) each failed the tests meant for them. An independent read-only review found no bug (its
  points taken: the restart's Auto-Load State, Reset against Restart in the row, the dialog outside the list, a name,
  two test names); the delta review passed. Not run on a device: the RP6 was busy with a long test.
- **On the RP6, to check**: the section shows for PSP games only; changing the renderer (or the resolution under
  Vulkan, or the threads) shows Restart to Apply, which restarts into the new renderer, and changing it back hides the
  row; with Auto-Load State on, the restart still starts the game over. Right Stick: Face buttons in Super Stardust
  Portable (aiming and firing, a diagonal firing two buttons); D-pad in a game that turns the camera with it (Monster
  Hunter Freedom Unite); a face button held on the pad while the stick presses the same one, then either let go
  first; Analog stick moving the PSP's stick from the right, the left stick doing nothing; Off as before; a PS1 game's
  right stick still analog; the choice the same in Settings and the pause menu.

## PSP: a read of all 64 controller samples (MACH's title takes Start) — 2026-10-11

Branch `cursor/psp-ctrl64-2b67`, on `master` (v2.0.0). The owner found MACH ignoring Start at its title on the RP6.
Its title loop reads all 64 kept samples each frame (sceCtrlReadBufferPositive with a count of 64), which
`ares/psp/kernel/ctrl.cpp` refused as too many (INVALID_SIZE), so no button ever reached it; the library runs never
showed it, their Start coming before the title. Up to 64 may now be asked for, more refused, in the peek and read
functions alike. In the runner, Start/Cross at the title now takes MACH to its profile handling. tests/psp's controller
group asks for 64 (and 65, refused); tests/psp 438/0.

## PSP: WipEout without waiting for the GPU, the stencil as each format keeps it, the emulation thread at 1x — 2026-10-11

Branch `cursor/psp-speed8-2b67`, on top of speed 7's `cursor/psp-speed7-2b67` (part 67), measured against its tip
`671072d6f` (#204's Experimental merge in it). docs/psp-core.md, part 68, has the evidence; docs/psp-gpu-renderers.md's
"Owned until needed" and "Depth, stencil, blending and logic operations" the design. Clean room: no PPSSPP code copied
or translated, JPCSP's not read. Save states unchanged (25).
- **Changes** (each its own commit, with its tests): WipEout's one finish a frame was a CPU load of its depth buffer
  through VRAM's fourth copy (the screen's middle, after a SIGNAL mid-list), where its bloom's buffers lie: the GPU's
  depth never reaches memory, so the finish only gave it the last bloom's colors; a load through VRAM's second or
  fourth copy (through `Memory::read()`) now waits only where the drawing threads (which write memory's depth) reach
  it, not for a hardware renderer (`Memory::vramDrawnOver`'s `depthLoad`). The GPU renderer's stencil: a 5650 frame
  buffer's test compared with 0 (failing, nothing drawn), and a 5551 or 4444 one's held as the GE keeps it after
  clear mode (points' and lines' too, which had left it 0), a REPLACE and MASK_ALPHA (a 5551 REPLACE's comparison
  made on the top bit where it must). A PRIM's vertices transformed a stage
  at a time over 64 of them (the same arithmetic per vertex).
- **RP6, means of two rounds, each runner first in one** (fps, Turnip accurate / fast, Qualcomm's accurate / fast):
  WipEout's race at 1x 99.8 / 105.4 / 94.8 / 96.3 -> 135.4 / 141.8 / 125.9 / 127.6 (+33-36%, the emulation thread
  8.75-10.1 -> 7.0-7.9 ms), at 4x 38.7 / 59.8 / 43.1 / 39.3 -> 46.3 / 87.1 / 67.4 / 63.9 (+20-63%); God of War, MC3 and
  LCS within 1.5% at 1x and 4x (the staged transform +0.5-1.2% in God of War's battle, in rounds of its own); the
  software renderer within 1.6%, every end state the base's.
- **Pictures**: the software renderer's byte for byte (the measure harness's 173 files; every frame and end state of
  nine scenes at 1 and 7 threads on the M1, but MC3's menu's end state, which differs between runs of the base's own
  runner too; every end state on the RP6). Vulkan: 76 of 80 RP6 comparisons (ten scenes, four configurations, 1x and
  4x) byte for byte; the four others WipEout at 4x, now shown from the GPU's 4x target instead of memory's picture at
  the PSP's size scaled up (it was never shown above 1x before); it draws the same (its loads read 0 either way), and
  against the software renderer it's as close by the 4x4 blocks' middle samples (35.77 -> 35.71 and 35.64%).
- **Emulation thread profile** (simpleperf, -g, the RP6): the time is spread: the vertex's arithmetic (its divisions
  and lighting waiting on themselves; the X3's divider measured, not the bound), its reading, each PRIM's settings,
  each triangle's steps, the CPU's compiled code (16-29%). Tried and dropped, as no faster: vertices read in place,
  the texture cache's lookup without shares, the list's words straight from the page, `transformKept()`'s blocks;
  `vfpuDot()` without its checks was slower.
- **Checks**: tests/psp 437/0 (sanitized; three new groups, which broken versions fail, but a REPLACE's color alpha
  left whole), tests/allegrex 58/0, tests/psp/ares 307/0; the GPU tests on the RP6 33/0 on both drivers (one skips
  itself on Qualcomm's); GCC 11 and 13 as the base (four.cpp's maybe-uninitialized warnings, the base's the same).
  The states test's frame under the address sanitizer is within a few KB of the main thread's 8 MB: a machine 72
  bytes bigger overflowed it (fixed: the machine is the base's size).
- **Left**: the emulation thread's many small steps (a GE on a thread of its own would take most of them off it); the
  recompiler's registers kept in memory; a 16-bit stencil's increments and decrements; WipEout's depth loads read
  memory, not the GPU's depth.
- Scratch: `~/phobos-work/scratch/speed8`; nothing left on the RP6.

## Version codes for 2.x — 2026-10-11

Branch `cursor/version-base-2x-2b67`, on `master` after the PSP stack merged (#208, #210), at the owner's request before
the v2.0.0 tag. The version code is now 200000 + the commit count (`android/app/build.gradle.kts`), so v2.0.0 is
205,18x: the PSP work's test builds on the owner's RP6 went up to 106407, past what 100000 + the count gives 2.0
(105,18x), and Android refuses a lower code over a higher one. Every earlier build, release or nightly, is below 200000,
so nobody's update becomes a downgrade; nightly tags become `nightly-2xxxxx` and the newest three are kept as before.
README.md and the implementation plan's updater row say so.

## PSP: the final library run before the merge into master — 2026-10-11

Branch `cursor/psp-final-numbers-2b67`, on #207. Docs only. The whole library (266 games, the report's run: 3600
frames, three at a time, software renderer at 7 threads) on `be543afba`, #206 on the stack's top (#207 adds only the
Library's PSP art): menu or gameplay 194 (189 in part 64's run), movie 71, black 1, stuck loading 0, timed out 0, no
missing functions. The six black games all read PGD data, which now decrypts (#206): five reach a title, menu or prompt,
and Black Wolves Saga is loading its opening at 3600 (it plays from about 4200). Eighteen other games differ from part
64's run only by a few frames (fades, animation steps, movies further on); none shows less.
- **Docs**: docs/psp-compatibility.md (a Final column, the six rows, the run's paragraph); README.md (the PSP's row,
  its compatibility line, the known issues, PGD-protected data decrypted).
- **Release**: the owner merges the stack through #208 and tags v2.0.0 right after, so the README speaks of 2.0 (the
  latest release, the desktop packages with it, the PSP new in 2.0 and marked Experimental in the app) rather than of
  the nightly builds.
- Scratch: `~/phobos-work/scratch/final` (lib.sh, `lib-top/` the run, `cmp-top.tsv` against
  `~/phobos-work/scratch/gow-sw/lib/fix`, `diff-sheet-*.png`).

## Library: the PlayStation Portable's own console art — 2026-10-11

Branch `cursor/psp-console-art-2b67`, begun on #205's `cursor/psp-speed7-2b67` (`671072d6f`), so it ships with the
Experimental PSP core. The Systematic pack (BAXY Square's, MIT) has no PSP, so `systematicAssetFor`
(`ui/LibraryScreen.kt`) gave the PSP's tile the PlayStation's console.
- **The art**: `assets/platforms/psp.svg`, drawn for Phobos in the set's style (LICENSE says so after the Systematic
  notice): a PSP-1000 from the front, with its wide pill body and 16:9 screen, the D-pad's four buttons with the nub
  below, the face buttons' triangle, circle, cross and square, the row under the screen, L and R along the top corners,
  and the speakers and power light where the PSP-1000's manual puts them. It uses the set's 256 grid shown at 4/3, its
  greys and its 8-unit white halo at half opacity; it's 330.667 x 165.333, slimmer than the GBA's 186.667, as a PSP is.
  The D-pad, nub and marks are lighter than mid-grey, since the recolor takes darker greys below a dark theme's tile,
  where the Game Gear's controls vanish into its body.
- **Code**: the `psp` -> `psx` alias is gone and `psp` is in `SYSTEMATIC_ASSETS`; nothing else gave the PSP the
  PlayStation's art (the Phobos, Pixel and Manga packs keep their own `psp` glyph).
- **Checked**: `:app:compileModernReleaseKotlin` and `:app:testModernDebugUnitTest` (273 tests, none failing;
  `ConsoleArtTest` recolors the new SVG in every theme). Previews outside the repo drew it beside the GBA's, Game Gear's
  and PlayStation's art at 120-315 px, in grey and through the app's own `ConsoleArtPalette` mapping for all 39 theme
  variants. Not yet seen on a device.

## PSP: God of War faster in every renderer — a move's stencil in one draw, the GE's PRIMs cheaper, logic operations in fours — 2026-10-11

Branch `cursor/psp-speed7-2b67`, begun on #200's `cursor/psp-gow-vk-2b67` with #201's `cursor/psp-measure6-2b67`
merged under it, then #204's `cursor/psp-experimental-2b67` (#181, #202, #203 and #204: the desktop's disc titles, the
README, PGD in states, the Experimental mark; none touches the core's speed); the RP6 measured against #201's tip
`055029692` (#201 changes no GE, renderer or CPU code; the
M1's and some earlier RP6 checks were against `c86461ec6`, as part 67 says). docs/psp-core.md, part 67, has the evidence;
docs/psp-gpu-renderers.md's "Two targets over the same pages", "A texture running into the frame buffer below" and
upscaling's uploads the design. Clean room: no PPSSPP code copied or translated, JPCSP's not read. Save states
unchanged (24).
- **Pictures**: the software renderer's byte for byte the base's (the measure harness's 137; every frame and end state
  of the bench scenes and God of War's two at 1 and 7 threads on the M1, but MC3's menu's end state, which differs
  between runs of the base's own runner too; every end state the same on the RP6). Every Vulkan frame
  of the bench scenes and God of War's two, in all four configurations, the base's at 1x, and at 4x but Midnight Club
  3's menu (on the RP6 76 of 80 comparisons identical, the four MC3's 4x): its frame buffer below now keeps the GPU's
  own pixels where part 65's round trip kept one of each pixel's sixteen; against the software renderer as close or
  closer by the blocks' means and three of four samples (RP6 49.9 -> 52.2% identical by the means), 0.7 points further
  by the top-left one.
- **Changes** (each its own commit, with its tests): moves and fills above 1x give the stencil with the colors in one
  draw where the driver has `VK_EXT_shader_stencil_export` (both of the RP6's), else draw only the bits the format
  keeps, none for 5650; a texture over two targets is moved into one only where the PRIM doesn't draw over the pages
  moved (MC3's menu: no moves, part 63's two-part copy); logic operations four pixels at a time in the software
  renderer; and for God of War's 37,600 PRIMs a frame at 1x: a PRIM's 3D settings kept and worked out again only where
  their words change, vertices in a row asked about at once, the renderer's page scans skipped while nothing changed,
  list words read without asking twice, the backend's dynamic state set only where it differs, fills a row at a time,
  the GE time's triangle areas cut only where they cross.
- **RP6, means of two rounds, each runner first in one** (fps, Turnip accurate / fast, Qualcomm's accurate / fast):
  God of War's battle at 1x 16.8 / 17.0 / 15.8 / 15.8 -> 22.1 / 22.4 / 20.5 / 20.6 (+30-32%, the emulation thread
  59-63 -> 44-48 ms a frame), its menu 73.6 / 76.4 / 66.4 / 67.1 -> 101.7 / 104.8 / 88.2 / 90.6 (+33-38%); at 4x the
  battle 8.46 / 14.3 / 10.9 / 10.0 -> 9.93 / 18.8 / 13.9 / 12.5 (+17-32%; the GPU's moves 17.3 -> 3.6 ms a frame on
  Turnip, 22.6 -> 6.3 on Qualcomm's), the menu 17.9 / 23.0 / 20.5 / 19.7 -> 21.2 / 28.9 / 26.1 / 24.5 (+18-27%).
  MC3's menu at 1x 103.1 / 108.4 / 93.5 / 97.8 -> 113.0 / 119.3 / 107.7 / 112.9 (+10-15%, past part 63's 105 on
  Qualcomm's), at 4x +26-35%. The other bench scenes at 1x +5-7% (Killzone's menu and the 2D two about the same).
  The software renderer: God of War's battle 4.29 -> 4.32 at 1 thread, 8.41 -> 8.73 at 7, the bench scenes within 2%;
  logic operations 2.4-3.3 times as fast.
- **Checks**: tests/psp 431/0 (sanitized; three new groups and cases in the moves, scaled and stacked texture groups,
  which broken versions fail), tests/allegrex 58/0, tests/psp/ares 307/0; the GPU tests on the RP6 31/0 on both
  drivers (one skips itself on Qualcomm's); GCC 11 and 13 as the base (four.cpp's maybe-uninitialized warnings at
  -O1-O3, the base's the same). An independent review found no correctness bug (two test gaps filled, a comment
  corrected); a delta review no bug (its two points taken).
- **Left**: the draws at 4x (God of War's scene about five times a vblank, 16 times the pixels; 93 ms a frame on
  Turnip's accurate mode); `transform()`'s arithmetic (exactly, lane by lane, not attempted); the GE's real speed; two
  older GPU renderer differences found writing the tests (a 5650 target's stencil test, clear mode's full 8-bit
  stencil on 5551/4444).
- Scratch: `~/phobos-work/scratch/speed7`; nothing left on the RP6.

## Android: the PlayStation Portable marked Experimental — 2026-10-11

Branch `cursor/psp-experimental-2b67`, on top of `cursor/psp-pgd-states-2b67` (PGD in states, on #202's README). Once the PSP is on master,
the owner releases it as an Experimental core in the nightly builds, before a tagged v2.0; this is how the app and the
nightly's notes say so. Display only: "PlayStation Portable" stays the system's name wherever it's a key (the core,
LaunchSystems, folders such as `firmware/PlayStation Portable/font`, saves, states, settings, PerformanceHud,
TouchLayouts).
- **What**: `ui/ExperimentalSystems.kt` holds `experimentalSystems` (the PSP alone) and `ExperimentalBadge`, a small
  pill reading "Experimental" in the style's pill shape and the theme's tertiary container colors (AA-checked by
  ThemeContrastTest). It sits under the name on the Library's tiles (all five tile styles), after it in Platform
  Visibility, and after the "PlayStation Portable" header in Settings → Emulation (`SettingsCategory` and
  `SectionHeader` take a `badge`). The nightly release notes say the core is experimental, as the README does (#202:
  the Systems table's experimental row and the PSP's own section).
- **Checks**: `:app:compileModernReleaseKotlin`; `:app:testModernDebugUnitTest` 273/0 (the app's unit tests run for
  the debug variants only), with `ExperimentalSystemsTest`: an experimental system must be named as the Library lists
  it (it fails on "Playstation Portable"). The workflow's YAML parsed, its nightly step through `bash -n`. Not on the
  RP6 (busy measuring), so the badge hasn't been seen on a screen yet.
- **Left**: dropping the mark is taking the PSP out of `experimentalSystems`, the sentence out of the nightly's notes,
  and README.md's experimental row and wording. Unmarked on purpose: the system's game list, the running game's card,
  the firmware table's PSP fonts row, the touch layout list and a launch's system choice.

## PSP states: open files keep their PGD decryption (layout 25) — 2026-10-11

Branch `cursor/psp-pgd-states-2b67`, on top of #201's `cursor/psp-measure6-2b67` (`055029692`). Save states only; the
PGD code (pgd.cpp, io.cpp's ioctls and sceNpDrm) is untouched.
- **What**: each open file's PGD fields (e014f4a34's: whether it's set up, its version key, whether its descriptor is
  decrypted, the data's key, size and block size, where the data starts) are saved and loaded. A state saved while a
  game had a PGD file open used to load with that file reading its raw bytes. StateVersion 25; layouts 15 to 24 load
  with no file's PGD decryption set up (each read as it is). Loading refuses a descriptor decrypted with nothing set
  up, and a folder set up as PGD (neither ioctl nor sceNpDrmEdataSetupKey takes one); a descriptor closed
  asynchronously keeps its file's fields (sceIoCloseAsync leaves them), and loads.
- **Checks**: "state fields" changes each of the seven fields on a disc file and round-trips them, refuses the two
  damaged states and loads the two real ones; "older layouts load" makes a layout-24 state, and takes the new bytes
  out of its older states too. Versions with the fields left unsaved, the checks gone, the layout gate at 24, or the
  kernel's default layout left at 24 fail them. tests/psp 428/0 (sanitized; the checks are in existing groups),
  tests/allegrex 58/0, tests/psp/ares 307/0.

## README brought up to date, with the pixel-art logo — 2026-10-11

Branch `cursor/psp-readme-2b67`, at the owner's request before the PSP chain merges into `master`. Docs and the logo
only; no code change.
- **Logo**: `docs/images/phobos-logo.png`, built by `tools/logo/build_banner.py` from the launcher icon's
  `moon-greenscreen.png`: the moon sampled once per art pixel (its grid is 10.2 image pixels, phase 5.3 and 8.6), and
  "phobos" in Avenir Next Bold Italic rasterized onto the same grid in the old logo's purple (six bands, dark outline,
  drop shadow), scaled 4x with nearest neighbour. The README shows it at 502 px wide, 2 screen pixels to the art pixel
  (4 on a 2x display), so it stays crisp. The old logo (`ares/ares/resource/logo@2x.png`) is unchanged: ares's
  resources compile it in.
- **README**: a Download section (channels, the Modern and Legacy flavors, the desktop packages); the systems table
  moved up, with the PlayStation Portable as an experimental row and in the known issues; new sections for the PSP
  core (no firmware, the CPU, the three renderers, discs, sound, saves, compatibility, tests) and the desktop program;
  the app's themes, shaders, Performance Monitor, touch controls, remapping, hotkeys and GPU Driver Manager; CI's
  desktop and PSP test runs; the desktop build's presets; the components list. The existing technical sections are
  kept as they were.

## PSP measure: round 6 — the GE's time, lighting's cosine, the reciprocal's chords, patches — 2026-10-10

Branch `cursor/psp-measure6-2b67`, on top of #200's `cursor/psp-gow-vk-2b67` (`c86461ec6`). docs/psp-core.md, part 66,
says what each test is for and what result would settle what. Only `tools/psp-measure`, `tests/psp/measure.cpp`, the
rebuilt `tests/psp/programs/pspmeasure.elf` and docs; no core change. Clean room: no PPSSPP code read for it, JPCSP's
not read.
- **What**: a new first menu line, "Round 6: the GE's time, lighting, reciprocal, curves (about 7 MB)", 36 tests into
  `results/ge` (`manifest6.txt`): seven timing tests (lists of known work queued alone, every run timed to its finish
  callback, at the clocks found and at 333/166 MHz: commands, primitives of every kind, fills in every state and texture
  format, primitive sizes, block transfers, the texture cache, a SIGNAL); lighting's products in full at four normals
  (round 5's `light-share-*` fit no one share a normal: 15 of 16 normals' intervals are empty), 8 sweeps of 1024 normals
  with 64 products each, lights added up; the reciprocal's chords read off texels at pixels (13- and 14-bit qs, the
  latter through the texture matrix, also measuring that path and q of 0 and below) and at vertices (constant-w cells at
  every chord, and as sprites), and how far the GE keeps a texture coordinate's low bits; PATCH_CULL_ENABLE in 256
  combinations and at the scissor's edges and the planes, and timed off-screen; divisions past 64 along v, one to a
  test, last (four expected to stall, each reset after 5 s); the driver's statuses while a list draws and with its
  FINISH's interrupt held off.
- **Checks**: the measure test drives round 6 in the core: every file, the timing records whole and every run finished,
  and self-checks that the products and chord pictures read what they're meant to in the core's own arithmetic; with a
  PSP's results it prints round 6 against them. tests/psp 425/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0.
  pspmeasure.elf rebuilt in the pinned image, byte for byte reproducible. An independent read-only review found the
  timed primitives "outside the scissor rectangle" drawn on screen (the timed lists' scissor spans 512 columns),
  `recip-pixels` missing 16 qs and `ge-statuses` able to run out of looks, with smaller points: all fixed; a delta
  review found the fixes right.
- **Owner**: `.local/psp-round6/` (EBOOT.PBP SHA-256 `bb59b7ab11e913693f20d851048af62b19a8ffdce2471d082c418ea2cce6ba0b`,
  HOW-TO.md); not on the RP6 yet (it wasn't attached in this session: `~/phobos-work/scratch/measure6/push-rp6.sh`
  pushes it to `/sdcard/Download/PSPMEASURE-ROUND6` and checks the SHA-256 there, under the RP6 lock): run round 6
  (about two minutes), bring `results/ge` back to the RP6's `Download/PSPMEASURE/results`.
- **Left**: fitting it all once the PSP's files are back (part 64's time model, lighting's multiply and cosine, every
  chord, what PATCH_CULL_ENABLE does, v past 64). In the core `recip-vertex-*` read 0 (it clamps texture coordinates at
  65536 texels; `texel-range` will show the PSP's range).
- Scratch: `~/phobos-work/scratch/measure6` (build.sh, the pinned image's build; quick.sh, the measure group alone;
  times.py and topng.py read the files; share*.py, round 5's light-share taken again).

## PSP GPU: God of War in Vulkan without waiting for the GPU; part 63's open risks closed — 2026-10-10

Branch `cursor/psp-gow-vk-2b67`, on top of #199's `cursor/psp-gow-sw-2b67` (#198 under it; measured against #198's
`e9c98f3b9`). docs/psp-core.md, part 65, has the evidence; docs/psp-gpu-renderers.md's "Frame buffers side by side",
"Owned until needed", "Two targets over the same pages", "Block transfers" and "Render to texture" the design.
Software untouched (its pictures byte for byte, every frame of the bench scenes and God of War's the same); every
Vulkan frame of the seven bench scenes, Peace Walker's title and God of War's two scenes byte for byte as before, so
the accuracy is the base's. Clean room: no PPSSPP code copied or translated, JPCSP's not read. Save states unchanged
(23). - **Changes** (each its own commit, with its tests): a PRIM the GPU refuses is drawn at once, not left in the
drawing threads' batch (part 63's risk was real: the new test failed three ways on the base); God of War's frame
buffers side by side are parts of one target, its copies kept for their place and held to what each PRIM takes, pixels
another target drew moved on the GPU as a finish and a fill would leave them, bytes block transfers write over the
GPU's pixels memory's at once, and `drawnOver()` asking each page's owner; the CPU's stores over the GPU's pixels
likewise memory's at once (LCS's list over its picture); a state refused for its fonts says which (Peace Walker's
bench states: the RP6's fonts folder was gone); CI runs the GPU tests on Mesa's lavapipe, and builds again (GCC 13
refused two tests' lambdas). - **RP6, means of two rounds, each runner first in one** (fps, Turnip accurate / fast,
Qualcomm's accurate / fast): God of War's battle 3.97 / 4.51 / 3.71 / 3.67 -> 11.82 / 11.92 / 10.91 / 10.93, its menu
2.08 / 2.30 / 2.02 / 2.05 -> 8.50 / 8.66 / 7.54 / 7.61, finishes 129 and 322 a frame -> 2 in 150 frames; at 4x (the
GPU the bound there) the battle 1.66 / 1.70 / 1.46 / 1.44 -> 5.49 / 9.79 / 7.08 / 6.49 and the menu 0.76 / 1.00 / 0.75
/ 0.81 -> 1.72 / 2.22 / 1.98 / 1.90. LCS's city and woods 5-12% faster (76 finishes in 300 frames -> 2), WipEout
12-36% (600 -> 300); MC3's menu 6% slower on Qualcomm's driver, 1% on Turnip (two moves a frame where part 63 took a
copy); the rest level. - **Checks**: tests/psp 421/0 (sanitized; eight new groups, which broken versions fail, but for
a copy not held to what a tile takes), tests/allegrex 58/0, tests/psp/ares 307/0; CI (GCC 13, lavapipe) 421/0 on the
last commit, its GPU groups 8.8 minutes; the GPU tests on the RP6 pass on Turnip and Qualcomm's driver (30 groups
each); GCC 11 builds the GPU code and tests with -Werror at -O1-O3 (the GE too, but for twelve maybe-uninitialized
warnings in four.cpp, the base's the same). An independent review found one correctness bug (a move left its target's
dead and beside ranges over the pixels it moved in), and a delta review its neighbor (beside bytes a move grows the
drawn rectangle over): both fixed, with a test group that fails without them; the rest of its points taken but one
(frame buffers shown from inside a target aren't shown from the GPU: left). - **Left**: God of War now bound by the
emulation thread at 1x (83-132 ms a frame) and by the GPU at 4x in three of the four configurations (each move a
render pass of ten draws); MC3's menu's two moves a frame; WipEout's one finish a frame (the CPU reads the bloom's
pixels); Killzone's menu at 4x; one rectangle of changes a target; dead and beside ranges 16 a target; GCC 11's
maybe-uninitialized warnings in `four.cpp` (the base's too). Scratch: `~/phobos-work/scratch/gowvk`; on the RP6 only
the fonts folder beside the harness is left, which Peace Walker's scenes need.

## PSP GE: God of War in the software renderer — the GE's time, its bloom batched, its stencil in fours — 2026-10-10

Branch `cursor/psp-gow-sw-2b67`, on top of #198's `cursor/psp-vk-speed6-2b67` (`e9c98f3b9`). docs/psp-core.md, part 64,
has the evidence. Clean room: pspautotests' recordings and Sony's published figures for the GE; nothing of PPSSPP's or
JPCSP's read.
- **Why the menu drew 28 million fours**: God of War's loop never waits for the vertical blank; it waits only in
  sceGeListSync for the last frame's list, flips at once and queues the next whole frame. Our GE took no time, so the
  menu drew its scene 27 times a vblank and the battle 8 (the game times itself by the clock: its speed was right).
- **Changes** (each its own commit, with tests): a list is done for the program only once a PSP's GE would have drawn
  it (its pixels at four a clock, or its primitives at 166/35 clocks each, at the bus's clock: Sony's peaks, so a
  lower bound) and its FINISH comes as an interrupt (not while interrupts are held off, nor ahead of a GE callback),
  as pspautotests' gpu/signals and gpu/ge/intrsuspend recorded; save states version 24, layouts 15-23 loading.
  drawsOver() compares a texture's rows with the pixel rows, so God of War's bloom (levels side by side in one buffer)
  is batched instead of drained; jobs with a deferred texture (in a batch), and with the stencil test, are drawn four
  at a time. Pictures unchanged: the measure harness's 136 pictures byte for byte, every bench frame of commits 2-4
  the same.
- **Review**: an independent read-only review found two crashes (sceGeBreak(0) leaving a FINISH waiting for no list; a
  deferred texture drawn at once four at a time with no copy) and a PAUSE's moved stall address lost; each fixed in
  its commit with a test, with its smaller points (a line owed only its pixels inside the scissor, a list GU ends
  reading as drawing while its FINISH waits, comments). A second review read the fixes.
- **RP6** (fps, pinned as the app pins it; before → after): God of War's battle at 7 threads 4.67 → 8.99, its menu 1.73
  → 22.2; at 1 thread 2.20 → 4.44 and 0.68 → 8.80. The final build: the same frames and end states as commit 4, the
  same speed beside it in a later (slower) session.
- **Checks**: tests/psp 417/0 (sanitized; four new groups), tests/allegrex 58/0, tests/psp/ares 307/0; each commit
  builds and passes its groups; 47 broken versions each fail a test; the GE driver programs 9 of 22 matching (7
  before); a state saved with a FINISH waiting carries on exactly. Library (all 266 beside the base's runner, then the
  final runner beside that): Chains of Olympus no longer times out (its difficulty menu at 3600; 11.1 fps with all of
  it); games show some frame a step or a few frames apart (the runner itself isn't the same from run to run), none
  less than before.
- **Left**: the GE's real speed (a measuring round for the owner's PSP would let the model be the PSP's rather than a
  lower bound); God of War's battle is now its pixels and the emulation thread's setup; logic operations a pixel at a
  time. Scratch: `~/phobos-work/scratch/gow-sw`; nothing of it left on the RP6.

## PSP GPU: Vulkan faster — copies of what changed, a texture from two frame buffers, texels kept — 2026-10-10

Branch `cursor/psp-vk-speed6-2b67`, on top of `3de4a0514` (`cursor/psp-ge-speed5-2b67`, #196 in; PR #197's).
docs/psp-core.md, part 63, has the evidence; docs/psp-gpu-renderers.md's "Owned until needed", "Render to texture" and
"The texture cache" the design. Software untouched (its pictures byte for byte, every frame of the bench scenes the
same); every Vulkan frame of the seven bench scenes byte for byte as before, so the accuracy is the base's. Clean
room: nothing of PPSSPP's read for it, nor JPCSP's. Save states unchanged (23).
- **Changes** (each its own commit, with its tests): a render-to-texture copy taken again copies only the part of the
  target changed since (Killzone's menu blurs a strip right of its picture 1,100 times a frame, each a copy of the
  whole 512x512 picture); a texture running into the frame buffer below is copied from both targets (MC3's menu's
  texture reached one row of the next frame buffer: a finish every frame); a new texture's texels go from the GE's
  copy to the GPU without a copy of their own; a finish puts back only the pages each target drew in (an older bug
  the review found: the CPU's bytes between two PRIMs far apart were overwritten).
- **RP6, means of two rounds, each runner first in one** (fps, Turnip accurate / fast, Qualcomm's accurate / fast):
  Killzone's menu 30.2 / 38.6 / 29.6 / 29.5 -> 48.6 / 73.1 / 42.7 / 42.6, at 4x 4.4 / 4.9 / 4.5 / 4.5 -> 12.9 / 17.0 /
  14.0 / 14.0; MC3's menu 78.6 / 80.9 / 76.0 / 75.2 -> 110.7 / 115.0 / 105.0 / 108.7, at 4x 40.6 / 50.1 / 42.4 / 39.0
  -> 57.1 / 78.9 / 71.2 / 63.6; MC3's race, LCS's city and woods level (-0.4% to +1.3%), WipEout 3% faster with the
  last commit; Lumines' 2D within its noise; God of War unchanged (3.5-4.4).
- **Checks**: tests/psp 413/0 (sanitized; five new groups, which fifteen broken versions each fail), tests/allegrex
  58/0, tests/psp/ares 307/0; the GPU tests on the RP6 pass on Turnip and Qualcomm's driver (22 groups each); GCC 11
  builds the GPU code and tests with -Werror at -O1-O3. An independent review found no correctness bug in the first
  three commits (its test gaps filled, its wording taken; the older bug it reproduced is the last commit), and a
  second review of everything since found none either.
- **Left**: God of War in Vulkan (130-320 finishes a frame from small frame buffers sharing pages and the same memory
  as 5650 and 8888, 2,400 copies and 100 textures a frame); WipEout's two finishes a frame (a depth read, a blur
  spilling into the next buffer); LCS's every fourth frame (a list written over a picture); Killzone's menu at 4x
  (still 1,100 passes a frame); one rectangle of changes a target; no GPU test in CI; the drawing threads' settle
  giving the GPU's busy pages back to the CPU (older, the review's, untested); Peace Walker's states don't load on this
  stack. Scratch: `~/phobos-work/scratch/vkspeed6`; nothing of it left on the RP6.

## PSP GE: the software renderer faster — copies wait only for what they touch, bands listed, runs — 2026-10-10

Branch `cursor/psp-ge-speed5-2b67`, on top of #196's `cursor/psp-hle-games14-2b67` (#195 under it). docs/psp-core.md, part 61, has the
evidence. Nothing drawn changed: every measured picture, and every bench scene's frames and end state at 1 and 7
threads, byte for byte as before. Clean room: nothing of PPSSPP's or JPCSP's read. Save states unchanged (22).
- **Changes** (each its own commit): a triangle's fours in runs of three passes (depth tests, then the survivors'
  texel axes side by side, then their pixels), the chain to the texels no longer holding up each four; each band of
  a batch keeps the list of its jobs, and bands are 4 rows, not 8 (a slow core holds up a wait less); a block
  transfer waits only for the batches drawing over, or decoding a texture from, what it reads or writes (God of War
  streams its textures into VRAM with 1,750-2,250 copies a frame, each of which drew everything waiting).
- **RP6, the emulation thread pinned as the app pins it, medians of three** (fps, 7 threads; 1 thread): MC3 race 49.6
  → 49.2; 21.8 → 22.9 — MC3 menu 63.7 → 65.0; 27.0 → 28.6 — LCS city 75.5 → 76.7; 35.1 → 37.1 — woods 74.3 → 78.1;
  35.1 → 37.1 — Lumines 192.5 → 192.5; 98.1 → 103.2 — God of War's battle 3.67 → 4.42; 2.14 → 2.19 — its menu 1.50
  → 1.65; 0.63 → 0.68. Unpinned at 7 threads LCS gains 18-21%. At 1 thread more than half of part 60's cost is back.
- **Checks**: tests/psp 404/0 (sanitized; new "ge transfers among batches", which seven broken versions of the rule
  each fail), tests/allegrex 58/0, tests/psp/ares 307/0; GCC 11 builds the GE with -Werror and passes every draw3d
  group. An independent review found no correctness problem; its points (a test of a copy not waiting, numbers, the
  Vulkan wording) were taken.
- **Left**: God of War's 32 primitives a frame drawing over their own texture, each a drain; its menu's 28 million
  fours a frame (maybe drawn more than once a frame, the GE taking no time); MC3's race at 7 threads is the emulation
  thread's own work. Scratch: `~/phobos-work/scratch/speed5` (God of War's two scenes, the RP6's scripts rp6-run5.sh
  and rp6-run6.sh, the logging patches); nothing of it left on the RP6.

## PSP core: Sony's plain modules, sceIoGetFdList, GETSIZE's NO_DATA, a restart with all of RAM — 2026-10-10

Branch `cursor/psp-hle-games14-2b67`, on top of #195's `cursor/psp-hle-games13-2b67`. docs/psp-core.md, part 62, has the
evidence; docs/psp-compatibility.md's summary and changed rows are updated. Clean room: uOFW and PPSSPP's savedata
code read only as descriptions of the PSP; JPCSP not read.
- **Fixes** (each its own commit, with tests): a plain module of Sony's whose functions the kernel has none of runs as
  the game's own (scesupPreAcc was Sony's LIBSUPPREACC.PRX, carried by Persona 3 Portable and both Dissidias);
  sceIoGetFdList as uOFW describes it, descriptors in the order opened (Crush); the savedata size mode answers NO_DATA
  for a save not there, as Def Jam's code expects (its first save works; Hot Shots Golf 2 saves instead of calling its
  save corrupted); a program asking, before its first frame, for more than the whole 24 MiB partition is started again
  with all of RAM (Melodie, chosen); the disc's folders are read once (Ghost of Sparta's PGD retry loop no longer
  times out); the runner's WAV header is the standard 44 bytes.
- **Numbers** (against part 59 on the merged stack): menu or gameplay 187 -> 188, movie 71, loading 0, black or hang 6
  (Melodie out, Ghost of Sparta in, black on PGD), timed out 2 -> 1 (Chains of Olympus: the GE's, 68% of the
  emulation thread in GE::flush at block transfers). Ultimate Ghosts 'n Goblins and Me & My Katamari take a fresh
  stick's way now (its "no game data" prompt; its opening). No function missing in any game.
- **Checks**: tests/psp 407/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0, runner-test.sh passes; each commit
  builds and passes its groups alone. Save states version 23 (each open file's place in the opening order); layouts 15
  to 22 load, files in number order; the base runner's version-22 states carry on, Lumines', Patapon 2's and GTA's byte
  for byte, Crush's one frame apart (it calls sceIoGetFdList, refused on the base). An independent read-only review
  found two bugs (the ares suite's hand-made open file without its place; a negative size compared unsigned in
  sceIoGetFdList) and a test gap (the restart's once-only guard); all its findings are fixed.
- **Left / for others**: PGD (six games); Chains of Olympus's speed (the GE, part 61); sceIoChstat (imported by
  LIBSUPPREACC.PRX, never called in the runs); the restart and sceIoGetFdList's standard stream numbers are chosen.
- Scratch: `~/phobos-work/scratch/hle14` (run.sh, lib.sh, survey.sh; sync-dbg.sh builds the debug tree
  `~/phobos-work/wt/dbg-hle14` with apply-hooks.py's traces: PSP_TRACE, PSP_SAVEDATA, PSP_SFO, PSP_PROGRAM,
  PSP_SAVE_MODULES, PSP_BREAK/STEP/WATCH; tools/prxinfo.py and prxdis.py read a plain PRX; lib-new is this branch's
  library run, cmp-new.tsv its comparison with hle13/lib-merged).

## PSP core: the black games with nothing missing — descriptors, the drive, async requests, SAS, the movie ring — 2026-10-09

Branch `cursor/psp-hle-games13-2b67`, on top of #194's `cursor/psp-hle-games12-2b67` (merged in; its PR's base).
docs/psp-core.md, part 59, has the evidence; docs/psp-compatibility.md's changed rows are updated. Clean room: no
PPSSPP or JPCSP source read. The GE's agent worked the same list from the GE's side (#192); nothing here touches
`ares/psp/ge/`.
- **Fixes** (each its own commit, with tests): file descriptors handed out lowest free first, 3 to 63 (Crush: its
  per-descriptor table; Jak and Daxter: The Lost Frontier); sceUmdActivate waits a mount's sector read (Def Jam: Fight
  for NY's file thread ran too late); an asynchronous read's bytes land as it's done (Dead or Alive Paradise's marker);
  sceIoDread's short names laid out by the program's SDK, as io/shortname recorded (Need for Speed: ProStreet's stack,
  and Metal Gear Solid: Portable Ops, Ops Plus and MX vs. ATV: On the Edge, stuck loading); SysClock2USec with no seconds
  pointer gives the whole count (Tekken: Dark Resurrection's frame limiter); a file holds one asynchronous result at a
  time, a request over one not taken refused (Valhalla Knights' seek made again); __sceSasCore waits 300 microseconds
  for its grain (PaRappa the Rapper's top-priority sound thread); sceMpegRingbufferAvailableSize counts in the ring its
  library reads, as ringbuffer/avail's crash and refusal show (Ridge Racer's movie thread holds a copy of its ring).
- **Re-judged**: MACH (menu: its 3600 falls in a fade) and Street Supremacy (movie: a loading screen inside its intro).
- **Numbers** (against part 55's): menu or gameplay 178 -> 186, movie 63 -> 69 (Need for Speed: Carbon - Own the City
  among them, from its notice to its title and, after the run's Cross, its intro), loading 4 -> 0, black or hang 19 ->
  9. The whole library run on both runners: every other game the same scene, or a few frames apart; two logo movies now
  play through. None worse.
- **On the stack** (merged onto #194, parts 57, 58 and 60 under it): the whole library again, the same scenes as this
  branch's own run but #194's three rows (Need for Speed: Most Wanted 5-1-0, both Monster Hunter Portable 3rds) and
  part 60's few pixels in 3D; none worse than on either branch. Menu or gameplay 187, movie 71, loading 0, black or
  hang 6, timed out 2.
- **Checks**: tests/psp 383/0 (sanitized) on the branch, 403/0 on the stack; tests/allegrex 58/0; tests/psp/ares 307/0;
  two independent reviews, their findings fixed (the first fix for Valhalla Knights, seeks done at once, was undone
  after the library run found Patapon 2 and 3 black with it). Save states version 22, after #194's 21 (files numbered
  afresh, a pending read's bytes); layouts 15 to 21 load ("older layouts load" makes a 21 and a 20), and version 20
  states from the base runner carry on (Lumines, Patapon 2, GTA: Liberty City Stories). The branch's own 21 from
  before the merge is neither a 22 nor #194's 21: only test runs made such states.
- **Left / for others**: God of War: Chains of Olympus is slow in the GE (TRANSFER START flushes: the GE's part); Ghost
  of Sparta retries a PGD file (the PGD agent's); Def Jam's first save (WRITEDATASECURE into no save: no recording);
  Crush's sceIoGetFdList (the libraries'); the runner's WAV tags carry a NUL each (the runner's); the 300 µs grain and
  the mount's 1.6 ms are chosen, not measured (pspautotests' new audio/timing probes have no recording yet).
- Scratch: `~/phobos-work/scratch/hle13` (run.sh and lib.sh run the report's way; compare.py and pairs.py compare two
  library runs; statecheck.sh carries a base runner's state on in both runners; quick-tests.sh builds chosen test
  groups; build-commit.py split the commits; mipsdis.py disassembles a memory dump; the debug tree
  `~/phobos-work/wt/dbg-hle13` traces calls by frame).

## PSP core: sceJpeg, scePauth, the PS3's memory and the last missing functions — 2026-10-09

Branch `cursor/psp-hle-games12-2b67`, on top of #189's `cursor/psp-hle-games11-2b67` (its PR targets that branch).
docs/psp-core.md, part 58, has the evidence; docs/psp-compatibility.md's changed rows and tables are updated. Clean
room: no PPSSPP or JPCSP source read.
- **Added** (each its own commit, with tests): the sceJpeg library (a baseline decoder of our own from T.81; every
  function as pspautotests' jpeg tests recorded, six of the eleven exactly, the conversions' sums fitted to their 40
  colours, the Media Engine's 8-byte transfers and its waits); sceMpegAvcConvertToYuv420; scePauth_98B83B5D (type 5 with
  the game's key as a second key: the one way the digest checks out); scePowerCheckWlanCoexistenceClock (uOFW's docs:
  1, a PSP-2000); a program bigger than the user partition given all of RAM (Monster Hunter Portable 3rd HD); sceMt19937
  with the kernel's MT contexts as hash/mt19937ctx recorded (older states' contexts carried on); the sceRtcTickAdd
  family as rtc/arithmetic recorded (and sceRtcGetTick's microseconds past a second, and its years past 9999 refused, as
  rtc/convert recorded); sceHprm's callbacks (as scePower's, the shape Soulcalibur calls them in); the ATRAC low level
  (followed through Corpse Party's CRI library); sceP3da's mix (Sol Trigger).
- **Games**: both Monster Hunter Portable 3rds black to their intro movies, titles and game menus (and, merged with part 57's
  GE fix for a RET with no CALL, the PSP version's whole character creation screen); Corpse Party and Sol Trigger have their
  music; Genso Suikoden, Soulcalibur, Tekken 6, Final Fantasy Type-0, Kingdom Hearts BBS and Dragon Ball Z miss nothing.
  The report's missing functions are down to scesupPreAcc (3 games, none held by it). The base commit's runner and this
  branch's on 35 games: every other frame the same, or moved by the runner's timing under load.
- **Checks**: tests/psp 392/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0 (merged with #193: 396/0, 58/0,
  307/0); twenty-one deliberately broken
  versions each caught; pspautotests' rtc/arithmetic, hash/mt19937ctx and six jpeg programs print what the PSP printed.
  Independent read-only review: fourteen findings, each fixed or answered in part 58 (older MT contexts mid-round, a
  picture's data read only as far as used, the IDCT held to 0-255 before rounding, the Media Engine's misaligned writes
  done at once, a state's JPEG width checked, years past 9999, test and doc claims, sceHprm's sources), the fixes then
  reviewed again. Save states version 21 (layout 21: sceJpeg, sceHprm's callbacks, ATRAC low-level IDs); 15 to 20 load.
- **Left**: scesupPreAcc; P3DA's placing (unknown); whether
  sceMpegAvcConvertToYuv420 widens the movie's sample range (unrecorded); Melodie's 40 MiB request (a PSP-2000 refuses
  it too); PGD; the runner's WAV tags carry a stray NUL (`tools/psp-runner`).
- Scratch: `~/phobos-work/scratch/hle12` (sync-dbg.sh and apply-hooks.py for a traced runner; at/run.sh runs a
  pspautotests program against its recording; quick-tests.sh builds chosen test groups, scratch ones in qtests/;
  mutate.py and mutate2.sh the broken versions; compare.sh/compare.py the two runners' runs; jpegcmp/ the libjpeg-turbo
  check; pauth/ the digest search; cmp1/ and cmp2/ the comparison runs; build-commit.py the commits' split).

## PSP GE: perspective texels by the GE's reciprocal; patch divisions — 2026-10-09

Branch `cursor/psp-ge-fits3-2b67`, on top of #192's `cursor/psp-ge-lists-2b67` (#191 under it). docs/psp-core.md, part
60. 3D triangles divide by a 128-chord reciprocal and step s, t and q as colors: persp-wall 2662→23, persp-divide
3172→211, persp-floor 6026→351, persp-w3 33040→18944, with the older wall and floor pictures; no division at a pixel.
It costs the software renderer on the RP6 3-7% in 3D scenes with 7 drawing threads and 8-10% with one (part 60's
table); a 2D scene is 6% faster. Round 4 again (the owner's `psp-round5b`): #191's reset freed the PSP's GE
every time; PATCH_DIVISION's counts are 7 bits, and past 64 the GE hangs, so the core draws nothing of such a surface.
Lighting, sprites, lines and the near-plane split are unchanged; no measured picture got worse.

## PSP core: the GE's RET with no CALL, and what the GE driver's callers see — 2026-10-09

Branch `cursor/psp-ge-lists-2b67`, on top of #189's `cursor/psp-hle-games11-2b67`. docs/psp-core.md, part 57, has the
evidence; docs/psp-compatibility.md's changed rows are updated. Clean room: no PPSSPP or JPCSP source read.
- **Fixes** (each its own commit, with tests): the GE passes over a RET with no CALL to return from (Need for Speed:
  Most Wanted runs its CALLable frame buffer settings in place; it waited for good in sceGeDrawSync, black, and now
  reaches its title on Software, Vulkan and Vulkan (fast)); from pspautotests' gpu/ge and gpu/signals recordings, the
  driver's answers: sceGeDrawSync(1) no longer counts a list whose finish callback runs, "stalled" is the GE at the
  list's own stall address, sceGeSaveContext saves from a callback that stopped the GE; a list queued twice is BUSY
  for SDK 2.00 and later; programs built with SDKs up to 2.00.10 (or saying none) get the older driver's ways
  (callbacks told no list address, a suspending signal's list paused in its callback, sceGeContinue's -1); a list
  can't be queued at the head ahead of a paused list the GE still has (it would start over: unmeasured, chosen).
- **The report's other black games**: none stuck in the GE. What each showed from the GE's side went to the notes
  shared with the kernel side (`~/phobos-work/scratch/compat-notes.md`), whose parallel branch found and fixed the
  causes: Def Jam (sceUmdActivate taking no time), Crush and Jak and Daxter (descriptors never reused), DOA Paradise
  (an async read's bytes landing early), Tekken DR (clock conversion with no seconds pointer), Valhalla Knights (async
  seeks), PaRappa (__sceSasCore taking no time), ProStreet's crash (sceIoDread's d_private layout). Ridge Racer draws
  a clear a frame after its stub movie (not found). MACH isn't stuck (frame 3600 is a transition; judged again: menu).
- **Numbers** (against part 55): menu or gameplay 178 -> 180, black or hang 19 -> 17. pspautotests' 22 GE driver
  programs: 3 -> 7 printing what the PSP printed; gpu/signals' differing lines 147 -> 23 of 622.
- **Checks**: tests/psp 382/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0; each new test fails without its
  fix. No new saved state (version 20 still). 30 working games (14 of older SDKs) on both runners, Software, and 4 on
  Vulkan: the same pictures but for run-to-run variation each runner shows on its own. An independent read-only
  review; its findings fixed (the NFS row, the head enqueue, claims worded as unmeasured or as kernel-side findings).
- **Left**: the GE's time (statuses a callback sees while a list draws), the state buffer's PSP layout, sceGeGetStack,
  a SIGNAL call's stack in use refused, a third CALL, transfers past VRAM's end; God of War: Chains of Olympus's speed
  (every TRANSFER_START flushes all pending drawing: the kernel side's profile).
- Scratch: `~/phobos-work/scratch/ge-lists` (survey/ the black games' runs, at/ the pspautotests runs before and
  after, regress/ the base and branch runners' frames, ge-hooks.py and dbg-hooks-1.patch for a traced runner).

## PSP measure: a GE stall no longer freezes the owner's PSP — 2026-10-09

Branch `cursor/psp-measure-stall-2b67`, on top of #189's `cursor/psp-hle-games11-2b67`. Only `tools/psp-measure`,
`tests/psp/measure.cpp`, the rebuilt `tests/psp/programs/pspmeasure.elf` and docs; no core change.
- **Why**: in round 4 the owner's PSP froze at `curves-count` and `curves-count-128` (patches cut past 64 times),
  most likely the GE never finishing while `finishList` waited in `sceGeDrawSync(0)` for good; each test cost two
  power-offs.
- **What**: the GE gets 5 s a list (`waitForGe`); a stall is written to `<test>.stalled`, the GE reset
  (`sceGuBreak(GU_BREAK_CANCEL)`, then `sceGuTerm`/`sceGuInit`), the test saved as the GE left it, and the round goes
  on. A test whose reset itself froze the PSP is given up on at the next start. Counts past 64 are a test each
  (`curves-count-65`, `-100`, `-128`, `-200`, `-255`); the earlier program's leftovers for them are cleared once
  (`curves-count.timed`). A new `stall-check` resets the GE on purpose the first time round 4 runs in a results
  folder (docs/psp-core.md, part 48, "Round 4 again").
- **Owner**: `.local/psp-round5b/` (EBOOT.PBP SHA-256
  `2c5c3343cf1e1ecf56eb4deb4fdc4eb5d1dbf1312ae7896d0d90bdcbbbd0a173`, HOW-TO.md): round 4 again, restart, then
  round 5; bring back `results/ge`. Whether the reset frees a GE stuck inside a patch is what that run will show.

## PSP core: character conversion, thread-local storage, lent stacks; PGD holds five black games — 2026-10-09

Branch `cursor/psp-hle-games11-2b67`, on top of #188's `cursor/psp-gpu-fast3-2b67` (#187, #185 and #184 under it). PR #189.
docs/psp-core.md, part 55, has the evidence; docs/psp-compatibility.md's changed rows are updated. Clean room: no
PPSSPP or JPCSP source read.
- **Fixes** (each its own commit, with tests): sceCcc, the character conversions, as ccc/convertstring recorded (The
  King of Fighters - Orochi Saga: black to its autosave notice); thread-local storage pools and
  sceKernelTryLockLwMutex_600, as threads/tls and threads/lwmutex/try600 recorded (God Eater 2: black to its fan
  translation's notice that its DLC file isn't on the stick); sceKernelExtendThreadStack (part 51's) with its review's
  two findings fixed, module_stop's argument kept past a lent stack's end and the trampoline rewritten as states load
  (Dragon Ball Z: Tenkaichi Tag Team: its autosave notice, then PGD).
- **Re-judged**: Persona 3 Portable reaches its title; frame 3600 falls on its attract loop's loading screen.
- **Numbers** (against the report): menu or gameplay 175 -> 178, loading 5 -> 4, black or hang 21 -> 19. The base
  commit's runner and this branch's on 53 games: every other game's frames the same, or an animation step apart.
- **Checks**: tests/psp 373/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0; two independent reviews, their
  findings fixed. Save states version 20 (18 sceCcc, 19 TLS pools, 20 lent stacks); a version 17 state from the base
  runner loads and carries on byte for byte (Lumines, Patapon 2).
- **Left**: PGD decryption holds Black Wolves Saga, Dragon Ball Z, Shining Blade, Valkyria Chronicles III and Naruto
  Shippuden: Ultimate Ninja Impact (AMCTRL's cipher has no allowed description, and its checks need keys not committed:
  an owner's call). scePauth/sceJpeg (both Monster Hunter Portable 3rds; the HD one also needs 26.5 MiB of user memory).
  Need for Speed: Most Wanted's GE RET with no CALL (the GE's part). Def Jam, Ridge Racer, Crush, Tekken DR and the
  rest of the report's black list; ProStreet's crash; scePower 0xa85880d0, scesupPreAcc, sceMt19937.
- Scratch: `~/phobos-work/scratch/hle11` (apply-hooks.py and sync-dbg.sh for a traced runner; at/run.sh runs a
  pspautotests program against its recording; quick-tests.sh builds chosen test groups in seconds; build-commit.py split
  the commits; evidence/ has both runners' frames; survey/ the traces).

## PSP Vulkan pipelines kept between sessions — 2026-10-09

Branch `cursor/psp-gpu-fast3-2b67`, on top of part 53's `cursor/psp-gpu-fast2-2b67`. docs/psp-core.md, part 54;
docs/psp-gpu-renderers.md, the pipelines' paragraph and "Speed" (part 54). Software untouched; no picture changes.
- **What:** System's option "Pipeline Cache" (the app's `psp_vulkan_pipeline_cache.bin` in its Vulkan cache folder,
  beside the N64's; `psp-runner --pipeline-cache FILE`): the driver's pipeline cache read at the renderer's start and
  written when pipelines were made (game end, every 600 frames, renderer let go), behind a header of our own (device,
  driver version and cache UUID, pointer size, size, CRC-32) so damaged or foreign data never reaches the driver; a
  cached renderer failing its check is made again without the file.
- **RP6, second session against first** (driver's ms making pipelines; fps over 450 frames from a state): Turnip
  fast, MC3 race 2,589 → 1.2 ms, 49.2 → 67.1; LCS city 1,074 → 0.6 ms, 85.5 → 103.6; Qualcomm fast, race 2,587 →
  23 ms, 47.8 → 63.9. In the app, MC3's race start: 619/335/226 ms stalls → none over 30 ms.

## PSP renderer "Vulkan (fast)": the GPU transforms and lights 3D — 2026-10-09

Branch `cursor/psp-gpu-fast2-2b67`, on top of part 52's `cursor/psp-gpu-fast-2b67`. docs/psp-core.md, part 53, has
the story and numbers; docs/psp-gpu-renderers.md "Vulkan (fast)" the design, "Accuracy" and "Speed" the tables.
PPSSPP's hardware transform informed the design; none of its code is used (JPCSP's not read). Software stays the
default and exact; Vulkan (accurate) is the path as it was, its pictures unchanged.
- **The choice:** Settings' "PSP Renderer" is "Software (exact)", "Vulkan (accurate)" or "Vulkan (fast)", each
  described; the desktop's menu likewise; the core's "Renderer" takes "Vulkan (fast)"; `psp-runner --renderer
  Vulkan-fast`. The saved 1 is accurate (as Vulkan was), 2 fast. Fallbacks to Software as before, said once.
- **Fast mode** (`ares/psp/ge/gpu/shaders/transform.vert`, `gpu.cpp`'s `meshes()`/`mesh()`, `draw.cpp`'s
  `GE::meshTriangles()`): 3D PRIMs of 16 vertices or more go to the GPU untransformed; the shader skins,
  transforms, lights, fogs and maps them in the GE's arithmetic; the GE still decides which triangles are drawn and
  cuts the near plane's itself; each PRIM's settings a storage-buffer block, so PRIMs drawn alike are one draw; the
  GPU's own blending. Both modes: eight slots, two frames in flight.
- **RP6, medians of three, fps after warm-up** (Turnip accurate / fast; Qualcomm accurate / fast; Software 7
  threads): MC3 race 62.3 / 67.1; 59.1 / 63.2; 53.8 — LCS city 92.3 / 105.1; 87.4 / 96.5; 70.1 — woods 93.8 / 126.5;
  110.8 / 99.5; 71.3 — Peace Walker play 121.5 / 125.0; 114.8 / 113.7; 103.1 — WipEout 39.6 / 79.7; 75.5 / 76.5;
  17.1. Whole runs from a state include the first pipelines (fast makes more): part 54 keeps them on disk.
- **Accuracy** (pixels identical to Software, accurate / fast): MC3 menu 97.0 / 88.0%, race 95.0 / 71.5%, LCS city
  95.4 / 57.8%, Peace Walker play 99.2 / 55.0% (its 16-bit blends a different dither); mostly a level or two;
  nothing missing or misplaced, in the harness or in the app (MC3, both GTAs, Burnout, WipEout's menus, Peace
  Walker, MotorStorm, Ridge Racer 2, all at 60 but MC3's race at 51-53).
- **Left:** a pipeline cache on disk (part 54); bones in a block of their own (Peace Walker's small skinned PRIMs to
  the GPU); Qualcomm's driver's GPU time in fast mode; textures decoded on the GPU; block transfers on the GPU.

## PSP Vulkan renderer faster: its own waste, found with a profile — 2026-10-09

Branch `cursor/psp-gpu-fast-2b67`, on top of #184's `cursor/psp-vk-play-2b67`. docs/psp-core.md, part 52, has the
profile and numbers; docs/psp-gpu-renderers.md the design and the speed table. Original code: nothing of PPSSPP's or
JPCSP's used. Software stays the default and exact; Vulkan (accurate)'s pictures byte for byte as before.
- **Fixes** (`ares/psp/ge/gpu/gpu.cpp`, `draw.cpp`, `texture.cpp`, `system.cpp`): memory told of the GPU's drawing
  once a finish, not once a PRIM (2.7 ms a frame); 2D sprites' exact texture reach for render to texture (Midnight
  Club 3's bloom: 300 finishes in 300 frames → 5); decoded textures kept when their page is written but their bytes
  aren't (25 textures a frame decoded again → none; both renderers); lists submitted at their end only with 32
  commands; "Late Frames" / `--late-frames` to time the GPU as the app presents.
- **RP6, late frames, medians of three** (fps, Turnip / Qualcomm; Software 7 threads): MC3 race 52.6 / 46.9 (53.2),
  menu 68.0 / 64.0 (69.2), LCS city 76.3 / 75.7 (71.8), woods 79.0 / 94.7 (70.9), Peace Walker play 105.8 / 93.3
  (98.1), WipEout 36.2 / 60.9 (16.2). Before: race 34.5 / 33.1, LCS city 63.3 / 59.9, WipEout 18.9 / 21.0.
- **Checks:** tests/psp 345/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0, GPU tests on the RP6 both
  drivers; Software's frames and state identical in all seven scenes at 1 and 7 threads. Independent review: fixes
  taken (release(), lost reports, source copies bounded, ...).
- **Next:** Vulkan (fast), part 53 (the GPU's transform); block transfers on the GPU; a disk pipeline cache.
- Scratch: `~/phobos-work/scratch/gpu-fast` (build.sh, speed.sh, rounds.sh, perf.sh, callers.py, texlog-patch.py,
  warm-patch.py, m1-acc.sh, m1-hashes.sh, quick-tests.sh); RP6 `/data/local/tmp/gpu-fast`. Hold `~/phobos-work/rp6.lock`.

## PSP Vulkan renderer: in play on the RP6, blending without rasterization order, the 8x line — 2026-10-09

Branch `cursor/psp-vk-play-2b67`, on top of #186's `cursor/psp-hle-games10-2b67` (#183, #182 and #180 under it). PR #184. docs/psp-core.md, part
49, has what was seen and measured; docs/psp-gpu-renderers.md the design and the accuracy and speed tables. Original
code: nothing of PPSSPP's or JPCSP's used. Software stays the default; Native stays exact on Turnip.
- **In the app** (Turnip, in order): Midnight Club 3, both GTAs, both Burnouts, WipEout, Peace Walker, MotorStorm and
  Ace Combat played past their menus at Native, LCS at 4x and 10x too: pictures right, no crash; home and back and a
  window resize while presenting kept going. Killzone not started (it saves to the owner's stick); Ridge Racer 2's
  profile loop (kernel; #183 fixes it) left only its menus. Rotation untestable (the RP6 forces landscape).
- **Fixes** (`ares/psp/ge/gpu/`, `ares/psp/system/system.cpp`): without rasterization order, 8888 blending is the
  GPU's again (Midnight Club 3's race 1.3 → 38 fps on the M1, 13 → 32 on Qualcomm's driver), with the PSP's
  whole-number source term (`TERM`); masks, logic, absolute difference, doubled alphas and 16-bit blends read;
  Qualcomm's own driver reads nothing (stale reads in game-sized frame buffers), the start-up check has a game-sized
  case and falls back to reading nothing; no barrier between draws in order; above 1x a 2D triangle's texels held
  inside its corners (Ridge Racer 2's 8x line). Tests: `gpuBlendingApart`, `gpuSeams`, `gpuCheck`. tests/psp
  342/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0; independent review passed with fixes, all taken.
- **Accuracy, RP6** (Software vs Vulkan, pixels identical, Lumines / Peace Walker / MC3 menu / MC3 race / GTA city /
  woods): system driver from adb 73.0 / 40.4 / 88.7 / 67.1 / 52.2 / 49.3%; Turnip 97.8 / 100.0 / 97.5 / 87.8 / 73.4 /
  80.6%.
- **Next**: Vulkan at Native is now behind seven software threads in the 3D games (emulation thread: recording and
  read-back waits; WipEout's 209 submits a frame); Qualcomm's 16-bit blending; sprites' edges above 1x.
- Scratch: `~/phobos-work/scratch/vk-play` (build.sh, m1-acc.sh, rp6-acc.sh, rp6-speed.sh, cmp.py, hal-patch.py,
  blendlog-patch.py, edgelog-patch.py, gpu-tests/), `~/phobos-work/scratch/hw3/rp6` (the app's scripts); RP6
  `/data/local/tmp/vk-play`. Hold `~/phobos-work/rp6.lock` and time only while the app isn't running.

## PSP core: the library run again on the whole stack — three regressions fixed, the common blockers — 2026-10-09

Branch `cursor/psp-hle-games10-2b67`, on top of #183's `cursor/psp-hle-games9-2b67` (its PR targets that branch).
docs/psp-core.md, part 51, has the evidence for each fix; docs/psp-compatibility.md the new report. Clean room: no
PPSSPP or JPCSP source read.
- **The run**: the owner's 266 games, as the report's run went (Software, 7 GE threads, 3600 frames, Start at 120,
  Cross at 1800, PNGs at 60/300/1200/3600), on #183's tip (run 1), on this branch part way (run 2) and at its end
  (run 3); judged by eye, compared row for row with the report (`b27f084a5`), whose runs pressed nothing (the
  presses were broken then), so every worse-looking game was run again without presses on both commits.
- **Regressions**: Mega Man Maverick Hunter X and Juiced: Eliminator, black since scePsmf (#160): a ring fed past
  the movie's end, a frame width of 0, and sound drained past its end. Patapon 2, deadlocked by this branch's own
  memory stick fix: a semaphore's CB wait now runs its callbacks first when it has no timeout and nobody in line.
- **Fixes** (each its own commit, with tests): the memory stick callback told the stick is in; PGD-less files after
  a key; FPL alignment 0; module info of section-less static EBOOTs; the program's start thread as a module's;
  sceUtilityLoadUsbModule; write-data needs a save; the three movie fixes; sceRtc's day of week, 64-bit time and
  clock-set times; sceKernelStopUnloadSelfModule; sceIoAssign; sceImposeSetUMDPopup; sceUsb start/activate;
  sceDisplayIsForeground; sceKernelReferSystemStatus; sceUtilitySetSystemParamInt; the savedata sizes mode without
  names; a PRX program at 0x08804000; the semaphore CB order; rings over 4096 packets; sceMpegAvcDecodeDetail's
  width and height; a game's own library's 44-byte ring; utility modules 0x107/0x108; a movie ended once its whole
  stream is given.
- **Numbers** (against the report, gameplay as menu, hang as black): menu 141 -> 175, movie 35 -> 63, loading
  13 -> 5, black 73 -> 21, timed out 4 -> 2; 81 games further along; 14 stop at a missing function, where 77 did.
  Of the report's 22 menu-to-black games, 18 show something; the 4 still black: Black Wolves Saga (PGD install data),
  The King of Fighters - Orochi Saga (sceCcc), Melodie (40 MiB block), Ridge Racer (black after its movie, cause
  not found).
- **Checks**: tests/psp 362/0 (sanitized), tests/psp/ares 307/0, tests/allegrex 58/0; independent reviews for
  every batch, their findings fixed. Save states still version 17.
- **Left**: sceCcc (pspautotests' ccc/convertstring records it), TLS pools (God Eater 2), scePauth/sceJpeg (MHP3rd),
  scesupPreAcc (Persona 3 Portable), PGD decryption (Black Wolves Saga), a dozen black screens with no missing
  function (Ridge Racer and others, listed in the report), ProStreet's crash. sceKernelExtendThreadStack is written
  and reviewed in the worktree `~/phobos-work/wt/games10-b9` (not in this branch): fix the review's two findings
  first (module_stop's argument when a program unloads itself from a lent call; rewriting the trampoline when an
  older state loads); Dragon Ball Z stays black with it.
- **Vulkan** (MoltenVK, the 11 gameplay games to frame 2400): the same pictures as Software (differences are a frame
  of animation apart), but slow in 3D scenes: Seen in Liberty City 3 fps against 83, Star Wars Battlefront II 27
  against 344, The Sims 2 18 against 158, GTA Sindacco Chronicles under 3 against 72; Mega Man Powered Up faster.
- Scratch: `~/phobos-work/scratch/games10` (batch.py, judge2.py/judge3.py, report3.py, run1/ run2/ run3/ with every
  game's PNGs, analysis/ with the judgements, dbg/ with the traces, vk3/ with the Vulkan frames).

## PSP core: Ridge Racer 2's driver profile — the keyboard's answer — 2026-10-08

Branch `cursor/psp-hle-games9-2b67`, on top of #182's `cursor/psp-ge-fits2-2b67` (#180 under it). PR #183. docs/psp-core.md,
part 50, has the trace, the game's code and the evidence. Clean room: no PPSSPP or JPCSP source read.
- **The cause**: after "Please create your driver profile" and OK, the game opens the system keyboard (sceUtilityOsk*)
  with its default name, "DAMACY", in the field. The kernel answered a field that wasn't empty UNCHANGED (0); Ridge
  Racer 2 takes anything but CHANGED (2) as the player backing out, and went back to its title. No other utility or
  I/O call was involved, and nothing was missing.
- **The fix** (`ares/psp/kernel/utility.cpp`'s `keyboard()`): every field answered CHANGED, its text as before, as a
  PSP is taken to say of a field the player confirms: a player keeping the game's own default name would otherwise
  never get a profile. Killzone and Space Invaders Extreme also act only on CHANGED, Midnight Club 3 on anything but
  CANCELLED; nothing recorded (pspsdk, pspautotests, the wiki) says more. Test: "utility keyboard". No new state:
  version 17 still.
- **Now**, in the runner: the name confirmed, the profile saved (`PSP/SAVEDATA/UCES00422000/DATA.BIN`) and found at
  the next boot (CONTINUE), autosave on, Arcade, and a race, in Software and in Vulkan (MoltenVK) alike.
- **Checks**: tests/psp 340/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0; an independent
  review.
- Scratch: `~/phobos-work/scratch/rr2` (race.script from boot to the race, evidence/ PNGs, trace.patch: the scratch
  build's log of every utility call).

## PSP GE: colors, fog and depth stepped as the PSP steps them; measurement round 5 — 2026-10-08

Branch `cursor/psp-ge-fits2-2b67`, on top of #180 (`cursor/psp-cpu-speed4-2b67`, a4a63b13f). docs/psp-core.md,
part 48, has the rule, the numbers and what's open. Clean room: the owner's measurements only, no PPSSPP or JPCSP
source read.
- **Fitted** (`ares/psp/ge/draw.cpp`'s `stepped()`, `raster.cpp`, `four.cpp`): a triangle's colors, shine, fog and
  depth are stepped from its leftmost corner (topmost of two), a step a sixteenth across and down in 16384ths, rounded
  down, through the reciprocal of twice its area kept to 16 bits. The near plane's cut draws its four corners as
  0, 1, 3 and 1, 2, 3 of its walk (`transform.cpp`): the PSP's split in the one case measured (third corner past).
  Now identical to the PSP: `ramp-colors` (was 1712 pixels apart), `-vertical` (1359), `-3d` (6992), `ramp-fog`
  (240), `3d-floor-fog` (497), `3d-floor-depth` (1388), `3d-clip` (1512), `bezier-flat` (15368); curved patches
  closer (`bezier-curved` 1494 to 1056, `bezier-divide-8` 938 to 721, `spline-edges-0` 787 to 782), the rest in the
  GE's own patch vertices. Nothing worse. Vulkan can't mirror the steps (GPU interpolation); the cut is shared.
- **Still open**: lighting's share isn't 256ths (finer: the GE's own cosine, off either way); perspective texels look
  like a per-pixel divide of limited precision. Lines aren't changed (unmeasured; round 4 has them).
- **Round 5** (`tools/psp-measure`, first menu line, 12 tests, about 3 MB): random triangles' colors and depths, the
  cut with each corner past the plane each way round, lighting's share at 16 normals (192 products each),
  perspective texels with a repeating texture to the texel in thousands. `pspmeasure.elf` rebuilt (same pspdev
  image; README hash); `tests/psp/measure.cpp` drives the new line. #178's never-run probes replaced.
- **For the owner**: `.local/psp-round5/` (EBOOT.PBP, its SHA-256, HOW-TO.md). Round 4 hasn't been run either.
- **Tests**: "draw steps" (new), checks in "draw3d clipping" and "draw3d fog", hand-worked; seven broken versions
  each fail them. tests/psp 341/0 (sanitized), tests/allegrex 58/0, tests/psp/ares 307/0. An independent read-only
  review (two passes) found no bug in the core; its doc, probe and comment findings were fixed.
- Scratch: `~/phobos-work/scratch/ge-fits2` (the dumping core in `inst/`, the fitters in `fit/` and `py/`).

## PSP core: the emulation thread faster again — no drawing for its own textures, batches in a ring — 2026-10-08

Branch `cursor/psp-cpu-speed4-2b67`, on top of #178's `cursor/psp-ge-fits-2b67` (72443bc4d). docs/psp-core.md, part 47,
has the profile, the waits, every number and what's left. Clean room: no PPSSPP or JPCSP source read.
- **The GE** (`ares/psp/ge/`), five commits: deferred (render-to-texture) decodes stay with their batch, their pages
  busy for the CPU till decoded (part 46's could go stale, and ran `memory.watch()` off the emulation thread); a 2D
  sprite's texture rows exactly as its pixels reach them; batches in a ring of eight, the CPU waiting only for the
  drawing it touches; a primitive that can't change any pixel isn't drawn; 2D sprites drawing over their own texture
  drawn from a copy where none reads a texel it has drawn (Midnight Club 3's bloom).
- **Exact**: the six scenes, 300 frames at 1 and 7 GE threads, every frame's picture and VRAM, RAM and the whole state
  identical to 72443bc4d after each commit; RP6 end states match unsettled. tests/psp 335/0 (sanitized; 340/0 with #179's
  desktop tests, merged in after), tests/allegrex 58/0, tests/psp/ares 307/0. Three independent reviews; their findings fixed (one change to a result: a skipped
  primitive's unmapped texel reads, which a state counts, now guarded).
- **Faster** on the RP6 (medians, the app not running): at 7 GE threads Midnight Club 3's race 34.0 to 53.5 fps (the
  emulation thread 26.0 to 18.5 ms a frame), its menu 33.3 to 71.9, GTA's city 55.1 to 78.8, its woods 53.3 to 71.8,
  Peace Walker's title 213.7 to 224.5, Lumines 171.3 to 182.7; at 1 thread the menu 25.6 to 30.5, the race, city and
  woods 1.5-2% faster, Peace Walker and Lumines the same.
- **Next**: at 7 threads the emulation thread is now the CPU (compiled code; VFPU with prefixes interpreted) and the
  GE's setup (project's divisions, lighting, vertex and job copies); at 1 thread the rasterizer, bound by exact
  divisions.
- Scratch: `~/phobos-work/scratch/cpu-speed` (build4.sh, build-android4.sh, rp6-run4.sh, rp6-measure4.sh, rp6-hold.sh,
  ge-inst4-patch.py); RP6 `/data/local/tmp/cpu-bench4`. The RP6 is shared: hold `~/phobos-work/rp6.lock` (a live PID:
  rp6-hold.sh in the background), and time only while the Phobos app isn't running.

## PSP GE: fog amount fixed at each corner (measurement fits) — 2026-10-08

Branch `cursor/psp-ge-fits-2b67`, on `cursor/psp-vfpu-fits-2b67` (#177; #175 and #172 under it). PR #178. Clean room: Phobos measurement data only
(`tests/psp/measurements/ge-round3`, `docs/psp-core.md`); no PPSSPP. Does not touch VFPU / `ares/psp/cpu/` /
`docs/psp-vfpu-measurements.md`.

- **Fitted:** fog is converted to 0–255 at each vertex (`fogAmount`), then that amount is blended across the
  triangle or line as a color channel (software path, SIMD fours, and the Vulkan attribute). Measured on
  `ramp-fog` (18976 → 240 pixels a level apart) and `3d-floor-fog` (6257 → 497). Unit tests `draw3d fog` and
  `draw3d lines` updated to the same arithmetic.
- **Tried and not taken:** stepping perspective u/w and 1/w like 2D's `shortStep` made `3d-wall-texels` worse
  (307 → 2423); left as the edge-weight divide. Color ramps and lighting's last six product cells (level 32 at
  cosine 56/65, level 64 at 72/97) still have no rule the data pin down.
- **Probes (source only):** `tools/psp-measure/ge.c` `round5()` / `geRound(5)` —
  `ramp-color-probes`, `light-product-probes`, `texel-wall-probe`, `texel-floor-probe`. Not on the menu or in the
  committed EBOOT yet (so the host measure test keeps its line numbers).
- **Checks:** `tests/psp` 330/0, `tests/allegrex` 58/0, `tests/psp/ares` 315/0.

## PSP core: the adder's carry (GTA: Liberty City Stories' twitching limbs) — 2026-10-08

On `cursor/psp-vfpu-fits-2b67` (#177), for the stack above to merge up. The owner saw the player's limbs twitch
standing by a car in Portland (RP6, Vulkan). It was #177's `vfpuDot`: when the final round to nearest carried out of
the 24-bit mantissa, the exponent stayed, so a sum just under a power of two came out half of it. GTA's skinning
dots unit vectors with themselves (`vmmul`, some `vcrsp`) and got 1/2 for 1, about eight times a frame standing still.

- **Found:** the psp-runner from a state of the player beside the car, frame-to-frame differences over him: #169,
  #172 and #175 identical and smooth; #177, #178 and #180 with a limb out for a frame 17 times in 80 frames, the same
  in Software, Vulkan (MoltenVK) and with the interpreter; walking, a frame with his torso collapsed. Every result
  the carry changed, logged in the game (1,305 in 160 frames): 1 → 1/2, or −1 → −1/2.
- **Fix:** `vfpuDot` raises the exponent on that carry (interpreter-vfpu.cpp). The recompiler's `vdot` helper
  (recompiler-vfpu.cpp) still summed in doubles, off the PSP on 19% of `vdot-spread`'s quads, so the engines
  disagreed (#177's note had the recompiler calling the interpreter for the VFPU; vdot it didn't): it goes through
  `vfpuDot` now.
- **Measurements:** no measured adder input carries (4,730,624 results), so `compare.sh`'s tables are byte for byte
  the same; `vdot-spread` is exact on both engines (262,144 quads), and `measured.cpp` now checks that file.
- **Tests:** `vfpu edges` (the game's vector to 1; two measured `vdot-spread` quads, which the double sum misses on
  the recompiler) and `vfpu matrices` (`vmmul` to 1) fail without their half of the fix; `measured on a PSP` checks
  all of `vdot-spread` (interpreter). Allegrex 58/0 (sanitizers), PSP suite (ASan) 335/0, tests/psp/ares 307/0.
- **Open:** a measured carry case (a probe in psp-vfpu-measurements.md's Next); `measured.cpp` still leaves out
  `vlog2` from 4 up, which #177 fitted.

## PSP core: VFPU measurement fits (vlog2 above 4 and the adders) — 2026-10-08

Branch `cursor/psp-vfpu-fits-2b67`, on `cursor/psp-gpu-vk-acc-2b67` (#175; #172 under it). PR #177. Does not touch `ares/psp/ge/` or
OpenGL. Co-authored with Cursor; independent review before the commit.

- **`vlog2`:** coefficient cutting by level `d` and truncation to step `2^(d+2)` in units of `2^-24` (linear term
  kept full width until then). Exact on every measured binade above 4, on `[1/2, 2)`, and on the spread-out file.
- **Adders:** `vfpuDot` (2 extra bits, round-to-odd products, align+truncate, nearest-even sum). Used by `vdot`,
  `vhdp`, `vfad`, `vavg` (weights `±1/size`), `vdet`, `vcrsp`, `vqmul`, `vmmul`, `vtfm`, `vhtfm`. Exact on every
  round-2 adder file and on `vdot-spread`.
- **Recorder:** ops.bin 1216/1216; ops3.bin 260/284 (24 open: swizzles past the size in `vcmp` / `vcrs` / `vcrsp` /
  `vdet` / `vsocp` — probes sketched in `tools/psp-measure/vfpu.c`).
- **Docs:** `docs/psp-vfpu-measurements.md` (fits section), Part 3 in `docs/psp-core.md`.
- **Tests:** Allegrex 58/0; PSP suite (ASan) 330/0. Interpreter and recompiler match (recompiler still calls the
  interpreter for VFPU). Independent review PASS (manifest/diff hashes in
  `phobos-work/scratch/psp-vfpu-fits-review/`).
- **Stack:** VFPU only; do not merge to master from here.

## Desktop Library: the PSP's disc titles — 2026-10-08

Branch `local/psp-desktop-disc-info-2`, on top of `cursor/psp-gpu-hw4-2b67` (#179's merge, part 45). The desktop's
Library now shows a PSP disc's own title, as the Android app does (docs/psp-core.md, part 39's consumers).
- **The display** (`desktop/main.cpp`'s `drawLibrary`): the list's line shows the disc's title (its PARAM.SFO's
  TITLE) when there's one, the file's name when there isn't. The file's name stays the game's key: the states,
  the saves and the memory card are keyed by it, as before.
- **The fill** (`desktop/PspDiscInfo.cpp`): the list shows at once; the PSP's disc images' titles fill in after,
  on a thread of their own (a CHD's open takes a while, so the UI isn't held), each from the cache or the shared
  reader (`ares/psp/kernel/disc-info.cpp`), and a rescan supersedes the unfinished scan. No titles fill in while
  a game runs: the thread stops when a game starts and restarts when the Library shows.
- **The cache**: the title, disc ID and icon (its ICON0.PNG's bytes) beside the settings (`dataFolder`'s
  `psp-icons/`), under a SHA-256 of the file's path+size+mtime, as the Android app's `psp-icons/` cache: a
  second visit doesn't re-open the disc. The icon is cached, not drawn: the list's 8x8 font has no place for
  it. A title the font can't draw has its characters dropped; only when none are left is the file's name shown.
- **Tests**: `tests/psp/desktop-disc-info.cpp` (the key, the key's collision, the disc image's file choice, the
  title's choice, the cache's round trip, the title served from the cache without re-opening the disc, and a
  cancelled scan, before it starts and mid-read).
- **Checked**: tests/psp (with the new desktop groups).
- **Left**: the icon's draw (the list's rows are 12 px and the font 8x8: no place for it); the game's window
  title (the launch keeps the file's name).
- **Round 2 review fixes** (2026-10-08): (1) the reader gives up (returns 0) when the scan is superseded, so a
  cancelled disc isn't read to the end or cached; (2) the cache key's parts are joined with `|` separators (the
  `"\0"` was an empty C string, so it appended nothing); (3) the cache files are written to a temporary name and
  renamed over the real one, so a crash mid-write can't leave a truncated title that counts as a hit; a `.info`
  without both lines is a miss; (4) the title thread stops while a game runs and restarts when the Library shows;
  (5) `desktop/PspDiscInfo.*` added to the PSP Core Tests workflow's paths; (6) `listTitle` drops the characters
  the 8x8 font can't draw instead of rejecting the whole title, falling back to the file name only when nothing
  printable is left; the test's scratch folder is removed when the program ends.
- **Round 3 review fixes** (2026-10-08): (1) the `.png` write's rename is guarded by its `open` too (the `.info`
  branch already was), so a temp file that never opened is never renamed over the icon; (2) a direct test that
  the key's `|` separators disambiguate (two files whose path+size+mtime merge into the same string get
  different keys); (3) a test of a scan cancelled mid-read: it gives up at its first sector and caches nothing;
  (4) note, no code change: a title with a newline shows its parts joined (the 8x8 font can't draw the newline,
  so `listTitle` drops it).

## PSP core: blending in the shader — 2026-10-08

Branch `cursor/psp-gpu-hw4-2b67`, on top of part 44's `cursor/psp-gpu-hw3-2b67` (#171 under it). Opened as #172.
docs/psp-core.md, part 45, and docs/psp-gpu-renderers.md's "Shader blending" describe it; PPSSPP stayed a guide only.
- **Shader blending**: draws that blend, use a logic operation or a partial write mask read the frame buffer
  (an input attachment) and do pixel.cpp's arithmetic in `draw.frag`; 16-bit frame buffers keep their own bits.
  A barrier before each reading draw; overlapping primitives kept in one draw where the driver has
  rasterization-order access (Turnip; every pipeline asks), else split into separate draws (M1, Qualcomm's driver).
  A check failing in order is retried split. Native exact, Software the default.
- **Review fix**: the shader's write mask keeps stencil/alpha bits too (was only RGB).
- **Tests**: new `gpuBlending` byte-exact in four formats at 1x and 2x; pspsdk's blend sample 100% (from 68.7%); the
  sanitized PSP suite passes on the M1 (325 groups, Allegrex 58).
- **RP6** (Turnip, in order): Lumines 60 fps at Native, Burnout Legends' menu 60 at Native and 51.4 at 4x. The
  first build failed Turnip's start-up check (only reading pipelines asked for the order, no barrier); fixed. The
  GPU tests built for Android pass on Qualcomm's driver. Settings put back to Software (resolution left at 3x;
  Software ignores it).
- **Next**: Software/7-thread remaining waits and Vulkan accuracy (OpenGL parked; Software covers devices without Vulkan).

## PSP core: upscaling, and presenting without reading back — 2026-10-08

Branch `cursor/psp-gpu-hw3-2b67`, on top of part 46's `cursor/psp-cpu-speed3-2b67` (#176 under it). docs/psp-core.md, part 44, and
docs/psp-gpu-renderers.md's "Upscaling" and "Presenting" describe it; PPSSPP stayed a guide only, none of its code used.
- **The setting**: "PSP Resolution" (Native, the default and exact, then 2x-10x) in the app's PSP settings and the
  desktop's menu, given to the core as its "Resolution" option when a game loads; Vulkan's alone.
- **Upscaling**: targets, depth and stencil at N times; uploads drawn larger, read-backs for the CPU and GE shrunk to
  the PSP's size first so memory stays exact; render to texture keeps the scale. New test `gpuScaled`; the sanitized
  PSP suite passes on the M1 (322 groups, the GPU groups run).
- **Presenting** (Android): the frame drawn straight onto a swapchain on the app's window, no read-back; screenshots
  from a shrunk copy; a failing surface falls back to the read-back. The desktop reads back at up to 4x. Review fix:
  when present fails (background, acquire timeout), the frame goes the read-back way instead of leaving a stale screen.
- **RP6**: Lumines 59.4/59.3/50.6/39.2 fps at Native/4x/8x/9x (9x GPU ~74% busy); Ridge Racer 2's menu 60/46.5 at
  Native/8x. The fallback toast seen at last (`adb shell setprop debug.phobos.psp.failcheck 1`; set back to 0).
  Backgrounding then returning kept presenting (Lumines ~30 fps after). 10x is in the menu but its row sits under
  the app's nav bar, so it wasn't selected from adb. Still to check: 10x in play, 3D past menus, rotation, the 8x line.
- **Next**: shader blending (#172); OpenGL parked.

## PSP core: render-to-texture decode later (emulation thread) — 2026-10-08

Branch `cursor/psp-cpu-speed3-2b67`, on top of part 43's `cursor/psp-cpu-speed2-2b67` (with #165 disc-info).
docs/psp-core.md, part 46. PR #176.
- **The GE** (`ares/psp/ge/texture.cpp`, `threads.cpp`, `draw.cpp`): a texture whose bytes a batch still draws over
  (render to texture) is not decoded on the GE's thread. The source batch is launched if it's being filled; the
  Look waits in the next batch (`deferRows`, CLUT snapshot when indexed); `ensureDecoded` runs as that batch
  starts drawing. Texture cache shared with workers (`textures.mutex`).
- **Exact**: tests/allegrex 58 groups, tests/psp 328 groups (0 failures), including `geThreads` RTT. Six cpu-speed
  scenes remade at StateVersion 17; 300 frames `HASHES=1` at 1 and 7 GE threads identical to parent
  (`eeba5028e`). RP6 race/menu end states match unsettled too.
- **Faster** on the RP6 at 7 GE threads (four rounds): MC3's race about 37 → 39 fps (medians 37.3 → 38.1);
  menu about 36 either way. Emulation thread on the race still ~24 ms.
- **Next**: primitives drawn at once / textures read from memory as drawn.
- Scratch: `~/phobos-work/scratch/cpu-speed` (v17 scenes); RP6 `/data/local/tmp/cpu-bench3`; take
  `~/phobos-work/rp6.lock`.

## PSP core: the emulation thread faster again — drawing off it, registers held — 2026-10-08

Branch `cursor/psp-cpu-speed2-2b67`, on top of part 42's `cursor/psp-hle-games8-2b67` (#169; measured from part 38's
4ebc59d97). docs/psp-core.md, part 43, has the profile, the waits and every number. Opened as #170.
- **The GE** (`ares/psp/ge/`): a list starting waits only for the batch it's to fill, not for everything the last
  list left being drawn (b0af46594); lighting's light-by-material products once a primitive (3292c775f).
- **The recompiler** (`ares/psp/cpu/`): a block holds the game's registers in host registers, written through, so
  `ipu.r[]` is always current (5a5fc5d0c); lwl, lwr, swl and swr natively (e06caa086).
- **Exact**: tests/allegrex (58 groups, on ARM64 and as an x86-64 build under Rosetta) and tests/psp (319, the
  hardware renderer's included) pass. The six scenes, 300 frames at 1 and 7 GE threads: every frame's picture and
  VRAM, RAM and the whole serialized state identical to 4ebc59d97's; on the RP6 the end states match unsettled too.
- **Faster** on the RP6 at 7 GE threads: MC3's race 24.2 to 32.3 fps (the emulation thread 34.4 to 26.8 ms a
  frame), its menu 30.4 to 36.2, Lumines 137 to 177; GTA's city and woods unchanged; Peace Walker's title 226 to 216.
  At 1 thread the CPU's time is 2-9% lower (Peace Walker's the same) and the frame rates the same.
- **Next**: addressed in part 46 (RTT decode deferred). Then primitives drawn at once, textures read from memory
  as they're drawn.
- Scratch tools and states are in `~/phobos-work/scratch/cpu-speed` (never committed); the RP6 copy is
  `/data/local/tmp/cpu-bench3`. The RP6 is shared: take `~/phobos-work/rp6.lock` first, and time only while the
  Phobos app isn't running.

## PSP core: Killzone's movie, the dialogs' sizes and statuses — 2026-10-08

Branch `cursor/psp-hle-games8-2b67`, on top of `cursor/psp-gpu-hw2-2b67` (#168, the Vulkan renderer in Phobos).
docs/psp-core.md, part 42, describes it; one commit per fix, each with its tests.
- **Killzone: Liberation**: its jump into VRAM at frame 1547 came from its movie being told no picture came at the
  first decode, which made it clear its target through a null pointer. With a game's own sceMpeg library (stood in
  for) each access unit now gives its picture at once; the firmware's library still holds the first back
  (video/mpeg/basic). Killzone plays its intro and reaches its main menu.
- **Dialogs**: each kind takes only its recorded parameter sizes; InitStart only while no dialog is current;
  Update INVALID_STATUS outside running and finished; sceUtilityMsgDialogAbort finishes after 8 Updates. Save
  state version 16.
- **Equal corner colors losing a level**: kept exact in a trial, psp measure didn't move, so left as it is.
- **scePower 0xa85880d0**: no game here calls it; nothing names it. Left for a game that does.
- **Checks**: tests/psp 321 groups with and without sanitizers, tests/psp/ares 306 checks, none failing; the ten
  priority games the same sound and screens against the Killzone commit.
- **Left**: the 21 menu-to-black games (not yet copied over); scePower 0xa85880d0; the web browser's own state and
  memory, and the Screenshot and NpSignin dialogs.

## PSP core: the Vulkan renderer in the app and the desktop — 2026-10-07

Branch `cursor/psp-gpu-hw2-2b67`, on top of part 38's `cursor/psp-cpu-speed-2b67`. docs/psp-core.md, part 41, and
docs/psp-gpu-renderers.md's "In Phobos (part 41)" describe it; PPSSPP stayed a guide only, none of its code used.
- **The setting**: "PSP Renderer" (Software, the default, or Vulkan) in the app's PSP settings and the desktop's
  menu, given to the core as its "Renderer" option when a game loads. The hosts hand over the `vkGetInstanceProcAddr`
  their `loadVulkan` got (the system's or a custom driver's on Android, MoltenVK on the macOS desktop).
- **Fallbacks**: a start-up check (sprites byte for byte, triangles within a bound) in a machine of its own; a
  renderer that fails it, or loses its device later, gives way to the software renderer, said once in the log and
  the UI. The shown frame is read straight from its target (no finish), memory's picture the fallback.
- **Bytes beside the pixels**: busy VRAM pages asked about to the byte, so a display list beside the frame buffer
  doesn't wait for the GPU (Brave Story on the RP6 18.6 to 59.5 fps). A new test; the PSP system tests pass on the
  M1 (sanitized, the GPU groups run).
- **Measured in the app** (RP6): Midnight Club 3's title 54.3 fps with the installed Turnip, 49.6 with Qualcomm's,
  35.6 software; its race 39.0, 36.0, 29.0; the 2D scenes at 60. Twenty-two games on Vulkan: none crash; three
  black as with software (the core's). Not seen: the app's fallback toast (nothing failed).
- **Next**: upscaling (presenting without reading back), then accuracy (shader blending), then OpenGL.
- `tools/psp-runner` builds again (Vulkan's headers) and takes `--renderer`.

## PSP core: the emulation thread faster, the CPU's recompiler first — 2026-10-07

Branch `cursor/psp-cpu-speed-2b67`, on top of `cursor/psp-hle-games7-2b67` (part 37; measured from part 33's
70b239ac6). docs/psp-core.md, part 38, has the profiles, the scenes and every number.
- **The recompiler** (`ares/psp/cpu/`), six commits: the FPU natively (`recompiler-fpu.cpp`, new); multiply,
  divide, trapping sums and bit fields; blocks chaining to the next by themselves, then to successors known at
  compile time; loads and stores with the page tables in saved registers; the VFPU's common instructions with the
  prefixes at rest (`recompiler-vfpu.cpp`, new). Every block still starts and ends where it did (the run loop's
  check points decide when interrupts and kernel events land), and anything unusual (NaN results, FCSR not at its
  default, division by 0 or -1, overflow, VFPU prefixes set) goes to the interpreter as before.
- **Tried and reverted**: GE vertices' matrices four at a time (b75298198, reverted by d4b896d4f). Exact without
  skinning, but no faster on the host or the RP6.
- **Exact**: tests/allegrex (58 groups, 2,000 generated programs and the new "chained runs") and tests/psp (308)
  pass. Six scenes (Midnight Club 3's menu and race, GTA LCS's city and woods, Peace Walker's title, Lumines' demo),
  300 frames at 1 and 7 GE threads: every frame's picture and VRAM, RAM and the whole serialized state identical
  to the base's.
- **Faster**: the CPU's own time a frame down 30-45% in the 3D scenes (MC3's race 12.8 to 8.4 ms on the RP6). On the
  RP6 at 7 GE threads, MC3's race 21.6 to 25.2 fps, its menu 28.4 to 31.8, GTA's city 43.6 to 57.2; at 1 thread
  19.7 to 21.6, 24.3 to 25.9 and 36.4 to 39.7.
- **Next**: the GE's setup is now most of the emulation thread (lighting's per-light numbers once a primitive, the
  emulation thread's share of drawing at settles); in the recompiler, MIPS registers kept in host registers within a
  block, and lwl/lwr/swl/swr natively.
- Scratch tools, states and memory sticks are in `~/phobos-work/scratch/cpu-speed` on the Mac (never committed);
  the RP6 copy is `/data/local/tmp/cpu-bench2`.

## PSP core: the stuck games, and the runner's presses — 2026-10-07

Branch `cursor/psp-hle-games7-2b67`, on top of `cursor/psp-gpu-hw-2b67` (#164, the hardware renderer, part 36).
docs/psp-core.md, part 37, describes it; one commit per fix, each with its tests.
- **Killzone: Liberation**: its boot program loads and starts the game's module, then unloads itself
  (sceKernelStopUnloadSelfModuleWithStatus), which the kernel took as the program leaving. Now the program goes as a
  module would (`programAsModule()`), its module_stop run, the game's threads running on; code in no module gets
  CAN_NOT_STOP. It reaches its language menu and, with presses, its autosave notice; after that it loads
  `mpeg.prx` and jumps into VRAM at frame 1547 (not looked into yet).
- **Ace Combat: Joint Assault**: utility module 0x308 loads (0x309 refused); the gamedata install dialog's five
  functions. It reaches its autosave notice, and with presses its legal notice.
- **MotorStorm: Arctic Edge**: the drive notifies its callback on activate and deactivate (umd/callbacks, umd/wait,
  umd/register); the save state version is 15. It reaches its warnings, its logos and its intro movie.
- **Chili Con Carnage**: corners at one depth keep that depth at every pixel (65535 had become 65534 at some, and
  the logo lost stripes under GEQUAL). The logo draws whole.
- **Points past z / w ±1** aren't drawn: psp measure's 3d-rules 6 to 4 pixels apart, nothing else changed.
- **The runner**: `--press` lists were never split at their commas, so no run with one (the compatibility
  report's 266 included) had any input; and "program: ended" was lost on unload. Both fixed.
- **The 22 menu-to-black games**: not the unload change (it's older than both runs; Killzone was black at
  de6e6dcb7 too). With no input in either run, a re-run with the fixed runner is the way to sort them.
- **Checks**: tests/psp 312 groups with and without sanitizers, tests/psp/ares 302 checks, none failing; the ten
  priority games the same pictures and sound against the base, but two frames a moment apart.
- **Left**: Killzone's jump into VRAM; the other dialogs' sizes and the Screenshot and NpSignin dialogs; equal
  corner colors losing a level; scePower 0xa85880d0, the report's one top missing function still missing.

## PSP core: a hardware renderer on Vulkan — 2026-10-07

Branch `cursor/psp-gpu-hw-2b67`, on top of `cursor/psp-ge-curves-2b67` (#162). docs/psp-core.md, part 36, and
docs/psp-gpu-renderers.md (rewritten) describe it. The owner stopped the exact compute renderer (parts 34-35, on
their own branches) for a PPSSPP-style hardware renderer; PPSSPP's GPU backends informed the design, none of their
code is used.
- **The renderer** (`ares/psp/ge/gpu`): the GE transforms, lights, clips and decodes textures as before and hands
  each PRIM's settings and primitives to `GE::Renderer`; `gpu.cpp` draws them with the GPU's rasterizer, texture
  units and blending into frame buffers kept on the GPU (read back only when their pages are needed), with render to
  texture by copies on the GPU, the GE's decoded textures cached, and a pipeline per mix of settings (a fragment
  shader specialized by 15 constants). `vulkan.cpp` runs it through the host's `vkGetInstanceProcAddr` (custom
  drivers included; its own tables, volk untouched). Not in the app or the desktop program yet.
- **Measured** (the six verified scenes, the RP6's Adreno 740): 1.5-2.3 times the seven-thread software renderer
  (Midnight Club 3's race 37.5 fps against 24.2, Liberty City Stories' woods 94.4 against 54.5). Not exact: 28-65% of
  pixels identical to the software renderer's, nearly all the rest a level or two apart (blending's rounding). 2D
  sprites and render to texture are byte for byte, as `tests/psp/gpu.cpp` checks.
- **Next**: upscaling and presenting from the GPU, then OpenGL, then the app wiring (Settings' "PSP Renderer:
  Software / Vulkan / OpenGL", Software the default; the host's `vkGetInstanceProcAddr` and `loadVulkan` for the PSP;
  a sanity check; measuring in the app with the system and a custom driver, none installed or deleted without asking).
- **After review** (part 36's "After review"): builds with GCC (two conditionals' types); a PRIM the renderer can't
  take (past VRAM's end, no target, a lost GPU, a texture read from memory) is drawn by the software renderer instead
  of lost; render-to-texture copies bounded (32, one for each target and size); the depth buffer's pages watched
  apart from the colors; long PRIMs handed over in pieces; `GE::setRenderer()` has a renderer forget the machine
  before. Three new tests; the PSP system tests pass on the M1 (sanitized, the GPU groups run) and with g++ 13.
## PSP disc info: title, disc ID, region and icon — 2026-10-08

Branch `local/psp-disc-info`, on top of `cursor/psp-ge-curves-2b67` (#162). docs/psp-core.md, part 39, describes it.
- **The reader** (`ares/psp/kernel/disc-info.hpp` / `.cpp`): a PSP disc's title, disc ID (NPD ID), region and
  icon (PSP_GAME/ICON0.PNG), read off its image (ISO, CSO, ZSO, DAX, JSO, or CHD) a few sectors at a time, every
  size bounded (PARAM.SFO over 64 KiB, ICON0 over 1 MiB, taken as none). Region from the disc ID's third letter:
  J Japan, U US, E Europe, else Unknown. CHD read through libchdr when `ARES_ENABLE_CHD` is defined; without it,
  a CHD is taken as none. `sfoValue` and `paramSFO` moved here from `psp-runner` and `mia`, which now share them.
- **Consumers**: `psp-runner`'s `discInfo()` and `mia`'s `load()` call the shared reader; the Android app's
  `PhobosCore.pspDiscInfo(fd)` (JNI) reads title+icon together, and the Library's `MainViewModel` caches the
  icon keyed by URI+size+mtime, showing title and icon in `SystemDetailScreen`.
- **Tests**: `tests/psp/disc-info.cpp` (ISO, CSO, CHD, damaged images, SFO values, regions) and
  `PspDiscInfoTest.kt` (title choice, cache key).
- **Checked**: tests/psp (with the new disc-info groups), tests/psp/ares, Android unit tests and release build
  (`-Pphobos.ffmpeg=OFF`).
- **Round 2 review fixes** (2026-10-08): (1) the disc's descriptor is closed by the caller (`pfd.fd`, not
  `detachFd()`); (2) the icon is downsampled to 160 px at cache time (refused past 512 px), so the list's
  decode is small and safe; (3) the plain list shows first, then titles and icons fill in a separate job, cached
  under a SHA-256 of URI+size+mtime in `cacheDir/psp-icons/` (the title and disc ID beside the icon), so a
  second visit doesn't re-open the disc; the previous Library scan (list and title fill) is cancelled; (4) the SFO's offsets are 64-bit,
  the title capped at 128 bytes with controls dropped, and passed as bytes into Kotlin's UTF-8; (5) size/mtime
  captured only for PSP discs; (6) a single-disc .m3u shows the playlist's name; (7) a CHD disc-info case in
  `tests/psp/ares/system.cpp`; (8) the CATEGORY isn't read (the Library shows the title and icon, not the
  category).
- **Left**: cost measurement on 20+ real images (to be done on the handheld); RP6 fd-count check (no device
  access on the host). The desktop's Library shows the same titles (the entry above).

## PSP core: curved surfaces (BEZIER and SPLINE) — 2026-10-07

Branch `cursor/psp-ge-curves-2b67`, on top of `cursor/psp-hle-games6-2b67` (#161, part 32; parts 31 and 30, #160
and #159, underneath). docs/psp-core.md, part 33, describes it.
- **The GE** (`ares/psp/ge/curves.cpp`): BEZIER and SPLINE drawn, the control points read as PRIM reads vertices;
  Bézier patches of 4x4 points (3N + 1 a way, the rest left out), cubic B-splines with open or closed ends (SPLINE's
  bits 16-19), PATCH_DIVISION per patch or piece (0 as 1), PATCH_PRIMITIVE's strips, line strips and points;
  positions, colors (rounded up), texture coordinates (made up from 0 to 1 when the vertex type has none), normals
  (from the slopes when it has none, out of PATCH_FACING's front) and weights blended. The vertices then go through
  `draw.cpp`'s new `drawVertices()`, PRIM's own path (transform, lighting, culling, jobs, threads, four pixels at a
  time), which PRIM now uses unchanged. More than 131072 vertices aren't drawn (noted). No state change (version 13).
- **Against the PSP**: pspautotests' gpu/primitives/bezier and spline pictures covered pixel for pixel (points, flat
  shading's colors, lines, texels exact; smooth gradients within a level); round 3's Bézier patches the same pixels
  as the owner's PSP, every channel within a level (bezier-curved 1494 pixels apart, from 42624 undrawn), the
  closed spline but 4 edge pixels. gpu/exact/curves: 2 of 16 checksums (its knife-edge points and depths need the
  GE's own fixed-point arithmetic, which nothing tried matched). Found and left: the PSP drops points past z / w ±1
  even with DEPTH_CLIP_ENABLE on (round 2's 3d-rules, and a third checksum), which the core draws; changing it would
  change more than curves.
- **The measuring program's round 4** gained fourteen curve cases (vertices' places to the sixteenth, colors, depths,
  texels, made-up texture coordinates and normals, culling, counts): built in pspdev's Docker image,
  `tests/psp/programs/pspmeasure.elf` replaced (README checksum); the PSP's EBOOT is outside the repository at
  `/tmp/psp-measure-round4-curves/PSP/GAME/PSPMEASURE/EBOOT.PBP` (SHA-256 bc6e90b4...e69343f2).
- **Checked**: tests/psp 297 groups (6 new) with ASan and UBSan, none failing, "psp measure" identical but for round
  3's curve files; tests/psp/ares 294 checks; 7 broken versions each caught. Games: none of the 22 here (the
  handheld's 22) drew a curve in 7200 frames (a probe build); Macross Ace Frontier and its sequels aren't here.
  Scenes without curves (4 scenes from states, 1 and 7 threads, the builds taking turns): every frame's picture the
  same; speed within the shared Mac's noise (part 33's table).
- **Left**: the GE's exact arithmetic (round 4 records it), PATCH_CULL_ENABLE, the made-up normals' direction and
  texture coordinates (chosen), points past z / w ±1, morphing and skinning on control points.
- **After review** (independent, clean room and bounds confirmed, builds reproduced): round 4's strip joins are
  `curves-joins`, run before the counts, and the counts past 64 cuts run last, split three ways (`curves-count-128`,
  `-200`, `-255`), so a GE stall there loses only its own test (34 files, about 8 MB; ELF and EBOOT rebuilt, the
  SHA-256 above). `GE::patch()` checks its vertex budget before allocating anything, and gives back
  `patchVertices`' room past 4096 vertices once drawn. Checks as above, none failing.

## PSP core: the next functions games stop at (run status, ID lists, LoadExec, the memory stick) — 2026-10-07

Branch `cursor/psp-hle-games6-2b67`, on top of `cursor/psp-psmf-2b67` (part 31): commit 17ea2d8a0 (code and tests),
then the docs (9b580b37c), then the re-run's list (code, tests and docs together), the PSMF branch merged in, then
the review's fixes (#161).
docs/psp-core.md, part 32, describes it. Sources: pspsdk's headers, pspautotests' programs and the results they
recorded on a PSP, uOFW's export lists, the games' own calls; no other emulator's code read.
The owner's re-run of the compatibility report hadn't landed at first; every function the old report names was here.
- **ThreadManForUser 0xffc36a14, sceKernelReferThreadRunStatus**: the 44-byte run status (status, priority, wait,
  wakeups) with real run figures, which threads now keep: time on the CPU, interruptions by calls into the program,
  preemptions by better threads, releases from waits. The full thread status gives them too.
- **0x94416130 sceKernelGetThreadmanIdList, 0x57cf62dd sceKernelGetThreadmanIdType**: every kind the kernel has
  objects of (threads, semaphores, event flags, mailboxes, both pools, pipes, callbacks, alarms, virtual timers,
  both mutexes; threads by state), the refusals as threads/threads/threadmanidlist and threadmanidtype recorded.
- **LoadExecForUser's sceKernelLoadExec**: WipEout's next stop (its launcher starts each of its games as a program
  of its own). The program named starts in the caller's place on a cleared machine, with the argument given, and the
  call never returns, as modules/loadexec/loader recorded. KDebugForKernel's Kprintf, which that test prints with,
  is sceKernelPrintf's.
- **The memory stick's room**: Chili Con Carnage's and Ace Combat X's sizes request (the free space alone) was told
  there was no save, the kernel looking for one by the request's empty save name; the save measured is msData's, as
  utility/savedata/sizes recorded, and with none the answer is 0. The size mode (22) now answers. One stick told
  everywhere (the capacity devctl, both modes): 2 GB with 1,792 MiB free, its bytes within 31 bits (games add sizes
  up in 32-bit words; pspautotests' PSP had 16 GiB free, which would wrap there).
- **Part 28's recorded differences**: the event flag's and semaphore's status size words (52, 56; every status now
  copied as far as its size word says); their creates (NULL names, attributes, any semaphore counts); the lightweight
  mutex's status (both functions, 64 bytes; the work area's holder 0 while free); the thread status's size by SDK
  (104, or 108 refusing larger after 2.60) and its exit status (DORMANT, then NOT_DORMANT);
  sceDisplayAdjustAccumulatedHcount (31 bits, from 0x7fffffff to 0). State version 14.
- **Checked**: tests/psp 296 groups (7 new) with ASan and UBSan and without, tests/psp/ares 298 checks, none failed;
  12 broken versions each caught. pspautotests: of threads/ and intr/'s 167 recorded programs, 72 printed exactly
  what the PSP printed before and 83 after; elsewhere 75 and 76 of 192; none worse; modules/loadexec/loader as
  recorded. Games on this Mac: Ace Combat X from "Checking Memory Stick" for good to its title and attract movie;
  Chili Con Carnage from "Not enough free space" to its profile menu; WipEout lists its threads and, through its
  menus, starts WipEout Pure to its title. The 14 others the same pictures (frames 600 to 3000) and sound, Snoopy vs.
  the Red Baron's screens the same at moments apart (its two NULL-named semaphores now refused, as recorded).
- **On the RP6** (build 105300): Chili Con Carnage to its main menu with a new profile, Ace Combat X to the first
  operation's briefing, WipEout Pure started from the collection to its menu's opening, all at 60 fps.
- **The re-run's list** (PR #158, `local/psp-rerun`: 77 of 266 games stopping at a missing function, from 149; its
  state column unreliable): sceDisplayWaitVblankStartMulti and its CB form (0x40f1469c, 0x77ed8b3a), as
  display/vblankmulti recorded; sceKernelCheckThreadStack (0xd13bde95), as threads/threads/stackfree recorded;
  sceRtcGetAccumulativeTime (0x011f03c1, and Sony's spelling 0x029ca3b3); sceHprmIsHeadphoneExist (0x7e69eda4) and
  the rest of sceHprm but its callbacks (nothing plugged in); sceNpDrmSetLicenseeKey (0xa1336091) and the user DRM
  library's other four, with no DRM (keys taken, a game's files read as they are, their size their own). Names from
  candidate names' NIDs (pspsdk, uOFW). scePower 0xa85880d0 left out: no name, no recording, no game here calls it.
  The report's per-game table names no functions, so nothing else could be counted across three games. Checked:
  tests/psp 299 groups (3 new, one extended) with ASan and UBSan and without, tests/psp/ares 298 checks, none
  failed, 6 broken versions caught; vblankmulti and stackfree now exactly as the PSP printed; the 14 other games
  and the three above the same pictures and sound (Snoopy's frame 600 and Chili's sound vary run to run anyway).
  Five games of the device's not here before: Joint Assault quits at utility module 0x308, Killzone's boot program
  unloading itself ends the game it started, MotorStorm waits on blanks, Ape Escape and Ridge Racer 2 run on.
- **Left**: threads' attributes as recorded (0x800000ff added), where a preempted thread goes back in line, the
  blank's timing (display/hcount's lowest and highest lines), thread-local storage pools, the lightweight mutex's
  other recorded differences, PARAM.SFO in saves and the list mode's pattern, Chili Con Carnage's logo and menu art
  drawn in broken stripes (a GE matter), the imported-but-uncalled functions part 32 lists, scePower 0xa85880d0,
  a program unloading itself while a module it started runs on (Killzone), utility module 0x308 (Joint Assault).
- **After review** (with #160's PSMF fixes and #159's faster GE merged in; the audit found the clean room held and
  LoadExec sound): the size mode's needed and overwrite figures are now the shortfall past the free space (0 for
  any save that fits; their text written only then), not the files' whole size; sceKernelSignalSema's limit check
  adds in 64 bits (0x7fffffff onto a count of 1 had wrapped past it); a thread's time is no longer counted twice
  when an interrupt's handler ends the game (a state saved after then failed to load). State version 14 still.
  Checked: tests/psp 302 groups with the sanitizers and without, tests/psp/ares 298 checks, none failed;
  each fix undone, caught.

## PSP core: movies through scePsmf and scePsmfPlayer, in a clean room — 2026-10-07

Branch `cursor/psp-psmf-2b67`, on top of `cursor/psp-ge-features-2b67` (#157, part 29): commit 18c58f968 (code and
tests), then the docs; local only, not pushed. docs/psp-core.md, part 31, describes it. Made in a clean room the
owner approved: a separate agent read other emulators and wrote a facts-only specification (from their behaviour,
pspautotests' recordings and the movies' headers); this was written from that, from pspautotests' video/psmfplayer
programs and recordings directly, the movies' own bytes and the games' behaviour, the specification's sources never
seen and no emulator's code read.
- **scePsmf** (`kernel/psmf.cpp`, 25 functions): the header, stream table and EP map through the game's 32-byte
  structure (copies work on their own). Peace Walker's four (SetPsmf, GetNumberOfSpecificStreams, the start and end
  times) are there.
- **scePsmfPlayer** (`kernel/psmfplayer.cpp`, 28 functions): one player, its statuses and every refusal the
  recordings show, the movie read through a game file descriptor and decoded with the existing decoders, start-up on
  the third GetVideoData, a picture due two Updates after the last, sound kept in step, the end on the second Update
  after the last picture (or at the caller's next wait when the player's priority is worse), looping, slow motion,
  pause, step frame, fast forward and rewind, pictures with zero alpha (sceMpeg's stays opaque: no recording shows
  it). A new wait kind, `Wait::Psmf`. State version 13.
- **Modules**: no new rule. Peace Walker's psmf.prx and libpsmfplayer.prx are `scePsmf_library` and
  `scePsmfP_library` (stood in for, as Sony's); the player's other names (libpsmfplayer, psmf_jk, jkPsmfP_library)
  load as game code, but the kernel's functions win the linking, so their games reach the kernel's player too.
- **Checked**: pspautotests' video/psmfplayer, 19 of 22 exactly as the PSP printed (basic, configplayer and playmode
  differ only in the PSP's own addresses, its USB-paced file reading and its data cache; part 31 says how);
  tests/psp 289 groups (8 new) with ASan and UBSan, with FFmpeg and `PSP_FFMPEG=0`; tests/psp/ares 294 checks both
  ways; none failed; 11 broken versions each caught. Games: all 14 on this Mac the same pictures at frames 1200,
  2400 and 3000 and the same sound before and after; Peace Walker now plays its opening movie (all 2368 pictures,
  with sound) after pressing Cross on its title, where it had given it up at the missing scePsmfSetPsmf. None of the
  14 uses scePsmfPlayer; the report's 8 player games aren't on this Mac.
- **On the RP6** (build 105200): Peace Walker plays its opening movie by itself after the Kant quote, through the
  stood-in psmf.prx, at 60 fps with its sound running; Chili Con Carnage (not on this Mac) loads psmf.prx too and
  now plays its opening movie and the Eidos logo to a Memory Stick warning (black in the compatibility run). Ace
  Combat X and WipEout stop at ThreadManForUser 0xffc36a14 and 0x94416130; no game tried loads the player's library.
- **After review** (617d34e48; the audit's findings in the owner's words, its report unread): an EP map's size in 64
  bits and the map kept inside the header (a wrapped count had copied host memory and walked 429 million entries);
  an access unit cut at 2 MiB and its end searched for once (it had grown without limit, searched quadratically),
  and sound frames without headers passed over once; an end before the start taken as the start. New group "psmf
  malformed movies"; tests/psp 290 groups, tests/psp/ares 294, none failed; pspautotests and Peace Walker unchanged.
- **Left**: the player meeting a game (the report's 8 player games are neither here nor among the device's games
  tried), LocoRoco 2's GetPsmfInfo variant, PCM sound, marks, the unknown NID 0x340c12cb's real behaviour, the
  start-up's and loops' real timing, and the choices on the specification's uncertainties (part 31 lists them),
  which a measuring round with a movie that has sound and an EP map would settle.

## PSP compatibility re-run — 2026-10-07

Branch `local/psp-rerun`, cut from `origin/cursor/psp-ge-features-2b67` (tip
`b27f084a5`, PR #157). Re-runs all 266 PSP games against the three PRs since
the old run—#153 (music/movies through FFmpeg's decoders), #156 (41 kernel
functions, missing 149→77), and #157 (GE line/box drawing, DXT; no curved
surfaces, anti-aliased lines drawn without anti-aliasing)—and updates
`docs/psp-compatibility.md`.

- **Result:** 66 improved (black → menu), 22 went from menu to black (why
  isn't checked yet; the report names them),
  35 shifted to movie (dark intro frames the old rule lumped into black,
  so the Before column's 155 "black" overstates the old run's true-black
  total). Menu: 104 → 141. Movie: 35 (new category). Black: 155 → 73.
  Loading: 4 → 13. Timeout: 3 → 4 (The Legend of Nayuta added).
- **Missing functions:** 149/266 (56 %) → 77/266 (29 %). The GE functions that
  dominated the old top list (`sceGe_user`, `ThreadManForUser`) are gone; the
  new top gaps are `scePsmf`/`scePsmfPlayer` (video playback, 79 hits) and
  `scePspNpDrm_user` (DRM, 13 hits).
- **Speed:** 172 of 266 games now run above 200 fps (was 166). 8 very slow
  (< 30 fps), down from 13.
- **Report:** `docs/psp-compatibility.md` updated with the new commit,
  counts, per-game table, top missing functions, and a "Since de6e6dcb7"
  section.
- **No changes to `ares/psp`** beyond what the tool strictly needs.

Checks: `tests/psp/run-tests.sh` (281 groups, 0 failures),
`tests/psp/ares/run-tests.sh` (290 checks, 0 failed),
`tests/psp/ares/runner-test.sh` (passing). Builds clean
(`-Wall -Wextra -Werror`).

## PSP runner and compatibility report — 2026-10-06

Branch `local/psp-runner`, cut from `origin/cursor/psp-test-data-2b67` (tip
`de6e6dcb7`). Adds `tools/psp-runner/` (a headless PSP game runner) and
`docs/psp-compatibility.md` (the compatibility report for 266 games).

- **Runner** (`tools/psp-runner/runner.cpp`): mirrors `tests/psp/ares/system.cpp`'s
  `TestPlatform` (pak, video, input, audio callbacks) with a CLI, PNG/WAV
  writers, and an exit summary. `discInfo()` reads the NPID and title from the
  SFO's `DISC_ID` and `TITLE` fields via `sfoValue()`. `build.sh` reuses the
  build recipe from `tests/psp/ares/run-tests.sh` (same objects: `psp.cpp`,
  `android_globals.cpp`, `ares-runtime.cpp`, `nall`, sljit, libchdr, zlib,
  libco).
- **Test** (`tests/psp/ares/runner-test.sh`): runs `cube.elf` for 60 frames,
  checks the summary and PNGs. Passing.
- **Batch**: all 266 games ran through the runner with
  `--frames 3600 --press "120:Start,123:Start!,1800:Cross,1803:Cross!"
  --png-at 60,300,1200,3600 --wav dir/sound.wav --ge-threads 2 --fonts
  <fonts>`. 3 timeouts (Ys - The Oath in Felghana, God of War - Ghost of
  Sparta, God of War - Chains of Olympus). PNGs are named by the frame run
  (the runner waits for the screen to present a new picture after each
  frame that wants one); 100 games' final frame is `frame-001200.png`
  (the screen hadn't presented by frame 3600).
- **Report** (`docs/psp-compatibility.md`): per-game table (title, NPID,
  fps, state, last note), summary counts, top missing functions, timed-out
  games, and next missing functions. 149 of 266 games (56 %) hit at least one
  unimplemented function. Top gaps: `ThreadManForUser` (130+ games),
  `sceCtrl a7144800` (32), `sceGe_user b77905ea` (17), GE feature emulation
  (29 games: DXT textures, bounding-box tests, line drawing).
- **No changes to `ares/psp`** beyond what the tool strictly needs.

Checks: `tests/psp/run-tests.sh` (236 groups, 0 failures),
`tests/psp/ares/run-tests.sh` (278 checks, 0 failed),
`tests/psp/ares/runner-test.sh` (passing). Builds clean
(`-Wall -Wextra -Werror`).

**Active work (2026-09-25):** branch `feature/n64-accuracy-neutral-perf-2026-09`
([PR #4](https://github.com/pwnedbygary/phobos/pull/4), commits `c02166932` and `ffe337ceb`).
Stacked on it: branch `feature/perf-hud-2026-09` ([PR #5](https://github.com/pwnedbygary/phobos/pull/5))
with the MangoHud-style performance HUD (builds, 50 host tests pass, verified on the RP6; see
plan tasks 42–44/69). Stacked on that: branch `feature/emu-sched-2026-09` pins the emulation
thread to the fastest CPU core, re-applied every frame because Android resets thread
affinity (Settings → Emulation, default on). Mario vs Boo 59.4 / 59.5 FPS mean, worst second
~54, versus 58.3 / 58.6 and ~44 with it off
([audit](performance-audit.md#2026-09-25-follow-up-emulation-thread-on-the-fastest-core)).
Stacked on that: branch `feature/rsp-pipeline-copy-2026-09` reverts PR #4's RSP pipeline
hash skip, which a profile showed costing 27% of the emulation thread. Mario vs Boo then
held 59.9 / 59.9 FPS with worst second 59.6 and ~90% fewer frames over 20 ms
([audit](performance-audit.md#2026-09-25-follow-up-rsp-pipeline-copy-restored-regression-fix)).
Stacked on that: branch `feature/n64-rsp-dispatch-2026-09` makes ares' `Screen` handoff a
mailbox, so the emulation thread no longer waits for vsync (fast-forward was capped at 60 on
every system; now 90–134 FPS in Mario Tennis), plus RSP dispatch trims and the pause menu's
duplicate Reset button removed
([audit](performance-audit.md#2026-09-26-follow-up-frame-handoff-no-longer-waits-for-the-display)).
Stacked on that: branch `feature/ui-theme-2026-09` ([PR #9](https://github.com/pwnedbygary/phobos/pull/9))
adds the theme system: 37 IDE-colorway themes in Settings → Appearance and an optional Retrowave
effects toggle; system cards, HUD and touch controls unchanged (see the plan's UI theme row).
Stacked on that: branch `feature/n64-taken-links-2026-09` ([PR #10](https://github.com/pwnedbygary/phobos/pull/10))
keeps loops inside the CPU JIT block: a taken branch back into the same block no longer returns
to the dispatcher (10.1 → 0.6 million round trips a second in Mario vs Boo; fast-forward
89.2 → 95.4 FPS), with CP0 Count and PC identical to the previous build after a state load
([audit](performance-audit.md#2026-09-26-follow-up-loops-stay-in-the-jit-block)).
Stacked on that: branch `feature/n64-b-on-y-card-glow-2026-09` ([PR #11](https://github.com/pwnedbygary/phobos/pull/11))
moves N64 B to the face button up and to the left of A (Android `BUTTON_X`: the physical Y button
on the RP6 in its Xbox layout mode), so the pad matches an N64 controller, with the on-screen B
following; and the library's console tiles get the Retrowave neon edge.
Stacked on that: branch `feature/n64-ff-idle-skip-2026-09` ([PR #12](https://github.com/pwnedbygary/phobos/pull/12))
adds an exact idle-loop skip that runs only while fast-forwarding (uncapped fast-forward
103 → ~111 FPS; timing identical over 1,200 frames). At normal speed it stays off because the
RP6's power management answered the lighter load with a slower core or clock
([audit](performance-audit.md#2026-09-27-follow-up-exact-idle-loop-skip-while-fast-forwarding-the-fast-core-cant-be-forced)).
Stacked on that: branch `feature/busy-wait-pacing-2026-09` ([PR #13](https://github.com/pwnedbygary/phobos/pull/13))
makes the debug load intent reliably open the game screen, and adds an opt-in Settings →
Emulation → Keep the fast core busy (N64) that spins between frames instead of sleeping, which
keeps the RP6's emulation thread on CPU 7 at full clock
([audit](performance-audit.md#2026-09-27-follow-up-optional-busy-wait-between-n64-frames)).
On Turnip, Granite now uses binary fences instead of timeline semaphores, so GPU submits no
longer wait behind the driver's fence waits (Mario vs Boo's rally dips: worst second ~52 → 60 FPS;
[audit](performance-audit.md#2026-09-27-follow-up-gpu-submits-on-turnip-no-longer-wait-for-the-gpu)).
Stacked on that: branch `feature/n64-vi-field-rate-2026-09` ([PR #14](https://github.com/pwnedbygary/phobos/pull/14))
paces the N64 at the field rate its VI registers produce (59.826 Hz for progressive modes rather
than a fixed 59.94) and adds dynamic audio rate control for all systems, so the audio ring no
longer fills until it drops samples
([audit](performance-audit.md#2026-09-27-follow-up-n64-paced-at-the-vis-field-rate-dynamic-audio-rate-control)).
Stacked on that: branch `feature/rsp-vu-neon-2026-09` ([PR #15](https://github.com/pwnedbygary/phobos/pull/15))
emits the RSP multiply and multiply-accumulate instructions as inline NEON from the RSP JIT
(`ares/n64/rsp/vu-neon.hpp`), checked bit for bit against the SSE and scalar implementations by
`tests/rsp-vu-neon/run-tests.sh` on an AArch64 host; 3.0% less per-frame work in Mario vs Boo
([audit](performance-audit.md#2026-09-27-follow-up-rsp-multiply-accumulate-instructions-as-inline-neon)).
PRs #4–#15 were merged into `master` on 2026-09-27 (fast-forward, same commits); new work
branches from `master`. Branch `feature/rsp-acc-cache-2026-09`
([PR #16](https://github.com/pwnedbygary/phobos/pull/16)) keeps the RSP accumulator in NEON
registers across consecutive multiply-accumulate instructions, 2.3% less per-frame work
([audit](performance-audit.md#2026-09-27-follow-up-rsp-accumulator-kept-in-neon-registers)).
Branch `feature/rdp-inflight-depth-2026-09` ([PR #17](https://github.com/pwnedbygary/phobos/pull/17))
lets parallel-RDP keep 256 render contexts in flight instead of 32 (73 MiB more GPU memory), which
removes nearly all of Mario Tennis's shot-showcase stutter in Standard mode with Async RDP and
busy-wait on (frames over 20 ms per showcase 11–16 → 0–2)
([audit](performance-audit.md#2026-09-27-follow-up-parallel-rdp-keeps-up-to-256-render-contexts-in-flight)).
Branch `feature/busy-wait-wfe-2026-09` ([PR #18](https://github.com/pwnedbygary/phobos/pull/18))
makes Keep the fast core busy (N64) wait in `WFE` instead of spinning: the same placement and clock,
about half the power (RP6 on battery 4.76 → 4.15 W; sleeping 3.58 W)
([audit](performance-audit.md#2026-09-28-follow-up-the-busy-wait-waits-in-wfe)).
Branch `cursor/library-theme-cards-c6f7`, based on PR #18's commit, themes the Library at the
user's request: its tiles and the system page's directory and ROM cards use the Settings cards'
surface (`ThemedCard` in `ui/Components.kt`: no shadow, and a tile's ripple stays inside its
corners), and the console illustrations are recolored from the active theme instead of keeping
their original colors (`ui/theme/ConsoleArtPalette.kt`, loaded through `ui/ConsoleArtFetcher.kt`),
replacing Task 19's earlier rule to preserve the card art. Builds; 67 host tests pass, including a
check of every illustration in every theme. Not yet checked on the RP6.
Stacked on that: branch `feature/glass-ui-2026-09` gives the app a glass look at the user's request.
Cards and grouped rows on every settings-style screen, the Library and the log console are
translucent panels over soft color glows (`ui/theme/Glass.kt` computes the values,
`ui/theme/GlassPanel.kt` draws them), and the bottom navigation is a floating glass dock. The
console art now takes the glass tile's color as its anchor, so dark consoles stay darker than it.
The glows take each accent's hue at about the background's luminance, so they add color without
making the backdrop brighter or darker. Panel and glow strengths are searched once per theme from
its final colors (under 2 ms per theme on the Mac) so that text keeps 4.5:1 over the worst backdrop:
panels with body text sit at 0.60 alpha in every theme, and Retrowave panels need 0.69–0.98 over the
sun. The glows and panels are static and cached per size, nothing blurs the backdrop (no
RenderEffect; only the panel shadow is a blurred rounded rectangle), and the backdrop is hidden
behind the running game like the sunset. Builds; 73 host tests pass, including composited-contrast
checks for every theme and for Material You schemes from 24 seed hues. Checked on the RP6.
Screen titles and section headers are drawn straight on the backdrop, and where they passed over the
Retrowave sun they fell below 4.5:1 in every theme (primary text over the primary-colored sun was
about 1:1); PR #23 fixes that with a soft plate behind them. A Glass
effects setting in Settings → Appearance (Full, the default; Subtle; Off) is carried in the theme's
glass values, so every glass surface follows it and it applies at once. Subtle keeps the structure
with glows at about half strength, half the gloss, shade and rim, and panels at 0.80 alpha in every
theme without Retrowave. Off gives opaque cards and a solid dock with the faint outline; Retrowave
keeps its sunset and neon edges, with opaque panels. The contrast tests run every check at all three
levels. Pages run under the floating dock to the bottom of the screen and pad their lists' ends by its
height (`LocalDockInset`), so content scrolls behind and around it. Because anything can pass under
the dock, its opacity is searched against any color: 0.81–1.0 (0.97–0.98 in the median theme), so its
labels keep 4.5:1 and the selected icon 3:1 on a full-strength pill.
For depth, each panel adds a tight contact shadow under its soft shadow and a soft bevel inside its
edge, light along the top and dark along the bottom; both stay within the 8 dp band along the edge
that text keeps clear of, and scale with the level (half at Subtle, none at Off). The app now draws
edge to edge on every Android version (`WindowCompat.setDecorFitsSystemWindows(window, false)`), as
Android 15 enforces, and the navigation bar is transparent from Android 10, so pages also show
behind the gesture handle; Android adds its own scrim behind three-button navigation.
Branch `feature/fullscreen-ui-2026-09` ([PR #21](https://github.com/pwnedbygary/phobos/pull/21)),
stacked on PR #20, makes Settings → Video → Full Screen Mode also hide the status bar in the menus (a
swipe down shows it for a moment), since the clock and icons are what the setting promises to hide.
Games hide both bars as before, and leaving a game now restores the menus' state (the navigation bar,
and the status bar only with the setting off) instead of showing every bar. The gesture handle stays
in the menus so going home remains one swipe.
Branch `feature/glass-refraction-2026-09` ([PR #22](https://github.com/pwnedbygary/phobos/pull/22)),
stacked on PR #21, upgrades Compose (BOM 2024.06.00 → 2024.12.01: Compose UI 1.7.6, Material 3
1.3.1) for its graphics layers. At the Full level on Android 13 and later, the dock draws the pages
and backdrop behind it through a frosted lens (`ui/theme/Refraction.kt`): a 4 dp blur, and within the
8 dp band along its edge the scene bends inward by up to 6 dp with a slight color fringe while the tint
thins to 0.3; the labels keep out of that band, so they still sit over the contrast-safe tint. Only the
dock gets the lens: on every tile it rendered offscreen each frame, and scrolling the Library fell to a
34 ms median frame (61% janky), against 12 ms with the dock alone and 9 ms without a lens (about 3.5%
janky either way). Nothing is captured during games, at Subtle or Off, or before Android 13. If the
device can't compile the shader, the dock falls back to its plain tint.
Branch `feature/cleanup-2026-09` ([PR #23](https://github.com/pwnedbygary/phobos/pull/23)) puts
Retrowave headers drawn on the sunset (screen titles, section headers), and the notes and empty
states drawn straight on the backdrop (`BackdropText`), on a soft plate of the background, at the
least opacity that keeps them at 4.5:1 over the sun (0.67–0.93 by theme; a host test checks every
theme). It moves Keep the fast core busy to N64 Experimental → Frame pacing (in Settings and in
the pause menu), and
updates the plan: Task 61 (in-place upgrades work, but the committed release key is public),
Task 69 (superseded by the overlay), the PS1, Genesis VDP and Saturn rows (wanted eventually), a
list of candidate N64 Experimental performance options, and two planned features: the performance
monitor's settings in the pause menu plus resizing it on screen by pinching or dragging handles on
its corners and sides, with the text wrapping and scaling to fit; and controller and hotkey
settings in the pause menu, per console and per game (Tasks 13b and 13d).
A scripted sweep on the RP6 (2026-09-28) checked the three glass levels and theme changes, the
plates, Rogue Squadron's mode-change hold, the pause menu's quick actions, the status bar after
leaving a game with Full Screen Mode on and off, and the touch layout editor from Settings; every
setting it changed was put back. Its open findings (no confirmation on Reset All Layouts or on a
save state's Delete, a status bar flash at a cold game launch, the navigation handle over the quit
dialog, a likely false driver-update notice, launch intents dropped while Phobos runs) are in the
plan's device sweep and frontend launch rows.
Branch `fix/sweep-findings-2026-09` ([PR #24](https://github.com/pwnedbygary/phobos/pull/24)) fixes
all but the last. Reset All Layouts and a save state's Delete ask first, and Delete is disabled on an
empty slot. The stored settings are read once before the first frame (bounded at 500 ms), so a cold
launch straight into a game no longer shows the status bar for about 1.2 s over the Initializing
screen, and the first frame already has the saved theme. Every dialog hides the same system bars as
the screen under it (`DialogSystemBars`), so the quit dialog no longer brings the navigation handle
back over a full-screen game. The driver update check only counts releases in the installed
driver's line: StevenMXZ's repo publishes Turnip, Turnip Gen8 and Qualcomm lines side by side, and
Gen8 V36 had been offered as an update to Turnip v26.3.0-R5. Checked on the RP6, the cold launch
frame by frame from screen recordings (`.local/device/cold-rec.sh` and `frames.swift`); the driver
check has host tests.
Branch `feature/frontend-launch-2026-09` ([PR #25](https://github.com/pwnedbygary/phobos/pull/25))
lets frontends start games (setup in [frontends.md](frontends.md)). `MainActivity` is
`singleTask`; `launch/ExternalLaunch.kt` reads the intent and opens the file, through a folder
Phobos holds when no grant came with it, and `launch/LaunchSystems.kt` (plain Kotlin, host tests)
works out the system. The game the native core holds now lives in `CoreSession` in
`MainViewModel.kt`, because a clear-task launch replaces the activity, and with it the ViewModel,
while a game runs: loads and unloads take its lock and only the owning instance unloads. Game
screens can overlap while one game replaces another, so `EmulatorScreen` checks
`emulatorScreenReplaced` before touching shared state, and the core's surface is released only
by its owner (`PhobosCore.attachSurface`/`drawsTo`). Checked on the RP6 with `adb` standing in
for the frontends. Argosy needs an entry in its hardcoded registry: submitted as
[rommapp/argosy-launcher#471](https://github.com/rommapp/argosy-launcher/pull/471) from the
user's fork (branch `add-phobos-emulator`) after v1.1.0, a hardware test with Argosy's debug build
on the RP6 and the user's `takt` review (APPROVE), and awaiting the maintainers. Greptile's only
note is that its test checks 8 of the 27 platforms; a new commit there needs a fresh `takt`
summary in the PR body, which the user runs.
Branch `feature/app-updater-2026-09` ([PR #26](https://github.com/pwnedbygary/phobos/pull/26)) adds
the in-app updater the user asked for (Settings → About). Builds are numbered from git history in
`android/app/build.gradle.kts` (version code 100000 + commit count, version name from
`git describe`), so a local install is never a downgrade from a release; v1.1.0 was the last build
numbered by the old tag mapping. CI stages `update.json` next to the APKs
(`.github/scripts/stage-apks.sh`) for tag releases and for a `nightly-<code>` pre-release on every
push to `master`; `util/AppUpdates.kt` picks the newest build for the installed flavor, checks the
download and installs it through `PackageInstaller`, with Android's confirmation.
Branch `docs/new-systems-2026-09` records the user's direction for new systems (2026-09-29):
Phobos as a better RetroArch over time, retro systems only, without the PlayStation 2 / GameCube
generation. [new-systems.md](new-systems.md) lists the candidates and routes: PPSSPP for the PSP
and Flycast for the Dreamcast (both named by the user) through a libretro host, Mednafen's Saturn
emulation, the systems the ares tree has but Phobos doesn't list (32X, Super Game Boy, Arcade,
LaserActive, Pocket Challenge V2), melonDS for the DS (suggested), arcade boards, smaller consoles
and computers. The user decided both open questions on 2026-09-29: the app moves to
GPL-3.0-or-later when the first GPL core lands, and PPSSPP ships as upstream does, with the PSP's
decryption keys in its source (an exception to the rule against publishing keys, for PPSSPP only).
Branch `docs/perf-scan-2026-09`, stacked on it, records a read-only performance scan of every core
([audit](performance-audit.md#2026-09-29-scan-of-every-core-accuracy-preserving)): ranked,
accuracy-preserving opportunities (headroom per core first, then a PS1 recompiler, ZX key polling,
the 32X's SH-2 recompiler, cothread switches, 68000/ARM7TDMI dispatch, the PC Engine VDC, the GBA
timers) and four places where Phobos follows ares's speed-leaning defaults (SNES scanline PPU,
SNES coprocessor sync, PC Engine PSG output rate, GBA/WonderSwan pixel accuracy), which only the
user can change.
Branch `feature/n64-save-transfer-2026-09` implements Task 49: the pause menu's Save Data section
(N64) imports battery saves from Mupen64Plus, RetroArch or a Phobos backup and exports them in
those formats. The conversions are plain Kotlin with host tests (`util/N64SaveTransfer.kt`); the
byte orders were measured on the user's own saves (local copies in the git-ignored
`.local/ref/n64-save-samples/`, with `FINDINGS.md`). An import runs between the unload and the
reload of the running game (`startLoad`'s `beforeLoad` hook), after the unload has written the
current save, and moves the files it replaces plus the auto-save state to `Backups/<date>` beside
the save. Native `getSaveFiles` and `flushSaves` (`PhobosRunner.cpp`) list the game's saves and write
them, the Controller Pak's included, to disk. Checked on the RP6 (2026-09-29): a Mupen64Plus `.fla`
import (the save showed in Paper Mario's file select), a RetroArch export into RetroArch's
`saves/n64` (byte-identical FlashRAM), the replace prompt, and a `.srm` import that moved the
auto-save state into the backup folder.
Branch `feature/new-logo-2026-09` replaces the logo with the design the user chose from thirteen
concepts: a pixel-art Phobos (the real moon's shape, with a D-pad crater) in front of Mars. The
launcher icon is adaptive (background and foreground layers; no text and no monochrome layer, both
the user's choices), and the in-app logo is the round icon. `tools/logo/build_icon.py` rebuilds every
asset from the two source images beside it (it needs Pillow).
Branch `feature/pixel-ui-2026-09` ([PR #31](https://github.com/pwnedbygary/phobos/pull/31)) adds a
Pixel art style to Settings → Appearance, where the Retrowave switch became a Style effects setting
(`UiEffects`: None, Retrowave, Pixel art). Its pieces are in `ui/theme/Pixel.kt` (the fonts, type
scale, `PixelShape` and shapes, panels, plates, and the switch, slider and progress bar) and
`ui/theme/PixelScene.kt` (the backdrop, plain Kotlin with host tests). Material's buttons, switch and
slider don't take their shapes from the theme, so button call sites pass `pillShape()`, and dialogs,
menus and progress bars go through `PhobosAlertDialog`, `PhobosDropdownMenu` and `ProgressBar` in
`ui/Components.kt`. New ones should do the same, or they keep Material's look under the solid styles
below.
Branch `feature/crt-style-2026-09` adds a CRT terminal style and puts the style effects behind one
interface, `UiStyle` in `ui/theme/UiStyle.kt`. `UiEffects.style` maps each setting to one: `GlassUi`
for None and Retrowave, whose glass and neon code is unchanged, and a `SolidUi` object for each solid
style (`PixelUi` in `Pixel.kt`, `CrtUi` in `Crt.kt`). A `SolidUi` supplies the type scale, the shapes
and pill shape, the backdrop, the plates behind text on the backdrop, panels, dialog and menu edges,
the dock's shapes and indicator, header options (capitals, a title cursor, a section rule), and its
own switch, slider and progress bar. The shared components (`Components.kt`, `PhobosScaffold.kt`,
`MainScaffold.kt`) ask `LocalPhobosTheme.current.solid` for these. A style draws in
`MaterialTheme.colorScheme`, which cross-fades for 450 ms after a theme change, rather than
`PhobosThemeInfo.scheme`, the final colors, so its borders fade with their fills; only the pixel
scene, which is rendered again whenever its colors change, takes the final colors. A new style is a
`UiEffects` entry, a `SolidUi` object in its own file and a contrast test built on
`StyleTestSupport.kt`, which has every theme's palettes, Material You schemes from 24 seed hues and
the WCAG helpers.
Branch `feature/rpg-style-2026-09`, stacked on the CRT branch, adds the 16-bit RPG style (`RpgUi` in
`ui/theme/Rpg.kt`). Its font, `res/font/dotgothic16.ttf`, is DotGothic16 from google/fonts cut down
with fontTools: `pyftsubset DotGothic16-Regular.ttf --layout-features='' --unicodes=U+0020-007E,U+00A0-017F,U+0370-03FF,U+0400-04FF,U+2000-206F,U+20A0-20CF,U+2100-214F,U+2190-21FF,U+2200-22FF,U+2460-24FF,U+2500-259F,U+25A0-25FF,U+2600-26FF`.
The dropped layout features select Japanese glyph variants, widths and vertical forms, the "fi" and
"fl" ligatures and a slashed zero, none of which the app uses.
Branch `feature/manga-style-2026-09`, stacked on the RPG branch, adds the manga ink style (`MangaUi`
in `ui/theme/Manga.kt`), with Bangers and Comic Neue from google/fonts unmodified. The RPG and manga
borders are drawn as rings, a shape minus a copy of it shrunk by the border's width (`ring` in
`UiStyle.kt`), rather than as strokes clipped to the shape, which left a faint fringe outside stepped
corners.
Branch `feature/xmb-style-2026-09`, stacked on the manga branch, adds the XMB waves style (`XmbUi` in
`ui/theme/Xmb.kt`). It keeps the glass panels, so it is a plain `UiStyle` with its own type scale and
backdrop rather than a `SolidUi`; the shared components draw glass for any style that isn't solid.
`GlassStyle.of` takes a `GlassScene` (the glows, Retrowave's sunset or the XMB waves, picked by
`UiEffects.glassScene`) in place of its Retrowave flag. For the waves it checks text over colors from
`WaveColors`, the class the backdrop draws with, so a change to the waves reaches the contrast search.
Another glass style with its own backdrop would add a scene and the colors to check over it.
`tintKeepingContrast` moved from the RPG style to `UiStyle.kt` for the gradient's tints. The font,
`res/font/mplus1.ttf`, is the variable `MPLUS1[wght].ttf` from google/fonts cut down with fontTools,
keeping the weight axis and the layout features the app's text uses: `pyftsubset 'MPLUS1[wght].ttf' --layout-features='kern,mark,mkmk,ccmp,rvrn,liga,locl,tnum,pnum,case'`
with the `--unicodes` ranges above. Its default instance is Thin, so each weight in `XmbFont` sets
the weight axis. The backdrop waits 25 ms between frames, about 30 frames a second with the wait for
the next vsync; redrawn at every frame, it kept the RP6 rendering about 88 frames a second in the
menus.
Branch `feature/sega-32x-2026-09`, stacked on the XMB commit, adds the Mega 32X and Mega CD 32X to
the Library. In `PhobosRunner.cpp`, both load `ares::MegaDrive` with a 32X configuration after
`MegaDrive::option("Recompiler", "true")` and `joinAbandonedThreads()`: the SH-2 recompiler's code
goes in the executable buffer the N64's recompilers use, which `M32X::power()` releases. `pak()`
adds the boot ROMs (`vector.rom`, `sh2.boot.mrom`, `sh2.boot.srom`) to the Mega Drive system pak
when the configuration names the 32X, from the `fw_32x_g`, `fw_32x_m` and `fw_32x_s` firmware
slots or else the copies ares bundles (next paragraph). `missingFirmware()`
(`PhobosCore.missingFirmware`) lists what a system lacks, now only the Mega CD BIOS; the view model
asks it before loading and shows BIOS Required, and `initialize()` refuses the load as a backstop.
`connectDevices()` leaves the Mega CD 32X's cartridge slot empty so that ares builds the 32X's own
board there. The Mega CD 32X reads its disc through mia's Mega CD medium. Save restores now take
the folder name from the root node, as the flush always did. On the RP6 six 32X games (Knuckles'
Chaotix, Virtua Racing Deluxe, Virtua Fighter, Stellar Assault, Blackthorne, NBA Jam TE) run at
59.9 FPS with the emulation thread at 66–75% of a core, with the bundled boot ROMs; no Mega CD 32X
disc has been run.
Branch `feature/upstream-firmware-2026-09` ships the firmware upstream ares ships. mia's resources
were empty stubs; `mia/resource/resource.hpp` now declares them as `Blob`s (data and size, which
convert to the span and pointer mia's call sites take), and the root `CMakeLists.txt` writes their
data into `mia-resource.cpp` in the build directory at configure time, from `resource.bml` and
`mia/Firmware`, rerunning when either changes. When upstream adds firmware, copy the file into
`mia/Firmware`, add its line to `resource.bml` and its name to the header. `pak()` falls back to
`mia::Resource` for the 32X's boot ROMs and the ZX Spectrum's ROMs.
Branch `fix/video-settings-2026-09` passes Settings → Video to the cores. `setVideoSettings()` in
`PhobosRunner.cpp` keeps the three values, and `applyVideoSettings()` sets overscan on every screen
and the cores' "Color Emulation" and "Interframe Blending" Boolean settings, calling `modify()` as
well as `setValue()`, which skips it when the value is unchanged. It runs after load, before the
settings are latched, and on every change, under `systemMutex`. The view model collects the three
from the settings store on an IO thread and also sends them before each load. The original Game
Boy's "Color Emulation" is a String setting (a choice of palettes) and is left alone, as ares desktop
leaves it.
Branch `feature/theme-legibility-2026-09`, stacked on the video settings branch, tints the Library
tiles and outlines text over uncontrolled backgrounds (`ui/theme/Legibility.kt`). `libraryTileFill()`
is the tile color, which `ThemedCard` now takes as `fill` (the card color by default) and
`ConsoleArtPalette` gets as its tile. `legibilityOutline()` picks an outline color for a text or icon
color; `LegibleText` draws a stroked copy of the text under it (the stroke is twice the outline's
reach, and the text covers its inner half), `LegibleIcon` and `Modifier.legibleOutline()` draw the
content eight times offset in a layer tinted with the outline color, under the content. Translucent
text and icons are drawn opaque and faded with their outline, which would otherwise show through them.
`ScreenHeader`, `SectionHeader` (on the backdrop), `BackdropText`, the top bar's title and back arrow
and the Library tiles use them; a top-bar action's icon should be a `LegibleIcon`, whose outline
follows its own tint (the Console's and Shaders' are). `LegibilityTest` and `GlassContrastTest`
check the outline and tile colors for every theme.
Branch `fix/console-follow-2026-09`, stacked on it, keys the Console's scroll-to-end on the log list
rather than its size, which stops changing once the log holds `MainViewModel`'s 2,000 lines. A drag
(`DragInteraction.Start` on the list) stops following; coming to rest at the end resumes it.
Branch `feature/ngcd-audio-2026-09` gives the Neo Geo CD its sound: the Z80 runs the BIOS-loaded
program, CD audio plays, and two drive fixes keep the music from breaking loads and the BIOS's
track changes ([Neo Geo CD sound](#neo-geo-cd-sound--2026-09-30)).
Branch `feature/ngcd-fast-load-2026-09`, stacked on it, adds Neo Geo CD Loading Speed (Settings →
Emulation → Performance, and the pause menu's Neo Geo CD section): Accurate (1x, the default), 2x,
4x or 8x. After every frame the emulation loop checks whether the drive is reading data
(`Cdd::statusCdc` bit 0 with `Cdd::control` bit 8); while it is, and for 30 frames after, the loop
paces like fast forward capped at the chosen rate. The emulated drive keeps its speed, so the BIOS
sees 1x timing; speeding up the drive itself is what gave DISC I/O ERRORs before (see the
`lspc.cpp` comment). How close a load gets to the cap depends on the device.
Branch `feature/perf-hud-pause-menu-2026-09`, stacked on it, adds Performance Monitor Contents to the
pause menu's Display section. It opens a page in place of the menu's list, like N64 Experimental,
with the live preview and the same preset, layout and metric controls as Settings → Performance
Monitor; both screens use `PerfHudPresetChips`, `PerfHudLayoutItems` and `PerfHudMetricItems` from
`PerformanceMonitorSettingsScreen.kt`.
Branch `feature/ngcd-savestates-2026-09`, stacked on it, completes Neo Geo CD save states, which left
out everything the CD has that a cartridge system doesn't: the sprite, PCM and FIX RAM (5.1 MiB),
the upload and controller-select registers, the drive (`Cdd`), decoder (`Cdc`, with its buffer) and
DMA (`Dma`) state, and the LSPC's drive tick counter (`ares/ng/disc/serialization.cpp`,
`CDSerializerFormat` 2). The host harness `tests/ngcd/` gained a round trip over all of it, which
fails when any piece is left out. The harness also runs on the dev Mac: its Command Line Tools
linker can't read the macOS 27 SDK's stubs, so link against Xcode's 15.4 SDK, with
`CXX="xcrun clang++" CC="xcrun clang"
LDFLAGS="-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.4.sdk"
bash tests/ngcd/run-tests.sh`.
Branch `fix/firmware-slot-persist-2026-09`, stacked on it, has Settings → Firmware's per-slot picker
take the persistable read grant (`takePersistableUriPermission`), so a picked file still loads after
a reboot or an app update; picks made before it need redoing once. Files found by scanning the
Firmware folder already worked, through the folder's persisted grant.
Branch `fix/pause-menu-nits-2026-09`, stacked on it: `performLoadState` takes `announceFailure`
(set by `loadState`, not by Auto-Load) and toasts when a slot is empty or the core refuses the state;
the pause menu's Load is disabled without a state, like Delete; the quit dialog uses `romTitle()`
(`util/RomNames.kt`), which drops a short alphanumeric extension only.
Branch `feature/ngcd-cdz-drive-2026-09` makes the Neo Geo CD boot and load like a CDZ. The drive
starts idle and reports itself stopped once asked about the disc (`Cdd::handleTocCommands`), as in
libretro neocd. It used to start at 9, which the BIOS's command queue counts as busy (`$C0BBD8`),
so the BIOS's first command waited out its 20 s queue timeout, the disc check ended about 28 s after
boot, and the CD-player menu waited for START. Now the BIOS reads the table of contents and
recognizes the disc within about 3 s (`$C0BCF8` sets `$10F656` bit 0) and boots the game by itself
in the logo's last 384 frames (`$C14C2A`). With the CDZ's BIOS (`00C0 A3E8` at `$C0006C`, libretro
neocd's test; `Cdd::doubleSpeed`) the drive reads data at 150 sectors a second (`Cdd::tickPeriod`),
as the real CDZ does; audio stays at 75 and the front and top loaders stay single speed. On the RP6
Samurai Shodown booted with no input, loaded its title in 21 s instead of 40 and a stage in 7 s
instead of 13.5 with no DISC I/O ERRORs, and played its attract demo with music.
Branch `fix/ng-crop-border-2026-09`, stacked on it: `LSPC::frame()` sets the viewport to the 224
picture lines (`0, 16, 320, 224`) unless `screen->overscan()`, instead of always the full 320×256
frame with its 16-line borders, so Core Provided no longer letterboxes Neo Geo games.
Branch `feature/perf-hud-order-2026-09`, stacked on it: `HudConfig.order` (`HudItem`, saved as names
in `perfHudOrder`; `HudItem.parseOrder` drops names it doesn't know and appends missing items in
their default order) drives both HUD layouts, and the vertical one still puts FPS and frame time in
one row when they're next to each other. `PerfHudOrderItems` is the reorder list on both screens.
Branch `feature/library-running-2026-09`, stacked on it: `MainViewModel.runningGame` (the loaded game
while the emulator screen isn't showing, with the frame `swapToLibrary()` captures through
`PhobosCore.takeScreenshot`) drives the Library's Running card (`RunningGameCard` in
`LibraryScreen.kt`), and the system page's ROM tap calls `swapBackToGame()` for the running game
(`isRunning`) instead of loading it again.
Branch `feature/controller-nav-2026-09`: `MainActivity.dispatchGenericMotionEvent` passes joystick
motion to `GameInputState` only while the game runs on screen (`emulatorScreenVisible && !isPaused`).
Otherwise it's left unhandled, so ViewRootImpl turns the RP6's hat D-pad (its built-in controller is
an "Xbox Wireless Controller" with HAT_X/HAT_Y and no D-pad keys) and left stick into D-pad keys for
Compose focus. `Modifier.focusRing` (`ui/FocusRing.kt`) rings the focused element; it's on
`ThemedCard`, the settings rows, the dock tabs, the pause menu's actions and rows, ROM rows and the
Running card. The window callback turns L1/R1 into `viewModel.stepTab()`, which `MainScaffold`
follows to the neighbouring dock tab, and `setPause(true)` calls `GameInputState.releaseAllButtons()`.
Branch `feature/appearance-library-themes-2026-10` ([PR #78](https://github.com/pwnedbygary/phobos/pull/78))
gives every style a choice of backdrop. Each style has its own scene enum in `data/SettingsStore.kt`
(`PixelBackdropScene`, `MangaBackdropScene`, `RpgBackdropScene`, `RetrowaveBackdropScene`,
`CrtBackdropScene`, `GlassBackdropScene`, `XmbBackdropScene`), stored under its own key and carried in
`PhobosThemeInfo`; the style's own `Backdrop`, or `MainScaffold` for glass and Retrowave, draws the
chosen one. Behind glass, the chosen backdrop also picks the `GlassScene` (`glassScene` in
`UiStyle.kt`) that the panels and the plates behind headers are fitted to: a new backdrop drawn behind
glass needs a scene of its own, or one it draws within, with its colors shared between the drawing
and `GlassBuilder` (as `AuroraColors`, `meshLine`, `SunsetColors` and `WaveColors` are) and sampled
in `GlassContrastTest`. `GlassBuilder` keeps the gloss and shade weak enough for the opaque dock it
falls back to when no see-through one passes. `CatalogBackdrop` (`data/BackdropCatalog.kt`) lists every scene of every
style for the screensaver, so a new scene goes in its style's enum and there; `CatalogBackdropView`
draws any of them in the current theme's colors. The screensaver lives in `MainScaffold`: an effect
keyed on the delay, an idle counter, the route, the binding capture and the dialog and menu hold
shows it after the delay, never on `NO_SCREENSAVER_ROUTES`, during a capture or while held. While
it shows, the page backdrop fades out and leaves composition, as behind the game;
`MainViewModel.consumeScreensaverInput()` makes the window callback drop keys, and a `BackHandler`
takes Back, which predictive back delivers without that callback. Input bumps the idle counter only
while the screensaver could arm, since each bump recomposes the scaffold. Dialogs and menus take
input in windows of their own, so `PhobosAlertDialog`, `PhobosDropdownMenu` and the Driver
Manager's dialogs call `HoldScreensaver()`, which keeps it from arming while they're open, however
long. It counts only under the `LocalScreensaverHold` that `MainScaffold` provides to the pages and
to its own dialogs, so a new dialog or menu should call it within those. The Library's console art
goes through `systemIconSlug` (`ui/PlatformIcons.kt`): Systematic's SVG for the slug, or for the
Phobos, Pixel and Manga packs a glyph from `ui/theme/PlatformGlyphs.kt` drawn by
`PlatformGlyphArt.kt`. A new system needs a slug, listed in `systemIconSlugs` with a glyph
(`PlatformGlyphsTest` checks the two match).
On the dev Mac the Gradle distribution and dependency cache live in the git-ignored
`.local/gradle-home`; set `GRADLE_USER_HOME` to it, since the wrapper can't download there.
Accuracy-neutral: cross-section `J` and not-taken-edge (`LinkSlot`) linking with runtime
PC/live-state-key gates, RSP pipeline hash-skip (since reverted), `Screen::frame` CV. Opt-in N64
Experimental speed hacks (faster CPU sync, skip cache timing, RSP task mode; default
off). Measured one at a time on the RP6: no clear FPS gain in Mario vs Boo, and Faster
CPU sync stalled Conker's pub for 91.5 s at a time — a lost CP0 timer interrupt (Count only
advanced at sync; Conker's ~25 µs timer landed behind Count), root-caused on device and
fixed (accurate between-sync Count reads, wrap-safe timer check). Conker with Faster CPU
sync then ran 280 s stall-free; at default settings the fix measured Mario vs Boo 58.5 /
58.8 FPS and the smoke titles ran clean. Re-measured on the PR #9 build: still no worthwhile
gain, so Faster CPU sync stays an opt-in
([audit](performance-audit.md#2026-09-26-follow-up-speed-hacks-re-measured-gpu-driver-waits-after-the-handoff-fix)). Touch: seamless D-pad diagonals; N64 L large / Z pills both
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

## Desktop builds — 2026-10-02

Branch `cursor/desktop-phobos-2b67` ([PR #97](https://github.com/pwnedbygary/phobos/pull/97)),
at the user's request: Phobos for Linux (AppImage + .zsync), Windows (Phobos.exe) and macOS
(universal Phobos.app), alongside the Android app. The first cut was
`cursor/desktop-phobos-ports-a292` ([PR #80](https://github.com/pwnedbygary/phobos/pull/80));
see "Rebased onto master" below.

- Build: `CMakeLists.txt` builds the cores, mia and the runner as `phobos_core`. Android links it
  into `libphobos_android.so` with the old flags and libadrenotools; elsewhere it links into the
  `phobos` program with SDL 3.2.30 (FetchContent, static). sljit and libco pick the CPU from the
  compiler instead of ARM64 being hardcoded. Presets: `linux-x64`, `windows-x64` (MSYS2 UCRT64),
  `windows-x64-cross` (MinGW-w64 from Linux), `macos-universal`.
- Runner: ANativeWindow, AAudio and the adrenotools loader moved unchanged behind
  `PhobosHost.hpp` into `PhobosHostAndroid.cpp`; `desktop/PhobosHostDesktop.cpp` is the SDL
  version. Desktop Vulkan comes from `libvulkan.so.1`, `vulkan-1.dll` or the bundled MoltenVK.
  Thread affinity, the thread id and the WFE wait stay Android-only. Also changed for both:
  without an audio device the runner discards samples and retries every five seconds (it used to
  retry on every core write), and PS1 memory cards are written through stdio.
- Assets: the APK's `assets/System` (boot ROMs) and `assets/Database` ship beside the desktop
  program, and it copies missing files into its data folder at start, as `extractAssets()` does.
- Shell (`desktop/`): a placeholder UI in SDL's debug font: library (disc sets, cue/m3u, folder
  names decide shared extensions), pause menu (states, reset, fast-forward, mute, disc change, N64
  options, PS1 analog, fullscreen), ZX Spectrum and MSX keyboards, rumble, firmware matched as
  `MainViewModel.scanFirmware()` matches it, `settings.ini`.
- Packaging: `scripts/package-linux-appimage.sh`, `scripts/package-windows.sh`,
  `scripts/package-macos-app.sh`; CI in `.github/workflows/desktop.yml`.

Checks run (2026-10-02, Linux x86-64 VM, no GPU, no sound card, no controller):
- Linux: GCC 13.3, CMake 3.28.3: `cmake --preset linux-x64 && cmake --build build/linux-x64`
  builds. Blargg's `cpu_instrs.gb` passes all tests at 59.8 FPS; library, pause menu, save and
  load state, and quit to library work (driven with xdotool).
- AppImage: `scripts/package-linux-appimage.sh build/linux-x64` writes the AppImage (7.5 MB) and
  its .zsync. Run from outside the tree with an empty data folder, it installs System/Database
  and runs the same test.
- Windows: MinGW-w64 GCC 13 (posix), `cmake --preset windows-x64-cross`. `scripts/package-windows.sh`
  confirms Phobos.exe imports only Windows' own DLLs. Under Wine 9.0 it runs the same test at
  59.8 FPS.
- macOS: GitHub Actions run 37071950243 (macos-latest) built and packaged the app; `lipo -archs`:
  x86_64 arm64; MoltenVK 1.4.2 bundled; signed ad hoc.
- Android: JDK 21, AGP 9.3.2, NDK 28.2.13676358, CMake 3.22.1:
  `./gradlew :app:assembleLegacyDebug :app:testModernDebugUnitTest` succeeds, 211 tests pass,
  and all 85 JNI functions are exported. lld's `--why-extract` shows the archive members the
  static link leaves out are unused (Saturn stub, unused CPUs and ymfm chips, parallel-RDP's WSI).

Not checked: any device or real desktop hardware; Nintendo 64 on desktop (no GPU here; MoltenVK
untested); audio output; gamepads and rumble; the independent review that
[development-process.md](development-process.md) requires before commit (the PR is a draft for it).

Next: UI parity with the Android app (the user's direction); N64 on real desktop GPUs, including
MoltenVK; MSVC/clang-cl is not supported (the runner's threads use pthreads; MinGW provides them).

Rebased onto master (2026-10-03, the user's choice): #80's 11 commits replayed in order on master
with `git cherry-pick -x`, authors kept, in a new PR; #80 is closed. The two conflicts were next to
code master added: the LaserActive side functions in `PhobosRunner.cpp/.hpp`, just ahead of the
`setSurface` that #80 limits to Android, and plan item 7's "Parked" note. Master's runner code since
#80's base calls nothing Android-only, so the desktop build takes it unchanged. On top of that:
- The independent review of #80 (Bugbot, posted on #80) found that a game dropped on a running one
  skipped the key and rumble reset. The same path had a worse problem: `launch()` set the runner's
  per-game keys (memory card key, ROM path) before `initialize()`, which only then unloads the
  running game, so its battery save, PS1 memory cards and MSX data tape were written under the new
  game's name (over the new game's own saves when both are for the same system). `launch()` now
  unloads the running game first through `unloadGame()`, the teardown `quitGame()` did, without
  quitting a frontend's session. If the new game then fails to start, the library shows instead of
  the stopped game. Android isn't affected: `startLoad()` unloads before every load.
- The packages carried no license notices. They now ship LICENSE, and COPYING once the relicense lands: in the
  AppImage's `usr/share/doc/phobos`, beside Phobos.exe as `.txt` files, and in Phobos.app's Resources. Phobos.app
  also gets MoltenVK's Apache-2.0 license, from MoltenVK's release archive. Still missing: the notice for
  winpthreads, which Phobos.exe links statically.
- Not on desktop yet (part of UI parity): the systems added since #80's base (Mega LD, PC Engine LD,
  Pocket Challenge V2) aren't in `desktop/Library.cpp`'s table, and the LaserActive BIOSes aren't in
  `desktop/Firmware.cpp`'s copies of the app's firmware maps. The desktop also lacks the app's
  request for an MSX BIOS with BASIC before a tape starts (`msxFirmwareMissing()`), the MSX tape
  deck and data tape controls, and LaserActive side changes.

Checks run (2026-10-03, Mac M1 + Retroid Pocket 6 `49016109`):
- macOS arm64, native rather than the universal preset (Apple clang 17, CMake 4.4.3, Xcode's macOS
  15.4 SDK; SDL 3.2.30 needs CMake 3.24 or later on macOS, so the Android SDK's 3.22.1 can't
  configure it): builds. Two homemade 16 KiB MSX cartridges run on the bundled C-BIOS at 60 FPS,
  with sound through SDL. Handing the second one to the running program (`open -a` on a throwaway
  app bundle, which SDL delivers as a drop) writes the first game's saves under its own name
  ("[Blue]") and starts the second. A build of #80's `main.cpp` wrote them as "[Red]".
- `scripts/package-macos-app.sh` on that build: Phobos.app holds LICENSE and LICENSE-MoltenVK (and COPYING, with
  one in place), its ad hoc signature verifies, and at start it loads the bundled MoltenVK. No N64 game was run.
- Android: `./gradlew testModernDebugUnitTest assembleModernRelease` succeeds; the 240 tests pass,
  and all 92 JNI functions the build compiles are exported. On the RP6, an MSX cartridge paused,
  resumed and quit: AAudio stopped, restarted, and stayed open for the next game, which played
  sound. Sub-Terrania, Mario Tennis (Turnip through adrenotools) and Ape Escape ran at 60 FPS with
  sound, as they did in the boot pass on master.

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
when the app starts cold (`viewModel.systems.first {}`; since fixed, as the load now waits
for the app to be ready); touch placement avoids display
cutouts but not system gesture areas (the controls are excluded from the edge gestures
since 2026-10-03); the Neo Geo 2×2 grid's center presses B+C (they
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

## MSX game in one cartridge slot — 2026-10-03

Branch `cursor/msx-one-cartridge-v2-2b67` (#99's commit, rebased onto master after the desktop and licensing PRs
merged). The plan row "MSX game in one cartridge slot" has the details: an MSX
cartridge game was connected to the Expansion Slot as well as the Cartridge Slot, so the BIOS found a second copy;
`connectDevices` now leaves the Expansion Slot empty, as ares's own frontend does. A state holds the board of each
slot with a cartridge in, so MSX states move to v135 and older ones are refused (the review's finding: they would
have loaded misaligned).

- **Checks run:** the modern release builds, 240 host tests pass. On the RP6, a probe cartridge written for the test
  printed "S1 S1 S2" and held before the change, and now prints "S1 S1" before the BIOS goes on to BASIC. The log
  lists one cartridge port instead of two. A state saved to slot 0 loaded back. The probe, its states and its save
  folder were removed afterwards.
- **Version code:** the RP6 has the desktop branch's build number (104601, 11 commits ahead of master), and Android
  refuses downgrades on this user build, so this test build was given the same number (`-PversionCode=104601`).
  The in-app updater couldn't install master's nightlies there until master passed 104601, which the desktop PR's
  merge did.
- **Seen, fixed separately:** in 40-column text (SCREEN 0), each row's first character showed again at the right
  edge (branch `cursor/msx-text-columns-2b67`).

## Phobos's own code under GPL-3.0-or-later — 2026-10-03

Branch `cursor/gpl-own-code-v2-2b67`, for the user to merge: #94's commit, rebased onto master after #95 and #96
merged. It follows their answers of 2026-10-03: the work-email commits are cleared, and the firmware stays, so no
GPL code from elsewhere comes in.

- LICENSE opens with a Phobos notice (© 2026 Phobos Team, as the About screen has it): Phobos's own code under
  GPL-3.0-or-later, each component under its own license, and the firmware ares ships outside the GPL. COPYING is
  the GPL's text from gnu.org (SHA-256 `3972dc97…`, the published file); the build appends it to the APK's notices
  as their last one. The README gains a License section, and the Licenses and About screens say what's under the GPL.
- **Checks run:** the modern release builds, 241 host tests pass on master (`LicenseNoticesTest` now expects Phobos
  then ares and the GPL grant, and checks that COPYING is GPL v3 without a notice rule in it), and the APK's notices
  open on Phobos's and end with the GPL. On the RP6, with #94's build, Settings → About → Open-source licenses opened
  on Phobos's notice first, then ares, and ended with GNU GENERAL PUBLIC LICENSE.
- **Not checked:** the legacy APK on a device.

## MSX tape saving — 2026-10-03

Branch `cursor/msx-tape-saving-2b67`, stacked on `cursor/n64-hack-notes-2b67`. The plan row "MSX tape saving" has
the details; the user chose a data tape per game in its save folder.

- **Checks run:** the modern release builds, 240 host tests pass (new: `theDeckStateNamesTheTapeAndItsRecording`,
  `aBlankOrRecordingDataTapeSaysSo`). On the RP6 with the user's MSX BIOS and a stand-in tape written for the test
  (`SAVETEST.cas`, from a script kept outside the repository): `CLOAD` loaded its program from the game's tape; with
  Data Tape and Record on, `RUN` recorded it (`CSAVE "SAVED"` inside the program); the pause wrote
  `data-tape.wav`. A decoder written for the check read the file back as the exact program. After quitting and
  launching again, the data tape (Record off) loaded with `CLOAD`, and `LIST` showed the three lines exactly, twice.
  With the blank data tape in, `CLOAD` waited; swapping the game's tape back in started it at once ("Found:SVTEST").
- **Found on the way:** recording stored half the range plus the bit, a signal one step tall that a reload
  flattened (fixed to the full range), and at 44.1 kHz `CLOAD` misread a byte now and then (new data tapes run at
  176.4 kHz). The review found that a state doesn't record which tape was in, so a state saved while recording
  could start recording over the game's own tape; that tape no longer takes recordings, and a restored recording
  resumes only on a tape that does. Its second pass found that a swap didn't resync the motor relay, so a loader
  waiting with interrupts off never started the new tape; its third, that a recording a state brought back without
  the arming didn't stop with the motor (it now stops). The fourth found no bugs.
- **Cleaned up on the RP6:** the test tape, its save folder, its auto state (written on quitting) and the empty MSX
  folders they left.
- **Unexplained once:** one scripted run, the first launch right after installing, stalled at its first `CLOAD`
  (the game's tape never started, so nothing was recorded). The same steps by hand and two full scripted runs
  after it passed, so it wasn't traced; a mistimed tap in the script is the likeliest cause.
- **Not checked:** an MSX2 game, a game that saves to tape by itself, the legacy APK on a device.

## PSP core: the GE faster again, four pixels at a time — 2026-10-07

Branch `cursor/psp-ge-speed2-2b67`, on top of `cursor/psp-ge-features-2b67` (#157, the entry below): seven commits,
5a22092a0 to 1e63ea36a, then the docs; local only, not pushed. docs/psp-core.md, part 30, describes it. The owner's
priority: the software renderer as fast as humanly possible, every picture the same. Original code; no PPSSPP or
JPCSP source read.
- **What:** triangles' and sprites' rows drawn four pixels at a time in SIMD lanes (`ares/psp/ge/four.cpp`: GCC's and
  Clang's vector types, NEON on ARM64, SSE on x86-64), each lane the scalar arithmetic exactly (the same float
  expressions with a vector in every product, so ARM64's fused multiply-adds fall as before), the depth test before
  the texture (exact without the stencil test); textures decoded a row at a time and the palette's hash 8 bytes at a
  time; jobs a union of their kinds (584 to 256 bytes) and directional lights once a primitive; textures read as drawn
  (drawing over themselves) read straight from the host's memory; a vertex's depth flags once a vertex.
- **Found by ThreadSanitizer and fixed** (its own commit): a row's partial four wrote back its outside lanes' bytes,
  another row's where a stride is within three pixels of the width drawn; such fours now touch only their own lanes.
- **Speed** (host M1, the Android build's flags, 300 frames a scene, best of 3, part 29's and this taking turns; 1 GE
  thread and 7): MC3 night race 11.6 -> 27.8 and 26.6 -> 40.9 fps, its profile menu 11.6 -> 32.0 and 29.0 -> 51.7, LCS
  in the city 29.0 -> 77.5 and 75.8 -> 131.5, LCS in the woods 20.6 -> 47.4 and 60.7 -> 108.2, Peace Walker's title
  29.2 -> 106.2 and 135.2 -> 487.9, Lumines' demo 59.0 -> 111.9 and 197.7 -> 341.8. On the RP6 (part 29's app put back
  for "before", data kept): MC3's menu 21-23 -> 36-37 fps, its race 17-19 -> 30-34 (the Quick Race differs run to
  run); Peace Walker, Lumines, LCS's intro 60 -> 60. At 7 threads the GE's own thread is the limit now.
- **Same pixels:** six scenes (MC3 race and menu, LCS city and woods, Peace Walker's title, Lumines' demo): every
  frame's picture and VRAM and RAM at the end identical to part 29's at 1 and 7 threads; part 24's differential fuzzer
  against part 29's GE (thousands of lists, 1-8 threads, ASan/UBSan and TSan); new group "draw3d four pixels at a time
  against one" (20,000 random primitives, fours on and off; 19 broken versions caught); tests/psp 282 groups with both
  sanitizers ("psp measure" the same line for line), tests/psp/ares 290 checks.
- **Dropped:** handing batches over at 2^17-2^18 pixels of work, workers spinning before sleeping (no gain: the GE's
  own thread is the longest path at 7 threads).
- **Next:** the GE's thread (the CPU's emulation most of it, then vertex setup: 117,000 vertices a frame in MC3's
  race), four at a time for textures drawn over themselves where provably safe, shorter waits for render targets read
  back.
- Scratch, outside the repository: `/tmp/ge2-bench` (runner, states, scripts, profiles), `/tmp/ge2-fuzz`,
  `/tmp/ge2-mut` (broken versions), `/tmp/ge2-device` (RP6 screenshots and stats).

## PSP core: lines, bounding boxes, DXT textures and sceGeBreak — 2026-10-06

Branch `cursor/psp-ge-features-2b67`, on top of `cursor/psp-hle-games5-2b67` (#156, the entry below): commits
bb1da6c0a (the GE's code and tests) and 5892da700 (the measuring program's round 4), then the docs; local only, not
pushed. docs/psp-core.md, part 29, describes it. Written from pspsdk's headers and GU library, pspautotests'
programs and the results (and screenshots) they recorded on a PSP, public S3TC descriptions and the owner's games;
no PPSSPP or JPCSP source read.
- **Lines** (PRIM 1 and 2), in 2D and 3D (cut at the near plane), colors, depth, fog, textures, blending, through
  the same pipeline, threads and decoded textures: the "diamond exit" rule, its edges fitted to every picture of lines
  pspautotests recorded (gpu/primitives/lines, linestrip, indices: now pixel for pixel). Not the PSP's in every pixel:
  gpu/exact/lines' CRCs match neither it nor any variant tried. Anti-aliasing isn't emulated.
- **Bounding boxes** (BBOX, BJUMP): screen-space, the scissor and region a pixel wider on the left and top, held to
  the 4096 screen, z against w with DEPTH_CLIP_ENABLE, the count's quirks: every case of pspautotests' gpu/bounding
  (count, planes, viewport, vertexaddr's results) as the PSP printed them. Out of sight only past one edge for every
  vertex (chosen). State version 12.
- **DXT1/3/5**: the PSP's own block layout (colors first, indices then the two 565 colors with red on top, DXT3/5's
  alpha after), colors widened by shifting, thirds and halves rounded down: all 2216 results of gpu/texcolors/dxt1,
  dxt3, dxt5. The layout came from those programs and recordings; the blocks' order is S3TC's, taken (no DXT game on
  this Mac). Kept decoded like other textures.
- **sceGeBreak**: as gpu/ge/break and breakwait recorded (refusals and their order, mode 0 breaking off the running
  list for sceGeContinue, mode 1 throwing all away, waiters left waiting).
- **Also, from pspautotests' pictures**: 8-bit positions in through mode read as 0; index format 3 is 32-bit indices,
  their low 16 bits taken.
- **Measuring program, round 4** (lines at every sixteenth, short lines, strips, colors, depth and texels along lines,
  3D lines, 256 bounding boxes, DXT colors, alphas and block order): built in pspdev's Docker image;
  `tests/psp/programs/pspmeasure.elf` replaced (hash in its README). The owner's EBOOT.PBP for the PSP isn't
  committed (kept in the owner's `.local/psp-round4/`). Running it in the core caught a mistake of its own first
  (memory taken inside an object let the GE reach an unaimed BJUMP).
- **Checked**: tests/psp 280 groups with ASan and UBSan, tests/psp/ares 290 checks, none failed; the comparison with
  the owner's PSP (rounds 2 and 3) the same, line for line; 16 broken versions each caught. Speed within a couple of
  percent either way in six scenes, 1 and 7 threads. Games: Space Invaders Extreme's title gains its line box,
  Sindacco a line across the sky; Dominator, LCS, SOCOM, Snoopy, VCS, Peace Walker unchanged in the frames captured;
  pictures in `/tmp/gef-pictures`.
- **Left**: curved surfaces (Macross); the exact line rule, anti-aliased lines, the DXT block order, boxes past
  different edges (round 4 will tell); breakwait's wake order; sceGeSaveContext's layout; gpu/exact/coverage's tall
  triangles; PRIM kind 7.
- **After review**: clean room, hostile inputs (ASan) and round 4 in the core all fine. Fixed the one Low finding:
  sceGeBreak(1) now drops the GE's callbacks waiting their turn and has one running return to no GE (else a stale
  callback could end the list that took its ID before that list's own callback); unmeasured. GE callbacks are a call
  kind of their own (`Call::Ge`; state version 12 still). Test "ge break and callbacks", both engines, a state round
  trip. tests/psp 281 groups, tests/psp/ares 290 checks, none failed. The round-4 EBOOT stays out of the repository
  (the owner keeps it in `.local/psp-round4/`). RP6: Space Invaders Extreme's title shows its box of lines at 60 fps,
  GTA LCS's intro unchanged at 60 fps.

## PSP core: kernel mutexes, alarms, virtual timers, and the clock in system calls — 2026-10-06

Branch `cursor/psp-hle-games5-2b67`, on top of `cursor/psp-desktop-2b67` (#155, the entry below): commit 3133b63b2
(code and tests) and the docs after it; local only, not pushed. docs/psp-core.md, part 28, describes it. Written from
pspsdk's headers and pspautotests' programs and recordings; no PPSSPP or JPCSP source read.
- **Functions** (41, the ones the owner's 266-game report, `origin/local/psp-runner`'s docs/psp-compatibility.md,
  finds missing most): kernel mutexes (8: priority or first come, recursive counts, timeouts, callbacks, a holder
  ending, delete and cancel waking waiters), alarms (4) and virtual timers (14), their handlers called as interrupt
  handlers; sceKernelReleaseWaitThread, sceKernelCancelWakeupThread (the report's 0xfccfad26), CancelSema,
  CancelEventFlag; sceCtrl's idle thresholds, sceGeEdramSetAddrTranslation, the memory blocks by the NIDs games
  import (0xfe707fdf, 0x50f61d8a, 0xdb83a952), sceImposeGetLanguageMode and GetBatteryIconStatus, DevkitVersion,
  USec2SysClock and its wide form. The lightweight mutex now honours its priority attribute.
- **Time in system calls**: the clock catches up at each syscall with the instructions run (it had stood still
  through a CPU go, up to a frame), each syscall ends the go at the next thing due, the thread manager's timeouts last
  max(t, 205) + 35 microseconds as waittimeouts recorded (1 times out "at once"), and timers go off no sooner than
  215 microseconds after the call that sets them. States: version 11.
- **Checked**: pspautotests through the HLE kernel, before and after: threads/ and intr/ 36 to 72 of 167 matching
  (all of mutex and vtimers, alarm's four, mutexhandoff, waittimeouts, release, semaphores' cancel, callbacks'), the
  other areas 48 to 55 of 192 (ctrl/idle, sysmem/memblock, misc/timeconv, audio and ctrl timing), gpu/ge/edram;
  one line worse (msgpipe/trysend, 9 ms left for 8). tests/psp 272 groups (13 new) with and without ASan and UBSan,
  tests/psp/ares 286 checks, tests/allegrex 56 groups, none failed; five broken versions each caught. The ten games
  (Burnout Legends and Dominator, LCS, VCS, Peace Walker, Lumines, Midnight Club 3, SOCOM, Snoopy, Gunhound EX), 3600
  frames in `origin/local/psp-runner`'s runner (a scratch copy) built from this tree and from #155's: the same
  screens, no missing function called; five import functions added here but don't call them in that minute.
- **Left**: scePsmf and scePsmfPlayer (19 games): no allowed source for the PSMF container or the player's states;
  what calls and handlers cost (alarmcosts, preemptuser); event flags' and semaphores' create and status, the
  lightweight mutex's status, sceDisplayAdjustAccumulatedHcount (seen in pspautotests, not changed); next seen in the
  owner's games' imports: sceKernelReferThreadRunStatus, sceKernelGetThreadmanIdList, sceGeBreak.
- **Not checked**: the RP6; the app and the desktop program built whole (the core was built in the runner and the
  tests); a game that calls the new functions (none here does in its first minute).
- **After review** (the commit after the docs; part 28's "After review"): a mutex tried from an interrupt handler
  while no thread ran was left held by nobody (no thread could free it, a state saved then wouldn't load), and one
  tried from a handler that interrupted a thread went to that thread; now trying, unlocking, cancelling and making
  one held, kernel or lightweight, are refused outside a thread (ILLEGAL_CONTEXT), as locking was in a handler. The
  recompiler now gives the clock its exact count at a syscall inside a block, so both engines read the same time.
  The battery icon's and the cancels' negative-count comments say why. tests/psp 274 groups (2 new, each failing
  with its fix undone) with and without the sanitizers, tests/psp/ares 286 checks, tests/allegrex 56 groups, none
  failed; pspautotests' threads/ and intr/ unchanged. The reviewer ran this branch's build on the RP6: Peace Walker,
  GTA LCS and Burnout Dominator at 60 fps, as on #155's.

## PSP core: the PSP in the desktop program, with FFmpeg on Linux, macOS and Windows — 2026-10-06

Branch `cursor/psp-desktop-2b67` (#155), on top of `cursor/psp-codecs-2b67` (#153, the entry below), with #153
merged in as 8501415c7 and the review's fixes on top; CI's desktop build then passed on Linux, macOS and Windows,
packages included (run 37544580853). docs/psp-core.md, part 27, describes it. Original front-end code in the desktop
program's style; no PPSSPP or JPCSP source read.
- **Library**: PSP games from a `psp` folder or any folder: the PSP's extensions (iso, cso, zso, dax, jso, chd, pbp,
  elf); `.iso`, `.chd` and `.pbp`, shared with the PlayStation, are the PSP's when mia's PSP medium takes them (the
  runner's new `ares::isPspGame`, which reads the file's head: "PSP GAME" ISOs, DVD CHDs, EBOOT.PBPs not of category
  "ME", MIPS ELFs). An EBOOT.PBP goes by its folder's name, as in the app; PSP discs are listed one by one.
- **Settings** (pause menu while a PSP game runs, and `settings.ini`; each from the next start): the memory stick (the
  shared `<saves>/PlayStation Portable/Memory Stick` by default, or a folder picked), the user's fonts (none by
  default; the folder picked, its `font` or its `flash0/font`, with how many of the 18 it holds), drawing threads
  (Auto = all cores but one, 1-8). A failed start shows the medium's reason (`lastLoadProblem`).
- **Controls and picture**: the pad by position (Cross south, Circle east, Square west, Triangle north, L/R, Start,
  Select, D-pad, left stick); the keyboard's arrows, X/Z/S/A, Q/W, Enter, right Shift, and I/J/K/L now push the left
  stick. F8 (and the menu's "Screenshot", new) saves the core's frame as a PNG in the data folder's `screenshots`.
  The picture is drawn at the whole multiple of 480x272 nearest the window (1-4), then scaled bilinearly.
- **FFmpeg**: `thirdparty/ffmpeg/build.sh` gains `host` for Linux and MSYS2 UCRT64 (`--target-os=mingw32` there),
  `macos ARCH...` (each architecture, then `lipo`; `@rpath` names; macOS 11+) and `windows TOOL-PREFIX` (MinGW-w64
  from Linux, which also needs a host C compiler); the desktop branch of CMakeLists.txt runs it, links the two shared
  libraries, defines `ARES_ENABLE_FFMPEG`, and copies them beside `phobos` (run paths `$ORIGIN`, `@executable_path`).
  `-DPHOBOS_FFMPEG=OFF` turns it off. The packages carry them as separate files (AppImage `usr/lib`, Phobos.app
  `Contents/Frameworks`, beside Phobos.exe), each script checking them; desktop.yml installs nasm etc., caches the
  build by its name and uploads FFmpeg's tarball with each package and to a tag's release. `LICENSE`'s notice says
  where the libraries are in each package and how to swap them; README mentions the desktop's PSP and the switch.
- **Checked (host Mac)**: Burnout Legends from boot to racing at 60 fps (menu movie, soundtrack, profile saved to the
  memory stick, every face button, D-pad, stick, F5/F9 state round trip, the pause menu); Liberty City Stories to
  play after its opening cutscene at 60; Midnight Club 3's title movie and profile menu (25 fps there, the core's
  pixel filling); Space Invaders Extreme's name entry and Lumines' title at 60. The universal Phobos.app (arm64 and
  x86_64, FFmpeg's too) ran Burnout natively and under Rosetta at 60, the libraries from Frameworks; a copy with
  libavcodec swapped and re-signed ran too. Library scan and font folder rules checked by scratch programs.
- **Windows and Linux (Docker, targeted: 2 GB is too little for the whole core)**: FFmpeg built by CMake for MinGW-w64
  and for Linux arm64; the changed sources and the runner compiled for both; a program linked as Phobos.exe is
  passed `package-windows.sh` (after linking FFmpeg's DLLs `-static`: they had needed libwinpthread-1.dll), and one
  beside the Linux libraries found them by `$ORIGIN`. Full builds and packages are CI's.
- **Tests**: tests/psp 259 groups, tests/psp/ares 282 checks, the app's 264 unit tests, none failed; modern release
  APK built (23,604,918 bytes).
- **Review and CI fixes** (CI on 8501415c7 failed only at Linux's AppImage): linuxdeploy now runs with the build
  folder on `LD_LIBRARY_PATH` (it couldn't find libavutil for libavcodec), and the script checks both libraries are
  files in `usr/lib` that the program and each other find by run path (checked in an x86-64 Docker container with a
  stand-in program: the AppImage's copy found ATRAC3plus); `LICENSE` gains the MinGW-w64 runtime and winpthreads
  notice (in LicenseNoticesTest's list, and a test of its texts); the PSP fonts label is counted when the setting
  changes, not twice a frame; `build.sh`'s stale-lock takeover is atomic (a `takeover` mkdir inside the lock;
  8 builds at once, 3 rounds, one takeover each). Burnout Legends again at 60 on the Mac; the app's 265 unit tests.
- **Not checked**: a real gamepad, real PSP fonts, a PlayStation CHD in the library, the whole program run on Linux
  and Windows, and the fixed AppImage step in CI's whole build (next push).

## PSP core: music and movies through FFmpeg — 2026-10-06

Branch `cursor/psp-codecs-2b67`, on top of #152 (`cursor/psp-test-data-2b67`, the entry below, which carries
master's desktop build and the GPL-3.0-or-later license), PR #153: commits fffd26303 (FFmpeg's build), c0f2625e4
(sceAtrac3plus), 332015eaa (sceMp3), e772d44a2 (movies) and the docs; then the merges of #152 and master, and the
review's fixes. docs/psp-core.md, part 26, describes it. Original code; no PPSSPP or JPCSP source read (pspsdk,
pspautotests' programs and recorded results, the PSP Developer Wiki, public container formats, FFmpeg's API
documentation, the games' behavior).
- **Licensing setup (the owner's choice: FFmpeg's LGPL decoders).** `thirdparty/ffmpeg/build.sh` downloads FFmpeg
  9.0.2's official tarball, checks its SHA-256, and builds it unmodified with `--disable-gpl --disable-nonfree
  --disable-version3 --disable-everything --disable-autodetect` and only the decoders atrac3, atrac3p, mp3float and
  h264, as two shared libraries (libavcodec, libavutil); no FFmpeg binary or source is committed. The root
  CMakeLists.txt (option `PHOBOS_FFMPEG`, on; `-Pphobos.ffmpeg=OFF` from Gradle) builds it for arm64-v8a with the
  NDK, cached in `.cache/ffmpeg`, defines `ARES_ENABLE_FFMPEG` for `phobos_core` and links it into `phobos_android`
  (the desktop program builds without it for now); the APK carries the two `.so` files beside Phobos's, so they can
  be replaced (the LGPL's relinking terms). CI caches the build (keyed on the build's own name) and puts the tarball
  in `android/dist` to be published with each release (the complete corresponding source). `LICENSE` (Settings,
  About, Open-source licenses) has FFmpeg's notice, origin, hash, build script, how to swap the libraries, the
  Independent JPEG Group's credit and the LGPL 2.1 text; the About screen says "This software uses libraries from the
  FFmpeg project under the LGPLv2.1"; `LicenseNoticesTest` checks it all against the script. Without the define
  (`PSP_FFMPEG=0` for the host tests) the core builds with no decoders, the streams refused as before. The README's
  build section lists what the build needs (bash, make, curl, xz; the network the first time), the Gradle switch
  and `PHOBOS_FFMPEG_TARBALL`.
- **APK**: 23,604,722 bytes against 22,574,705 built with `-Pphobos.ffmpeg=OFF`: +1.03 MB (4.6%) for FFmpeg.
- **Decoded**: ATRAC3 and ATRAC3plus (sceAtrac3plus: whole, halfway and streamed files, loops, the second buffer,
  resets, every query; pspautotests' atrac programs match but for the low-level and sas modes), MP3 (sceMp3: both
  handles, its buffers' halves, loops, resets; no game of the owner's uses it), movies (the PSMF header read; H.264
  pictures by sceMpegAvcDecode or sceMpegAvcDecodeYCbCr plus sceMpegAvcCsc, converted by BT.601 into the game's pixel
  format; their ATRAC3plus sound by sceMpegGetAtracAu and sceMpegAtracDecode). sceAac, scePsmf(Player) and sceSas's
  ATRAC3 voices aren't done: no game of the owner's imports them but Peace Walker four scePsmf functions, not seen
  called.
- **Heard and seen** (host, from boot; WAVs and frames under `/tmp/codecs-runner/out`; pitch checked against the
  decoded streams, matching): Burnout Legends' EA movie, FMV and title over its movie, music -11.8 dBFS (0.34% of
  samples clipped in the game's own mix); Dominator's logo movies and title over its movie (-18.4 dBFS); the GTAs'
  and Midnight Club 3's logo, title and credits movies with sound (-15 to -18 dBFS, next to no clipping); Space
  Invaders Extreme's music (-15.9 dBFS at its title, -10.6 in its stage) and its stage's background movie, black
  before; SOCOM's menu music (-29.4 dBFS) and Brave Story's (-25.8).
- **CPU** (host): 90 microseconds an ATRAC3plus frame (0.2% of a core a stream), 600 a movie picture decoded and
  converted (1.8% at 30 a second); 1.9-2.8% of the game's time in movie scenes. On the RP6: Burnout Legends' opening
  movie and its profile screen over the menus' movie at 60 fps (HUD CPU 72%, 54%), no decoding errors in the log.
- **Checks**: `tests/psp/run-tests.sh` 259 groups with both sanitizers, no failures (new: atrac.cpp's 12, mp3.cpp's
  4, movies.cpp's 8; state fields and refusals for all three; real FFmpeg decodes of pspautotests' sample.at3 and
  sample.mp3 with `PSP_AUTOTESTS`; #152's six media groups); `tests/psp/ares` 282 checks, none failed; both again
  with `PSP_FFMPEG=0`; the app's 264 unit tests. State version 10 (9 refused); decoders are made afresh after a load,
  the sound's primed with the frame decoded last.
- **The review's findings, fixed** (each with a test that fails without its fix; part 26 has the details):
  sceMpegAvcCsc's part clamp overflowed in 32 bits (a host heap over-read, or a 4 GiB row): it compares with what's
  left of the picture now. atracParse looped for ever on a chunk length that wrapped its 32-bit walk: it walks in 64
  bits, and refuses data or sample counts past 32 bits (0x80630008). The IJG's credit is in FFmpeg's notice.
  sceMpegCreate starts the Media Engine's state afresh. The runtime's limits are the loader's (pictures at most 1024
  each way, the H.264 decoder held to it; fmt extras 64 bytes). The movie's last sound frame is saved and primes its
  decoder after a load, and pictures start again from I pictures that aren't IDR ones too. build.sh checks a download
  before keeping it and tries once more, takes over a lock whose process is gone, and prints its build's name for
  CI's cache keys; Gradle has `-Pphobos.ffmpeg=OFF`. The unused decoders aac, atrac3al and atrac3pal are gone.
- **Loads mid-movie** (host): Liberty City Stories, saved at frame 1100 in its logos movie, gave the same frames as
  the run that went on from frame 1260 and the same sound; Burnout Legends' menu movie from 1440. Both the same
  with the old IDR-only rule, so the I-picture rule is checked by its test only.
- **RP6** (this build, installed over the last): Liberty City Stories' logo movies and Space Invaders Extreme's
  title screen (its music, on the host) at 60 fps, the sound's stream steady in both (no underruns after the
  start); not listened to. Force-stopped after.
- **Uncertain / next**: the decode wait (300 microseconds) is chosen, not measured; the Media Engine's colour
  rounding isn't measured; a PSP's mixer may or may not clip Burnout's title as much; MP3's first frame after a load
  loses the bits it borrowed; sceAtrac's low-level and sas modes, sceSas ATRAC3 voices, sceAac and scePsmf are left
  for a game that needs them; Snoopy's movies weren't reached in the runs.

## PSP tests: the test programs and the PSP's GE measurements in the repository — 2026-10-06

Branch `cursor/psp-test-data-2b67`, on top of `cursor/psp-hle-games4-2b67` (#151). At the owner's request, the
PSP core's built test programs (`tests/psp/programs`: hello, system, disc, gu, pspsdk's GU samples and
pspmeasure, 13 MB, built from `tools/psp-test-programs` with pspdev on 2026-10-04 and -05, the very files the tests
have run with) and their PSP's round-3 GE measurements (`tests/psp/measurements/ge-round3`, 90 files, 21 MB, from
the owner's PSP on firmware 6.61) are kept in the repository, so another machine (and CI) runs every test with no
PSP toolchain. `tests/psp/run-tests.sh` and `tests/psp/ares/run-tests.sh` use them unless `PSP_TEST_PROGRAMS` or
`PSP_GE_RESULTS` name other folders (empty: none). The VFPU's measurements (about 670 MB a round) stay with the
owner, copied where they're needed. Each folder's README says where its files come from (the programs' licenses:
pspsdk's and newlib's BSD-style ones; their SHA-256) and how to rebuild them; `.gitattributes` marks them binary;
`docs/development-process.md` records the authorization.
- **Checks:** both test scripts with no variables set, so with these folders: tests/psp 236 groups (ASan+UBSan;
  "psp measure" compares pspmeasure's GE results with these), tests/psp/ares 278 checks (the program checks run,
  none skipped).

## PSP core: the games further still — 2026-10-06

Branch `cursor/psp-hle-games4-2b67`: `cursor/psp-ge-speed-2b67` (the entry below) with `cursor/psp-fonts-2b67`
merged in (commit 53fc621b1, both sides kept, state version 8), not pushed. docs/psp-core.md, part 25, describes
it. Original code; no PPSSPP or JPCSP source read (pspsdk, pspautotests' programs and recorded results, the PSP
Developer Wiki, the games' own behavior).
- **Burnout Dominator's GE hang: not the GE's.** Part 22's runner resumed from a state saved before
  sceKernelVolatileMemLock existed; the game asks for the volatile memory as its first race loads (frame 3908), got
  NOT_YET_LINKED and no address, and laid its race out from that: textures read from 0x00f79aac, its data over its
  display lists, and from frame 5496 the GE ran vertex data as commands until a JUMP, relative to an ORIGIN among
  them, sent it to 0x03c0de64, running zeros every frame. Reproduced exactly with the function taken out of a
  scratch build (GE trace); with it, and from boot on this branch, the race plays. Test: "power volatile memory
  locked at once".
- **Space Invaders Extreme: past "NOW LOADING" into its stages.** Its background movie's thread (the best priority)
  asked for an access unit, feeding the ringbuffer, in a loop that never waits: with movies that never had any
  data, it starved every other thread. Movies are now fed and taken apart as pspautotests' video/mpeg/basic
  recorded (sceMpegRingbufferPut calls the game's callback on its thread; sceMpegGetAvcAu cuts the MPEG-2 program
  stream's video into access units with their time stamps; packets freed as taken; a picture from the second
  decoded on; nothing drawn); basic's own movie run through the HLE gave all 180 recorded frames and the callback's
  181 calls exactly. sceMpegAvcDecodeDetail answers 0 (the game waits for its first picture). Headers are still
  refused, so the games that check skip their movies as before; Burnout's callbacks give nothing, as before.
- **Where each game gets now** (frames under `/tmp/games4-runner/out`, states in `/tmp/games4-runner/states`):
  Dominator racing its first race (`dom-c`); Peace Walker past its install and the optional DATA INSTALL (NO) to
  the beach training, Snake controllable (`pw-f`, `pw-g`); LCS driving Vincenzo to the safehouse (`lcs-e`); VCS Vic
  at the army base (`vcs-d`); Sindacco in Paulie's car to Atlantic Quays (`sind-d`); Burnout Legends lap 2 of 3
  (`burnout-b`); Midnight Club 3 racing (`mc3-a`); SOCOM deployed in its first mission (`socom-d`); Snoopy's flying
  lessons (`snoopy-b`); Gunhound EX Mission 01 (`gun-b`); Brave Story's prologue (`brave-b`); Lumines Challenge
  (`lumines-c`); Space Invaders Extreme's first stage, then END GAME back to its title, its movie stopped
  (`invaders-e`, `invaders-h`). Boots of all twelve checked with the movie change (`*-boot`).
- **Checks**: `tests/psp/run-tests.sh` 236 groups with both sanitizers, no failures; `tests/psp/ares` 278 checks,
  none failed (both run on the merge too, and after the review's fixes). New groups "mpeg movie fed and taken
  apart", "mpeg movie thread waits for its picture", "mpeg ringbuffer callback states", "power volatile memory
  locked at once" (programs on both engines, state round trips), and the review's six below; "state fields" checks
  a ringbuffer's callback part way through (five refusals). State version 9 (version 8 refused).
- **Review:** a general-purpose reviewer; the clean-room check found the mpeg code independent; four low findings,
  all fixed, each with a test that failed before it. sceMpegRingbufferPut trusted the game-written ring (0x7fffffff
  packets with 0x80000000 filled overflowed, UBSan; 8192 packets or -200 filled had the callback asked for 5000 or
  4296, and a state saved as it waited was refused by a fresh machine): a ring that couldn't be one is given
  nothing now, sceMpegGetAvcAu's guard ("mpeg ringbuffer that isn't one given nothing"). The callback's $gp came
  from ring offset 44, past pspsdk's 44-byte SceMpegRingbuffer: Put's caller's now ("mpeg ringbuffer callback with
  the caller's global pointer"). Missing groups added: a callback giving more than asked (clamped), one returning
  an error (Put returns what came before), a feeder terminated asleep in its callback (state round trip), seeded
  random packs through sceMpegGetAvcAu (read below the packets, filled within them); taking out the clamp, the
  `gave <= 0` check, or either mpegAbandoned call fails one (deleteThread's only via a loaded state in which a
  dormant thread still has a feeding: every live thread is ended before it's deleted). The counts here (227 and
  268) were out of date. State layout unchanged.
- **Speed** (host Mac, scratch runner, 600 frames of play, GE on 7 threads / 1): Gunhound 213 / 138 fps, Dominator
  193 / 104, Brave Story 168 / 94, Lumines 117 / 44, Burnout Legends 97 / 54, VCS 86 / 40, LCS 78 / 31, Sindacco
  73 / 35, Snoopy 61 / 38, Peace Walker 60 / 33, Midnight Club 3 7.3 / 2.1. Midnight Club 3 racing at night is
  bound by filling pixels (its 32-bit framebuffer's triangle and sprite loops take nearly all of seven threads):
  part 24's SIMD and setup work is where its time is.
- **Uncertain / next**: lines (PRIM 1, 2) aren't drawn (LCS's route, SIE, SOCOM, Dominator): needs a measurement;
  bounding boxes all in sight; Dominator's race start shows torn dark bands a moment (a bloom pass reading the
  framebuffer it writes); sceKernelDevkitVersion missing (Peace Walker carries on); Peace Walker's YES to the
  optional install not followed through; movie headers still refused, ATRAC3plus access units never come, and
  sceMpegAvcDecodeStop doesn't hand back the held picture. The codecs (FFmpeg's LGPL decoders, the owner's choice)
  would make movies seen: the access units are there now.

## PSP core: the CI's PSP system tests made green — 2026-10-06

Branch `cursor/psp-ge-speed-2b67` (#150). The workflow `.github/workflows/psp-core.yml` runs `tests/psp/run-tests.sh`
(the whole test built with `-Wall -Wextra -Werror` and the address and undefined sanitizers, then run); its PSP
system tests job failed (run 37483407156, exit 1). Two defects in the tests' own code, found one after the other:
- **What:** `tests/psp/audio.cpp:695` printed the `s64` pair (nall's `int64_t`, a `long int` on this host) with
  `%lld`, which the compiler's `-Werror=format=` rejects; fixed with `static_cast<long long>`, the repo's own
  precedent (`ares/n64/vulkan/parallel-rdp/vulkan/query_pool.cpp:212`). `tests/psp/disc-image.hpp:121` copied every
  file with `std::memcpy` from `file.data.data()`, null for an empty file (the test disc's `EMPTY.BIN`), which UBSan
  flags as UB; the no-op copy is now skipped.
- **Checks:** `tests/psp/run-tests.sh` with both sanitizers: 226 groups, 0 failures (before: the build failed at
  `audio.cpp:695`, then the binary aborted at `disc-image.hpp:121`).
- **Then:** the same two lines sat in the heads of the open PSP PRs #140-#151 (the memcpy line from #140 on, the
  `%lld` line from #147 on). Each fix was made where it first appears (#140's memcpy, #147's `%lld`, the same
  change as here) and every stacked branch merged the one below it, up to #151, so each PR's head carries both.

## PSP core: the GE made fast, every pixel the same — 2026-10-06

Branch `cursor/psp-ge-speed-2b67`, on top of `cursor/psp-hle-games3-2b67`, with `cursor/psp-fonts-2b67` (#149, the
entry below) merged underneath, not pushed. docs/psp-core.md, part 24, describes it (part 23 is the fonts'); the
owner's decision (the software renderer as fast as possible and exactly as accurate, the reference; Vulkan and
OpenGL renderers later) is in its Decisions. Original code; no PPSSPP or JPCSP source read.
- **What:** textures kept decoded, dropped the moment their memory changes (the GE watches their pages; the CPU's
  compiled stores keep off watched pages); primitives set up once as jobs and drawn row by row with the very same
  arithmetic; drawn in bands of 8 rows by several threads, each band in the list's order (option "GE Threads": 0,
  the default, one fewer than the host's cores; 1, as before); in 2D, regions and texture rows exact; a leaner pixel
  pipeline, texture function and filter; drawing going on while the CPU runs and the next batch is set up, nobody
  seeing it half drawn (`Memory::pointer()`, states and power wait for the VRAM pages it uses, the CPU's page tables
  lose them meanwhile); cheaper vertex reads and screen picture.
- **Speed** (host M1, the Android build's flags, 300 frames a scene, before -> after with 7 threads): GTA LCS in the
  city 20.4 -> 83.4 fps (4.1x), Peace Walker's title 13.5 -> 129.8 (9.6x), Lumines' demo 31.3 -> 192.8 (6.2x),
  Burnout Legends racing 26.8 -> 85.9 (3.2x), Midnight Club 3 racing 6.2 -> 27.9 (4.5x), Gunhound EX 123 -> 229
  (1.9x); single-threaded 1.4-2.1x.
- **Same pixels:** every frame's picture and VRAM (and RAM at the end) in the six scenes identical to the old core's
  with 1, 2, 4, 7 and 8 threads and on the interpreter; a scratch differential fuzzer against the old GE (thousands
  of random lists, every setting; 1, 4 and 8 threads; with the address, undefined-behavior and thread sanitizers);
  the parts' 213 groups (the PSP's measured results unchanged) with both sanitizers and under ThreadSanitizer, the
  ares system's 254 checks. New groups "draw textures kept decoded" and "ge drawn on several threads"; 16 broken
  versions each failed them.
- **Peace Walker's stripes:** not the core's (an NDK build in a Linux arm64 container draws every frame as the host
  does), but the presentation: the app handed Android the 480x272 frame and the compositor scaled it 3.97 times,
  bilinearly, the title's one-pixel lines becoming soft bands of drifting strength. The PSP's picture now goes into
  the window at the whole multiple of its size nearest the view's (4 sideways: 1920x1088, scaled 0.993 by the
  compositor; 2 upright; exact with Integer Scaled), each line sharp and even ("sharp bilinear"). Screenshots
  outside the repository: `/tmp/gefix-device/pw-before.png`, `pw-after-25s.png`, crops in `crops/`.
- **Review:** a general-purpose reviewer; the clean-room spot check found the new code original; one medium and four
  low findings, all fixed, each with a test that failed before; a pre-existing fill-rule function rewritten
  clean-room. Medium: a quarter-turned 2D sprite's first column could take texture rows outside the decoded copy (a
  heap read past it, 24 pixels unlike memory's); each turned pair now widens the rows reached, fetch() asserts in
  debug builds, and a new group compares 4000 random primitives drawn with textures kept decoded against one that
  decodes nothing (thread counts can't catch cache bugs). Lows: the screen's picture formed a pointer from null
  (VRAM's second copy); the cache keyed on rows reached (now one copy a texture, grown as needed, unused-longest
  dropped first from a list); "GE Threads" unbounded (now at most twice the cores and 64); the renderers' decision
  twice in the docs, and a claim about drawing over one's own texture that wasn't measured. The fill rule
  (rightOrBottom(), with a truncating division) is now the top-left convention on the edge functions' directions,
  exact: the comparison with the owner's PSP unchanged line for line; GTA, Burnout and Midnight Club differ by 1-4
  pixels in some frames (slivers' right edges the old division misjudged and drew twice), the other scenes
  identical; kept.
- **Merged with #149 (the fonts):** only system.cpp's options and the docs clashed; states stay version 8.
- **In the app:** Settings, Emulation, Performance, "PSP Drawing Threads" (Auto, the owner's default: all cores but
  one; or 1, 2, 4, 6, 8), handed to the core as "GE Threads" as a game loads.
- **On the RP6** (installed over the app, data kept; overlay after about 40 s; force-stopped): Peace Walker's title
  10.6 -> 60.0 fps, GTA LCS 16.6 -> 60.0, Lumines' demo 24.0 -> 59.9, Burnout Legends' title 60 -> 60 (before: the
  fonts' build without part 24; before tonight's work, GTA 17.7, Peace Walker 9.5, Lumines about 40).
- **Checks:** tests/psp 226 groups with both sanitizers and under ThreadSanitizer, tests/psp/ares 274 checks, the
  app's 262 unit tests and its release build.
- **Next:** setting up for less and drawing bands as primitives arrive (the GE's thread is GTA's and Burnout's
  longest path now), SIMD four pixels at a time, the GPU renderers.

## PSP core: the system fonts — 2026-10-06

Branch `cursor/psp-fonts-2b67`, on top of `cursor/psp-hle-games3-2b67` (the entry below), not pushed.
docs/psp-core.md, part 23, describes it. sceLibFont draws the PSP's own fonts (the PGF files of flash0:/font) where
part 20's stand-in found none, so games that print with them show their text. Written from the owner's own fonts
(read field by field with scratch scripts) and pspautotests' font programs and the results they recorded on a PSP;
no other emulator's code read (PPSSPP's and JPCSP's PGF readers weren't opened).
- **How the user gives Phobos the fonts**: they're Sony's firmware, never in the repository, the APK, a commit or
  a backup. Copy them from your own PSP with `tools/psp-flash0-dump` (it leaves `PSP/GAME/FLASH0DUMP/flash0/` on
  the memory stick) and put the `FLASH0DUMP` folder in the device's Download folder: while the app has fewer than
  the eighteen, it finds them there by itself (at start, before a PSP game loads, and as Settings, Firmware opens)
  and copies those it hasn't got, where Android lets it read there (a folder grant that covers it, or a device that
  allows it, as the RP6 does). Otherwise, or from any other folder: Settings, Firmware, "PSP fonts (from your PSP's
  flash0)", and pick `flash0/font` (or `flash0`, or the `FLASH0DUMP` folder: the font folder is found inside).
  Either way only the eighteen by name (any case), 1 byte to 4 MiB each, go into the app's own files
  (`firmware/PlayStation Portable/font`), and the row says how many are there and whether they were found by
  themselves ("found in Download/FLASH0DUMP") or picked ("copied"); picking again replaces them. The runner hands
  that folder to the core as a PSP game loads (core option "Fonts"); with none there the library is the stand-in.
  Android's backups leave the app's `firmware/` folder out, so after a restore the fonts are imported again
  (by themselves if they're still in Download/FLASH0DUMP and readable there).
- **What's there**: a PGF reader (`ares/psp/kernel/pgf.cpp`: header, tables, character maps, glyphs, run-length
  pictures by rows and columns, shadows, the Korean font's composites; every offset and size checked against the
  file, a damaged glyph missing on its own); the library (`font.cpp`): NewLib/DoneLib, the font list, FindOptimumFont
  and FindFont, Open, OpenUserFile, OpenUserMemory, Close, GetFontInfo(ByIndexNumber), character and shadow info,
  image rectangles and glyph images (whole and clipped, the 4- and 8-bit formats drawing, fractions of a pixel across
  shared as recorded), the alternative character, SetResolution and the four conversions. The game's alloc and free
  are called for every block, in the sizes and order recorded, the calling thread running them as callbacks run.
- **States**: version 8 (1 to 7 refused): libraries, open fonts and calls part way through saved and checked;
  fonts read again from where they came from on loading, the state refused if one is missing or differs.
- **On the host** with the owner's fonts (frames in `/tmp/fonts-runner/final`, not in the repository): Gunhound EX's
  save notice in Japanese (a black screen before); Peace Walker's "Checking Memory Stick™", its MSF disclaimer, its
  player's name ("Is this name OK?"), control scheme, BUTTON CONFIG, DATA INSTALL and "Installing... (Progress: N%)",
  all of which had shown none of their words.
- **Checks**: `tests/psp/run-tests.sh` 221 groups with both sanitizers (ten new: "pgf read back", "pgf damaged",
  "fonts memory", "fonts found", "fonts measured", "fonts drawn", "fonts of the program's own", "fonts resolution",
  "fonts states", "fonts folder"; "state fields" refuses seventeen more states); `tests/psp/ares` 264 checks (the
  option; a version 7 state refused); the app's unit tests (251, `PspFontsTest` new) and the modern release build.
  Twelve broken versions each failed a test. On the RP6: the build installed over the app (data kept); the new row,
  given `Download/FLASH0DUMP`, found `flash0/font` and copied the eighteen ("18 of 18 fonts copied", Complete). No
  game was started there (quitting would have written its auto state).
- **Review:** a general-purpose reviewer; the clean-room spot check found `pgf.cpp` and `font.cpp` independent; one
  medium and four low findings, all fixed, each with a test that failed before it. Medium: a state could say a font
  of the game's memory was read whole (mode 1), which sceFontOpenUserMemory never makes, and a call part way through
  opening one then ended as a whole file's open, writing through a block it never asked for (the address
  sanitizer's heap overflow); refused now, and an open's ending asks where the font came from before its mode.
  Lows: a game's font file opened by a path relative to its working folder was kept as given and read again on
  loading before the working folder was back, so a fresh session refused the state (kept whole now); a font in
  memory given a length past its end (-1) was copied to the end of RAM at every open and state load (8 MiB at most
  now, and one open at that address shared without reading memory again); the picker copied any .pgf of any size,
  and one unreadable file stopped the rest and was reported as none found (the eighteen by name, 1 byte to 4 MiB,
  each failure kept to its file, its part copy removed, and named apart from none found; the core skips a font
  file over 4 MiB); Android's backups took the copies (the firmware folder is left out now: `backup_rules.xml`,
  `data_extraction_rules.xml`, cloud and device transfer). The state's layout didn't change: still version 8.
  Then the owner's choice of the same night: the fonts found by themselves in Download/FLASH0DUMP, as above.
- **Checks after the review**: `tests/psp/run-tests.sh` 221 groups with both sanitizers (checks added to "fonts of
  the program's own", "fonts states" and "fonts folder"; "state fields" refuses eighteen states); `tests/psp/ares`
  264 checks; the app's unit tests 259 (`PspFontsTest` 15: the eighteen by name and size, failures kept apart and
  reported, and the dump found by itself in fake storage, only missing fonts copied, nothing read with all eighteen
  or from a folder the app can't read) and the modern release build. On the RP6 (installed over the app, data
  kept; the picker's grant on the dump's folder gone with the update): at start "all 18 here, none looked for"; as
  Settings, Firmware opened, the row "18 of 18 fonts copied", Complete, and the dump's folder found by itself and
  readable without any grant ("18 in /storage/emulated/0/Download/FLASH0DUMP/flash0/font, readable": the app has no
  storage permission and the files are a file manager's, so the RP6's own Android allows it). The automatic copy
  itself wasn't seen there, since the app already had all eighteen and nothing may delete them on the device; the
  host tests cover it. No game was started.
- **Uncertain**: shadow flags reported raw (the PSP's differ: their meaning isn't known); kr0's country; a game's
  font file opened a piece at a time is read through the kernel's files, not the game's callbacks; drawing borrows
  no memory from the game; the order of a few frees; see part 23's list. Whether other devices let the app read
  Download/FLASH0DUMP without a grant as the RP6 does (stock Android doesn't, for another app's files).
- **Next**: try more games that use the system fonts; Peace Walker's and Gunhound's text on the device. Then, as
  the owner chose, after the GE's speed (another branch's): the stuck games further (Dominator's GE hang, Peace
  Walker after its install, the GTAs into play), then the codecs (FFmpeg's LGPL decoders), then the Vulkan and
  OpenGL renderers (docs/psp-core.md, Decisions).

## PSP core: the games, further — 2026-10-06

Branch `cursor/psp-hle-games3-2b67`, on top of `cursor/psp-hle-games2-2b67` (#146) and, merged since, of
`cursor/psp-sound-2b67` (#147, the entry below, itself on #146), not pushed. docs/psp-core.md, part 22, describes it
(part 21, the sound worker's, describes #147; part 22's end, the merge). The stalls part 20 couldn't explain, traced
with the scratch host runner (never committed; now with write and read watchpoints, symbols for llvm-objdump over
memory dumps, saved states at menus, the stick, a memory stick folder per run) and fixed from pspautotests'
recorded results and the games' behavior; no other emulator's code read:
- **GTA LCS, VCS, Sindacco at 85%**: synchronous sceIoRead took no time, so GTA's streaming thread (0x20) finished a
  request and called its callback before the main thread (0x38) had noted it, and the callback dropped it as
  cancelled. Synchronous reads and writes now wait their device's time (part 20's rates), refused where a thread
  can't wait, as intr/waits recorded.
- **Brave Story on its logo**: its sound thread (0x10) took the CPU while the game held interrupts off, found every
  blocking output refused, and spun at the top priority. With interrupts held off nothing now takes the CPU and
  every wait refuses (intr/waits, intr/mfic); a thread taking the CPU has interrupts on.
- Then what the games asked for next: the keyboard accepts a name (an empty field gets "PSP": Peace Walker),
  sceIoRename keeps the file in its folder (io/file/rename: Peace Walker's install), sceRtcGetWin32FileTime (Midnight
  Club 3), sceRtcSetTick (Peace Walker), sceKernelVolatileMemLock (Dominator), and delays as long as a PSP's (205
  microseconds at least, plus 25: delaylen; Brave Story's 1-microsecond polls cost the host 25% of its time).
- **On the host** (frames in `/tmp/hle3-runner/final`, states in `/tmp/hle3-runner/states`, not in the repository):
  GTA LCS and VCS play their openings in the city, Sindacco is in its first mission; Brave Story names its heroes and
  plays its prologue; Gunhound EX starts Mission 01; Snoopy is in its flying lesson; SOCOM saves a profile and deploys
  into its first mission; Burnout Legends and Midnight Club 3 race; Burnout Dominator's first race loads, then its GE
  runs off a display list's end (to look into); Peace Walker gets its name, button configuration and data install,
  whose menus need the system's fonts. Peace Walker's title is GE-bound (11.5 fps here, drawing correctly: the
  handheld's 9.5 fps is the software GE's per-pixel cost; its garbling there doesn't show on the host).
- **Checks**: the parts' 196 groups with both sanitizers (eight new: files synchronous reads wait and wait state,
  interrupts held off keep the CPU, utility keyboard, files rename, rtc file times and ticks, power volatile memory
  waited for, kernel thread delays' lengths; "state fields" refuses nine more states); `tests/psp/ares` 236. Broken
  versions failed the new groups. States stayed version 5 then (two new waits, no new fields); see the review.
- **Review:** a general-purpose reviewer; the clean-room spot check found every area independent; one medium and
  five low findings, all fixed, each with a test that failed before it. Medium: a thread holding interrupts (or
  dispatching) off lost the CPU when it rotated its own line or changed its own priority (both made it ready, and
  the switch turned interrupts on); it keeps the CPU now, giving way once they're back. Lows: a handler that left
  interrupts off passed that on to the thread it interrupted (back on as it returns); the keyboard's limit of 0 cut
  the nickname to nothing and a roomless field got a NUL (0 is no limit; nothing written); sceIoIoctl's disc reads
  took no time (refused and timed as sceIoRead's); the interrupt flag's meaning changed under state version 5
  (version 6, older refused, the flag checked); mfic and mtic used a flag apart from the kernel's, reading 0 at the
  start where intr/mfic recorded 1 (one flag now, the CPU's, on at power, mtic's lowest bit alone;
  sceKernelCpuResumeIntrWithSync listed as sceKernelCpuResumeIntr). New groups "interrupts back on after a handler"
  and "interrupts one flag, mfic's and the kernel's"; four groups grown. 198 groups, `tests/psp/ares` 240.
- **Merged with #147 (sound)**, which this branch now sits on: only the state's version (7, each side having made a
  6 of its own), the ares test's list of refused versions and the docs clashed; part 21 comes before part 22 in
  docs/psp-core.md, this entry before sound's. Then `__sceSasCore` (and `__sceSasCoreWithMix`) refuse where no thread
  may wait, as intr/delays recorded. Checks: 211 groups with both sanitizers, `tests/psp/ares` 254; states are
  version 7, versions 1-6 refused. On the host from boot (scratch runner, frames and WAVs in
  `/tmp/stallfix-runner/out`): GTA LCS reaches the city, Brave Story its intro, Burnout Legends its title, as before,
  all silent there (ATRAC music, no voice keyed on); the Street Fighter III port sounds as part 21's capture, 4 s
  later (its 4.4 MB of boot reads now take the disc's 3.2 s).
- **Next**: Dominator's GE path; sceLibFont from flash0 (Peace Walker's and Gunhound's menus); wait timeouts as
  waittimeouts recorded; a thread started with dispatching held off running at once (dispatchwake); the GE's speed.

## PSP core: sound — 2026-10-06

Branch `cursor/psp-sound-2b67`, on top of `cursor/psp-hle-games2-2b67` (the entry below). Games are heard:
sceAudio's eight mixer channels and its SRC channel are added up into the system's 44.1 kHz stream where the DMA
timing model hears them (volumes over 0x8000, mono on both sides, the SRC rates converted by linear interpolation,
sums clamped to 16 bits), and sceSasCore's VAG and PCM voices sound: the SPU's ADPCM decoded, pitches interpolated,
the recorded envelope, volumes and dry/wet sums, written in stereo or multichannel's four planes, or mixed into the
game's buffer. Written from pspsdk, pspautotests' audio/sascore programs and their recorded results (reproduced
sample for sample), psx-spx and part 17's specification; no PPSSPP or JPCSP code read. docs/psp-core.md, part 21,
describes it. Reverb is a pass-through (no reverb added); noise, waves and sas's ATRAC3 voices stay silent.
- **Found in the recordings**: VAG's guess rounds down (no half added, unlike psx-spx's SPU); filters 5-15 read past
  the PSP's table; VAG samples are heard a sample late, PCM's at their place; multichannel is four planes;
  __sceSasCoreWithMix scales the buffer by its volumes and refuses multichannel (0x80000004); dry is on from
  __sceSasInit; and end marks are whole bytes (music.vag's header flags 0x41/0x75 play on), which changed part 20's
  `& 7` reading (0x41 and 0x87 no longer end a voice).
- **On the host** (a scratch runner writing the stream to WAVs, never committed; 40 s each, measured per second):
  Lumines' demo stage from 25 s (sas effects and a music voice, -18 to -34 dBFS, peak 19255); the Street Fighter III
  port from the start (four sceAudio channels, -17.5 to -31 dBFS, peak 23517, 44.1 kHz pacing); Space Invaders
  Extreme's effects (sas into the SRC channel, peak 7999); Burnout Legends and SOCOM silent at their titles (ATRAC3+
  music) and their menus' sas effects after Start (peaks 8006, 9979). Nothing clipped in any; sound added no
  measurable time (Lumines' 2400 frames in 48-50 s either way).
- **States**: version 6 (the output not yet taken, the SRC channel's place, VAG decoders), each checked on loading;
  versions 1-5 refused.
- **Checks**: the parts' 197 groups with both sanitizers (nine new: "audio mixed", "audio src heard", "audio output
  in states", "sas vag as recorded", "sas vag decoded", "sas pcm heard", "sas output modes", "sas voices mixed", "sas
  voices in states"; "state fields" with the new fields and 7 more refusals); `tests/psp/ares` 250 checks (a
  version 5 state refused; the stream heard: a mixer buffer sample for sample, 735.7 frames a frame). Eleven broken
  versions each failed (VAG's guess with a half added, part 20's `& 7`, no VAG lag, the envelope after its step, dry
  off, multichannel interleaved, the mixer's volume rounded towards zero, no clamp, the SRC channel's nearest sample,
  its place left out of states, the old silent stream).
- **Review:** a general-purpose reviewer; the clean-room spot check found the code independent; one medium and one
  low finding, both fixed, each with a test that failed before it. Medium: an SRC buffer armed once the slots had
  freed, while the last one's final 100 µs were still to be heard, was timed from the moment it was armed but heard
  after them, so a program draining the channel before each buffer drifted 4.4 frames ahead per buffer: silence for
  good from 21.9 s (1024 samples at 44.1 kHz) and states the loader refused. Such a buffer is now timed from where
  it's heard (it retires a whole buffer after the last one's end); the output's ring never moves its first frame not
  taken past the clock (what doesn't fit is left out at the far end); loading wants the SRC channel 7 frames ahead at
  most. Part 17's output2 results still hold ("audio src rest" and "audio draining" now pause a millisecond where
  the PSP's own runs print lines); the usual two-buffer stream is unchanged (same frames and return times at eight
  sizes and rates), and the 40 s captures of Space Invaders Extreme (SRC channel), the Street Fighter III port and
  Lumines are bit-identical to the old code's. Low: no test pinned the standard VAG ending (a block marked 1, then
  `00 07 77 77...`). New groups: "audio src drained between buffers" (30 s at 1024/44.1 kHz and 4096/48 kHz: lead
  bounded, every frame taken and as the buffers make it, states round-trip), "audio output kept from a channel
  ahead", "sas vag endings" (the flag-1 ending, 3 ending a voice that doesn't loop, a loop back to a 4); "state
  fields" tries the new bounds. Checks: 200 groups, `tests/psp/ares` 250; the state's layout is unchanged (version 6).
- **Next**: an ATRAC3+ decoder (Burnout's, SOCOM's and Invaders' music), sas reverb, noise and waves; the RP6 run.

## PSP core: more functions games ask for — 2026-10-05

Branch `cursor/psp-hle-games2-2b67`, on top of `cursor/psp-hle-games-2b67` (#145, the entry below). What the RP6 run
had the owner's games asking for, written from pspsdk's headers, pspautotests' tests and their recorded results, and
the games' behavior on a scratch host runner (never committed); no other emulator's code read. docs/psp-core.md,
part 20, describes it: files' asynchronous requests (done after the time the UMD drive or the memory stick takes,
polled or waited for, CB waits running callbacks), a silent sceSasCore (voices' envelopes and ends as recorded, no
mixing), message pipes and mailboxes, sceMpeg set up but finding no movie it can play (games skip them),
sceAtrac3plus refusing every stream, the network libraries with the WLAN switch off, sceLibFont with no fonts
installed, and sceRtc, sceOpenPSID, display, thread, Kernel_Library, power and utility odds and ends. Peace Walker
found two faults: a free lightweight mutex's CB lock ran a callback (it never enters the kernel on a PSP), and
threads' kernel area at the top of the stack wasn't zeroed. States are version 5.
- **On the host** (1800-2400 frames each; frames in `/tmp/hle2-runner/out`, not in the repository): Lumines to its
  gameplay demo; Burnout Legends, Burnout Dominator and Midnight Club 3 to their titles and, with Start, their
  profile menus; SOCOM past its "no data" screen to its credits; Snoopy to its "no save file" warning (it had
  exited); Space Invaders Extreme to its title (first time); Peace Walker to its title screen (no system-font text);
  GTA Liberty City Stories, Vice City Stories and Sindacco Chronicles past their movies to their loading screens,
  where they stop at about 85% (the main thread waits for a queue to drain, polling an event flag: not traced
  further); Gunhound EX past its logo to a black screen (likely its system-font text); Brave Story on its Game
  Republic logo, reading slowly; the Street Fighter III port as before.
- **Next**: what GTA's loading waits for; Brave Story's logo; sceLibFont from the owner's flash0 fonts; mixing
  sceSas and sceAudio into the speakers; ATRAC3plus and the movies' decoders.
- **Checks**: the parts' 180 groups with both sanitizers (new files: async, sas, messages, media; "state fields"
  changing every new field and refusing 36 more states); `tests/psp/ares` 236 checks (a version 4 state refused).
  Broken versions each failed their tests (requests done at once; a sender out of line; transfers' counts unwritten;
  sceSas's 32-sample start dropped; the kernel area left as 0xff).
- **Review:** a general-purpose reviewer; the clean-room spot check found every file independent; three medium and
  six low findings, all fixed (each with a test that failed before it) or recorded. Medium: a pipe without a buffer
  copied direct transfers through a host buffer sized by the guest's counts before any address check (1 GiB moved on
  a 32 MiB machine): messages and buffers need memory behind them all (ILLEGAL_ADDR), bytes go memory to memory, and
  loading checks each pipe waiter's rest; a send or receive with callbacks ran the callbacks of the higher-priority
  thread it woke: the caller's start first now; __sceSasSetVoicePCM left a voice past fewer samples, the machine's
  own state refused: it's brought inside them. Low: two threads waiting on one request, or a CB wait whose callback
  polled the result, left a thread waiting for good: every waiter ends now (the first to wait with the result, the
  rest NOASYNC), and loading checks only threads really waiting; sceIoIoctlAsync timed by the output's length (due
  in 52 minutes): by the bytes moved now; sceFontSetResolution kept 1e10 and infinities: refused now, NaN too
  (INVALID_VALUE); with dispatching held off, waits were refused only after a lightweight mutex's waiter count or a
  pipe's bytes changed, and rotating switched threads: waits are refused first (as pspautotests' intr/waits
  recorded), rotating keeps the CPU; a lightweight mutex deleted while its waiter's callbacks ran left the waiter for
  good: its wait ends deleted. Recorded, not changed: which of pipe attributes 0x100 and 0x1000 orders senders
  (pspsdk names neither). Every group of this branch now ends with a save, load and save giving the same state.
  Checks: 188 groups (eight new), `tests/psp/ares` 236; the state's layout is unchanged (version 5).
- **On the RP6** (build 104649): Space Invaders Extreme reaches its title screen at 60 fps; Peace Walker reaches its
  title scene but drawn garbled (striped) and at 9.5 fps (16%), a GE drawing and speed problem to look into; Burnout
  Legends, Midnight Club 3, SOCOM and Lumines ran with no missing functions noted.

## PSP core: the functions retail games ask for — 2026-10-05

Branch `cursor/psp-hle-games-2b67`, on top of `cursor/psp-retail-load-2b67` (two entries below), and since then of
#144 (`cursor/psp-decrypt-2b67`, the entry below), merged in. The HLE kernel gains
what Lumines, Space Invaders Extreme, Brave Story, GTA: Sindacco Chronicles and the Street Fighter III port asked for
next, found with a scratch host runner (never committed) that boots a CHD, traces every system call and dumps frames:
SDK version setters (explicit NIDs for those whose NIDs aren't their names' hashes), aligned partition blocks (the
C++ abort and the "can't allocate memory"), the 24 MiB user partition the emulated 64 MiB PSP-2000/3000 gives a
program unless PARAM.SFO's MEMSIZE is 1, callbacks that run in *CB waits, the vertical blank's functions and interrupt
handlers, sound output's channels with the driver's timing (silence for now), power, memory pools, the system's
dialogs with real saves on the memory stick, optional modules, and thread and clock odds and ends; all in save
states. docs/psp-core.md, part 17, describes it and what each game does now.
- **Checks:** the parts' 123 groups with both sanitizers (new files: callbacks, power, audio, utility, pools);
  `tests/psp/ares` 195 checks. A broken version (waits not resumed after callbacks) failed 18 checks.
- **Review fixes:** a save's game, save and data file names were joined to the host's paths unchecked (only climbing
  above the memory stick was stopped): a reviewer read /etc/hosts into the program's memory, and a delete with the
  game "" took every save, with ".." PSP/ and its homebrew, with "../.." the memory stick folder itself. Each name
  must now be one plain name, the save's folder one of SAVEDATA's own and its file one of the folder's, links
  followed; the buffer must be in the program's memory; ERASE takes the data file alone; the sizes and list modes no
  longer throw. Pools loaded from a state are checked (a fixed pool with blocks of 0 bytes divided by zero on a
  free, which is guarded too); a CB wait that needn't wait runs the callbacks already notified; states are version
  2; the docs' user partition is the PSP-2000/3000's; pools.cpp cites pspautotests and the RTC dates credit Howard
  Hinnant's algorithm. Each fix has a test that failed before it (the parts' groups: 128; `tests/psp/ares`: 199).
- **On the host:** Lumines reaches its title screen and menu (and writes its save); the Street Fighter III port its
  title screen at 60 frames a second; Space Invaders Extreme and GTA wait on the module manager (sceKernelLoadModule,
  sceKernelLoadModuleByID: another branch's work); Brave Story stops after sceSas fails. Next: sceSas, asynchronous
  file functions, message pipes and mailboxes. Not checked on the RP6 then (build 104648's results are below).
- **Merged with #144** (the entry below), which now sits under this branch: states are version 3 (each branch's
  version 2 had a layout of its own; both, and version 1, are refused by the header).
  `sceKernelStopUnloadSelfModuleWithStatus` calls `unloadSelf(arg(0), arg(1), arg(2), arg(4))`, so a module calling
  it unloads itself and the game goes on (the program calling it still ends), and `sceKernelTerminateThread` deletes
  a module's module_start or module_stop thread as `sceKernelExitThread` does. Also: callbacks return to the
  trampoline's fourth syscall (#144 took the third), #144's thread deletion is this branch's `deleteThread()`, and
  states refuse a module whose block is a pool's. Checks: 158 groups (new: "modules unload with a status",
  "modules terminated threads"), `tests/psp/ares` 228; part 17's last paragraph has the rest.
- **Review:** a general-purpose reviewer of the merged branch: one medium and five low findings, all fixed, each
  in its own commit with a test that failed before it; the merge, the clean-room code and the savedata fixes checked
  out. Medium: states took thread stacks unchecked, and sceKernelGetThreadStackFreeSize copied the whole stack per
  call (a state's 0xfffff000-byte stack would have cost 4 GiB): the stack is read in place, and loading wants each
  stack a block of its own, of its size, and every block inside the user partition. Low: a pool's or semaphore's
  waiter leaving unserved (timeout, termination) now has those behind it served (`waiterLeft()`); a vertical blank
  stays pending while the last one's handlers are queued or running (two 20 ms handlers had 97 calls queued by
  frame 120, saved in states); every block one owner at most among threads' stacks, pools, modules and the program
  (two pools on a block, or a pool in a stack or the program's block, loaded); the SRC channel's first buffer from
  idle retires 100 µs short of its length, as pspautotests' audio/output2/rest recorded ("13XX µs", where this read
  1452); sceKernelPrintf cuts a width to the field's room before snprintf (two fields 400,000,000 wide took 129 ms).
  sceKernelFreePartitionMemory refuses blocks the kernel holds, so the machine's own states stay loadable. States
  are version 4 (each call into the program marks a vertical blank's handler). Checks: 162 groups (new: "kernel
  semaphores served past waiters that left", "pools served past waiters that left", "interrupts handlers longer
  than a frame", "audio src rest"), `tests/psp/ares` 232 (a version 3 state refused). Found on the way, not changed:
  a program spinning on the clock sees it move only between the CPU's runs (each to the next thing due).
- **On the RP6** (the whole stack, launched from the user's CHDs): all ten of the user's priority games now start
  (nine tried with build 104648, decrypting their programs; Lumines with build 104647). SOCOM Fireteam Bravo reaches
  its own "No SOCOM Fireteam Bravo Data was found on the Memory Stick Duo" screen at 60 fps (then asks for
  sceAtrac3plus); Burnout Legends reaches its LOADING screen (asks for sceIoChangeAsyncPriority, sceIoPollAsync,
  sceIoReadAsync); Lumines runs to its log-in menus at about 40 fps (asks for sceSasCore); GTA Vice City Stories and
  Liberty City Stories, and Midnight Club 3, ask for sceMpeg (video) and async I/O; Peace Walker asks for sceRtc
  e7c27d1b, sceOpenPSID, sceDisplay 210eab3a and message pipes (ThreadManForUser 7c0dc2a0/74829b76); Burnout Dominator
  and Snoopy ask for async I/O and ad-hoc networking (sceNet*); Gunhound EX asks for sceLibFont and scePower 469989ad.
  Next: async I/O, a silent sceSas, message pipes, sceMpeg stubs that let games skip videos, networking reported off.

## PSP core: retail programs decrypted, and modules loaded — 2026-10-05

Branch `cursor/psp-decrypt-2b67`, stacked on `cursor/psp-retail-load-2b67` (commit `08b08ab64`; for stack #106),
worked in its own worktree because another worker had uncommitted changes in the main checkout. Shop-bought games'
programs are decrypted: AES-128 (`ares/psp/kernel/aes.cpp`), the KIRK engine's commands 1, 7 and 0xb
(`kirk.cpp`, SHA-1 moved there from `Kernel::nid()`), the published keys and the table of tags (`keys.cpp`), and the
`~PSP` format's types 0, 1, 2, 5 and 6 with gzip unpacking (`decrypt.cpp`), written from docs/psp-core.md's new part
18. The loader decrypts before reading an ELF, and a disc's EBOOT.BIN starts decrypted, BOOT.BIN only when it can't
(the reason reported). Modules load from the disc and the memory stick (`modules.cpp`, part 19: ModuleMgrForUser's
load, load by ID, start, stop, unload, the IDs and information), linked to each other's exports and the program's,
module_start run on its own thread; Sony's modules that games carry are stood in for by the HLE kernel. The state's
version is now 2.
- **Checks:** the parts' 118 groups with both sanitizers (new: five "crypto", five "decrypt", ten "modules"; "state
  fields" and "loader refusals" extended); `tests/psp/ares` (220 checks: the disc program encrypted as EBOOT.BIN
  boots and reads its disc; undecryptable ones report why, and a plain BOOT.BIN starts instead). Broken versions each
  failed a test (listed in parts 18 and 19).
- **Review:** a general-purpose reviewer found the code an independent implementation in a clean-room audit, plus two
  medium and five low findings, all fixed (parts 18 and 19 say how; each fix's test failed before it):
  `sceKernelLoadModuleByID` refuses a ~PSP size under 0x150 or over 64 MiB, and reads of an open file ask the host
  for no more than the file has left (a 4 KiB file had asked for 4 GiB); a module unloading itself ends its calling
  thread, runs its module_stop and goes, the program running on (it ended the game); the program's exports are
  offered to modules' imports; a module without a module_start runs its entry point, and modules' own thread
  parameters are read; a module_start or module_stop ending in `sceKernelExitThread` has its thread deleted; states'
  modules are checked (thread, block, ID); the keys' SHA-1 digests are pinned in `tests/psp/crypto.cpp`. The
  self-unloading is `Kernel::unloadSelf(exit status, argument size, argument, options)`: when
  `cursor/psp-hle-games-2b67` merges, its `sceKernelStopUnloadSelfModuleWithStatus` (0x8f2df740, which Gunhound EX
  calls) should call it too, `unloadSelf(arg(0), arg(1), arg(2), arg(4))`, rather than end the program, and its
  `sceKernelTerminateThread` should delete a thread `madeForModule()` as `sceKernelExitThread` now does.
- **Against the user's games** (CHDs pulled read-only from the RP6 to /tmp on the Mac; nothing kept or committed):
  every EBOOT.BIN and module on Lumines, Burnout Legends, GTA Liberty City Stories, Midnight Club 3, SOCOM Fireteam
  Bravo, Snoopy vs. the Red Baron, Gunhound EX and Peace Walker decrypts into a sane MIPS ELF (tags 0x08000000 type 0,
  0xc0cb167c type 1, 0xd91613f0 type 2; modules 0x00000000, 0x03000000, 0x4467415d, 0x3ace4dce), except the
  firmware updaters (tag 0x02000000, not in the table) and two splash modules packed with KL4E. Run through the system
  on the Mac, all eight start their EBOOT.BIN and run their own code to HLE functions not written yet; Burnout loads
  its fourteen modules and GTA its three (by ID, behind ~SCE headers), all stood in for, as they still do with the
  review's fixes.
- **Not checked:** the RP6 (the app wasn't built); types 5 and 6 and the module manager on a real game's own module
  (none of these games carry one: every module on them is Sony's), so a module unloading itself only on the tests'
  modules (Splinter Cell Essentials, which does it, isn't among the user's games).

## PSP core: one block for a program's memory; "nothing will run" noted once — 2026-10-05

Branch `cursor/psp-retail-load-2b67`, stacked on `cursor/psp-disc-formats-2b67` (for stack #106). The loader gave
each of a program's segments a block of its own, rounded to 256 bytes, so a retail PRX whose data starts right after
its code (Lumines': 8 bytes after, inside the code's last 256-byte step) was refused as overlapping memory already
handed out. A program now gets one block from its first segment to the end of its last, as the PSP's loader gives a
module one; segments whose bytes overlap are still refused, with a message of their own. And the kernel's note that
nothing will run again (no threads left, or every one waiting on another) comes once rather than every frame (the
RP6's log had 500 a launch), and again only after something has run since. The user's choices of what comes next are
in docs/psp-core.md's decisions: decryption and module loading, then HLE functions, sound, and sceFont; and their
games to test against first.
- **Review:** a general-purpose reviewer (no bugs; three low test gaps: the refusal of a block that can't go where
  the program is had lost its test, the segments' sorting and the empty ones passed over had none, nor the note's
  reset on a fresh start; all three tests added, each checked by the reviewer against a broken copy).
- **Checks:** the parts' 98 groups with both sanitizers ("kernel program memory": a program whose data starts 8
  bytes after its code, in the code's last 256-byte step, loads into one block covering both, which the old code
  refused, with the data listed first or last and an empty segment among them; a program linked into kernel memory
  refused, as no block can go there; overlapping segments refused with their own message; new "kernel stuck note":
  with no threads, two runs note it once, after a thread has run and gone once more, and after a fresh start again).
- **On the RP6** (build 104645): Lumines, Space Invaders Extreme, Brave Story and GTA Sindacco Chronicles now load
  from their CHDs and run their own code, stopping at functions the HLE kernel doesn't have yet: Lumines at
  sceDisplayWaitVblankStartCB; Space Invaders Extreme at SysMemUserForUser's SDK version calls, then a C++ runtime
  abort and ModuleMgrForUser 8f2df740; Brave Story at the SDK version calls, sceUtilityLoadModule, then its graphics
  library's "can't allocate memory (1056)", a load from address 8, and ThreadManForUser's message pipes and
  variable pools (c07bb470, d979e9bf, 68da9e36) and InterruptManager ca04a2b9; GTA Sindacco Chronicles at the SDK
  version calls, sceIoChangeAsyncPriority, ModuleMgrForUser b7f46618 and scePowerRegisterCallback. Those are the
  HLE work after decryption.

## PSP core: CHD, CSO v2, ZSO, DAX and JSO disc images — 2026-10-05

Branch `cursor/psp-disc-formats-2b67`, stacked on `cursor/psp-states-2b67` (for stack #106). The PSP reads CHDs (the
user keeps PSP games as CHDs) and the scene's other compressed forms of an ISO: CSO version 2, ZSO, DAX and JSO, with
LZ4 and LZO decoders of our own (`ares/psp/kernel/unpack.cpp`). nall's CHD reader (`nall/nall/decode/chd.hpp`), which
read only CDs and only by name, now reads a DVD's CHD too (chdman `createdvd`) and can read through a function, so the
PSP reads a CHD through the app's descriptor, never copied; the CD systems' path is as it was (`vfs::cdrom` refuses a
DVD's CHD). mia and the app take .zso, .dax, .jso and .chd for the PSP; the PSP's discs aren't gathered into
multi-disc sets. docs/psp-core.md, part 16, describes it.
- **Review:** a general-purpose reviewer (one medium: ZSO and CSO version 2 images written with an index shift pad
  their blocks, as maxcso's description allows, and LZ4's unpacking read the padding as more sequences and failed;
  two low: a damaged DAX could claim millions of uncompressed areas, a huge table and a scan long enough to look like
  a hang, and the message for a CD's CHD never reached the app, mia refusing the file first; all fixed: LZ4 ends once
  the block is whole, DAX's areas must fit the disc and become a table of frames, and the runner passes a medium's
  reason to the app's "Game Didn't Start"). The twelfth review of the save states branch (one low, wording) is fixed
  here too.
- **Checks:** the parts' 97 groups with both sanitizers (new: "disc formats", "disc formats damaged", "unpackers");
  `tests/psp/ares` (195 checks: every form boots the disc program, which reads its disc exactly; a CD's CHD and one
  needing its parent refused; its script builds libchdr); the app's unit tests (`LaunchSystemsTest`: .zso, .dax, .jso
  to the PSP alone, a .chd by its folder). Broken versions each failed a test (CSO v2's stored blocks, LZO's 3-byte
  match distance and its H bit, LZ4's distance 0 and its padding read as a sequence, DAX's uncompressed areas and
  areas adding up past the disc, JSO's short last block, and a DVD sector's place in its hunk).
- **On the RP6** (build 104643, launched by file URI from the SD card's `ROMs/psp`): all eight of the user's CHDs
  tried opened and read, up to 1.2 GB (Ace Combat - Joint Assault). Street Fighter III 3rd Strike's port starts from
  its CHD and runs at 60 frames a second (it asks for sound and utility functions that aren't written yet). Peace
  Walker, Gunhound EX, WipEout's collection and Ace Combat's EBOOT.BINs are encrypted, and say so. Lumines, Space
  Invaders Extreme, Brave Story and GTA Sindacco Chronicles have plain BOOT.BINs (PRXs), which the loader refuses:
  each segment takes its own block rounded to 256 bytes, and Lumines' data segment starts 8 bytes after its code ends,
  inside the code's last block. Next branch: one block for the whole module, then the HLE functions those games ask
  for. Also next: "no threads left to run" is logged every frame once a program has stopped. Build 104644 (the
  review's fixes): a CD's CHD (made on the host) launched as a PSP game shows "It's a CD's CHD. A PSP game's CHD is
  made from its ISO with chdman createdvd." in "Game Didn't Start"; Street Fighter III and Lumines as before.
- **Not checked:** DAX, JSO and ZSO images made by their own tools (the test's are made to the formats'
  descriptions, and the unpackers checked against minilzo's and lz4's own output); a CHD made with zstd hunks (the
  user's use zlib, LZMA, Huffman and FLAC); a CHD read through a descriptor on the RP6 (the app could read the SD
  card's paths, so it read them by name, as with part 14's images).

## PSP core: save states — 2026-10-05

Branch `cursor/psp-states-2b67`, stacked on `cursor/psp-umd-2b67` (for stack #106). Save states for the PSP: each part
saves and loads itself (the CPU's registers, memory 4 KiB at a time with empty pieces left out, the GE's commands,
palette, list and matrices, and the kernel's program, threads, objects, open files by path, controller, display,
calls and GE lists), behind a header naming the machine and the program the state was made with. Loading checks what
it reads (with no fixed limits: a list that claims too much runs out of state instead), refusing what no machine
could hold, and a bad state leaves the machine as it was, open files and all. A program that has ended makes no
state, and the app doesn't auto-save one, nor a game whose core is stuck in a frame (`PhobosCore.trySaveState()`). In
the app, #139's stand-in that left states out for the PSP is gone, and a PSP program in an `EBOOT.PBP` takes its
folder's name, so homebrew programs' states don't collide. Found on the way: a display list that never ends no longer
holds everything up (the GE runs a million commands a frame at most however often it stops, time passes while it
works, and its callbacks can't pile up); IDs and file numbers stop short of 2^31 rather than come round again; folder
listings leave out host names no PSP path can name. docs/psp-core.md, part 15, describes it. Both test scripts now
use the address sanitizer on macOS too.
- **Review:** a general-purpose reviewer (one high finding: every homebrew `EBOOT.PBP` had one name, so one set of
  states; two medium: the reader's list limits sat below what the kernel can make, and the tests couldn't catch a
  field left out; three low: range checks, files on a disc no longer in the drive, docs; all fixed). Then a delta
  review (one high: loading read the running thread after freeing it, which macOS's tests, built without the address
  sanitizer, missed; four medium: values loaded that would crash or corrupt memory later (a disc file open for
  writing, IDs handed out twice, display lists the driver couldn't have queued) or hang the machine (endless sound
  owed, a clock or controller timer far behind); four low: the memory-use claim, test gaps, files removed while open
  lost by an undone load, and external launches still named "EBOOT.PBP"; all fixed). Then a second delta review
  (seven low: a loaded state's folder names reaching outside the memory stick, the app's folder naming for providers
  whose paths are IDs, untested ID and open-file checks, auto-save of a program that had ended, stale script headers
  and doc wording, an endless display list holding a frame forever (older than this branch), and three files for the
  next branch in the working tree, kept out of this commit; all fixed). Then a third delta review (one medium: IDs
  counted past 2^32 came round and handed out one in use; four low: lists that stop every few commands still never
  let go, a host name with a '\' made the machine's own states unloadable, ".." alone at a device's top untested, and
  the app's check before an auto-save waiting for ever on a stuck core; all fixed). Then a fourth delta review (four
  low: a frame could still run between the app's check and its save, an endless display list still took seconds a
  frame beside a thread that woke often, mutants the tests missed, and a group count in the docs; all fixed, the check
  and the save now one step: `trySaveState()`). Then a fifth delta review (five low: a save asked for into the Auto
  slot told nothing when there was nothing to save, the GE's frame budget left out of the state, its refill at each
  vertical blank untested, a list that draws big primitives over and over still slow to end a frame (left as a stated
  limit until the GE's timing counts drawing), and stale comments; all fixed). Then a sixth delta review (one low: a
  states folder that couldn't make the state's file, or gave no way to write it, failed the save without a word or
  claimed it saved, older than this branch; fixed: both now fail as any other failure does). Then a seventh (two low:
  a multi-disc game's disc note written outside the save's error handling, so a failure crashed a save asked for or
  failed the auto-save without a word, older than this branch; and "Not checked" not saying the new failure paths
  never ran; both fixed) and an eighth (four low: the disc note's copy beside the state still best effort (older than
  this branch: left as it was, see "Known"), the temporary note left behind when a save is called off, and two
  handoff lines; the rest fixed), a ninth (three low: a save whose screen went away still logged as a failed
  auto-save, a leftover variable, and "Not checked"; all fixed), a tenth and an eleventh (two low each, all wording;
  fixed), and a twelfth (one low: two messages about a save cut short blamed a screen going away, where only the
  activity finishing cuts one short; fixed on the disc formats branch).
- **Checks:** the parts' 94 groups with both sanitizers (new: "state fields", where each of 212 fields reaches the
  state and comes back and 54 values no machine could hold are refused; "kernel states"; "kernel ids run out"; "ge
  endless list", with four kinds of list that never end, a thread that wakes often, and callbacks), `tests/psp/ares`
  (163
  checks on arm64 and x86-64, and with the address sanitizer: cube carries on exactly after a load on both engines and
  in a fresh session; bad states, cut short by 4 KiB or 4 bytes or owing endless sound, refused with every byte of the
  machine as it was and a file removed from the host while open still reading; another program refuses cube's state;
  hello, once it has left, makes no state; cube from a disc image takes its state back and another disc refuses it),
  the Allegrex tests' 56 groups with both sanitizers, the app's unit tests (`LaunchSystemsTest`: an `EBOOT.PBP` takes
  its folder's name; a launch's folder from its path, document ID or a URI ending in the file's name). Twenty-six
  broken versions each failed a test. On the RP6: with build 104631, hello's `EBOOT.PBP`, launched as
  `Hello/EBOOT.PBP` the way another app launches a game, ran as "Hello.pbp" and auto-saved as `Hello.pbp.state.auto`
  (216 KB); cube
  auto-saved (1.59 MB), and saved to slot 0 and loaded from it through the menu, carrying on from where it was saved.
  With build 104633: leaving hello (which had ended) made no auto state and no failure message; cube saved to slot 0
  (1.62 MB) and loaded from it through the menu. With build 104634 (the app's check before an auto-save now waits two
  seconds at most): leaving hello made no auto state again, and leaving cube auto-saved it (1.57 MB). With build
  104635 (the check and the save one step): leaving hello found nothing to save, saying nothing; leaving cube
  auto-saved it (1.58 MB). Builds 104636 (quiet only for the auto-save as a game quits) and 104637 (a states folder
  that can't take the state fails the save) did the same. Build 104638 (a multi-disc note written inside the save's
  error handling): leaving cube auto-saved it (1.56 MB); hello wasn't left. Build 104639 (the temporary note deleted
  however a save ends): leaving hello found nothing to save, saying nothing; leaving cube auto-saved it (1.58 MB).
  Build 104640 (a save whose screen went away no longer logged as failed) did the same (1.57 MB).
- **Not checked:** the Library's scan naming programs on the RP6 (it uses the same function as the launch, which
  ran); loading on the interpreter on the RP6; a damaged state whose undoing fails too (the game then starts afresh,
  but no test can make the machine's own state fail to load); a states folder that can't take the state, and a save
  asked for with nothing to save (no test or RP6 run made either happen; the app's save code has no unit test); a
  multi-disc game's save with these builds, and a disc note that can't be written; a save asked for, or the
  auto-save before a load, whose screen goes away while it runs, so that its message, saved or failed, may not show
  (the activity finishing).
- **Known, older than this branch:** a multi-disc game's disc note is copied beside its state as best effort, as the
  preview is: should that copy fail, the save still says it saved, and the slot keeps the note from its last state,
  so loading it would put the wrong disc in. Worth fixing with the app's save code's own tests.
- **Left on the RP6:** hello's `EBOOT.PBP` in the app's `files/psp-test/Hello` folder; `Hello.pbp.state.auto`,
  `cube.elf.state0` and `cube.elf.state.auto` (each with its thumbnail) in the states folder's `PlayStation
  Portable`. (The `cube.elf` states
  from build 104631's run were gone from the folder by build 104633's.)

## PSP core: disc images (ISO and CSO), the disc's files and the drive — 2026-10-05

Branch `cursor/psp-umd-2b67`, stacked on `cursor/psp-app-2b67` (for stack #106). The PSP's disc as an image: an ISO
or CSO reader with the ISO 9660 file system (`ares/psp/kernel/disc.cpp`), disc0: on it in the kernel's file calls
(paths, `sce_lbn` sector runs) and umd0: as the whole disc, sceIoIoctl and sceIoDevctl (the disc's, and the memory
stick's), sceUmdUser, and booting PSP_GAME/SYSDIR/EBOOT.BIN (or a plain BOOT.BIN when EBOOT.BIN is encrypted). In the
app, .iso and .cso are back, and a disc image the app can't read by path is read through its descriptor instead of
copied (mia's medium, `/proc/self/fd/<n>`, through `nall::vfs::descriptor`, new). docs/psp-core.md, part 14,
describes it. Behavior from PPSSPP's notes on the hardware (the ioctl and devctl codes, the drive's states and
timeouts, `sce_lbn` paths, umd0:), our own code.
- **Review:** a general-purpose reviewer (one medium finding: `sce_lbn` numbers are always hexadecimal; thirteen
  low: maxcso's padded last block, a failed block left cached, a damaged index able to ask for 186 GB, umd0: with a
  path after it, read-only opens that create, "." and ".." and short names in disc listings, the 1-microsecond
  timeout, trimmed images, a blank BOOT.BIN, the runner's descriptor route for a pipe, pread's EINTR, a test that
  never ran, wording; all fixed), then a fresh reviewer's delta review (nine low: `sce_lbn` runs with slashes and
  their status, a program cut short by a truncated image refused, a damaged folder size read whole, the status of a
  device's top, umd0:'s sizes in sectors, the spare status words, two ioctl errors, an untested kernel path, two
  stale comments, the descriptor route for a URI without the game's name; all fixed).
- **Checks:** `tests/psp/disc.cpp` (new: four groups; ISO and CSO images made by `tests/psp/disc-image.hpp`), the
  parts' 90 groups, `tests/psp/ares` (127 checks: the new `disc` program booted from an ISO and a CSO reads every
  path right; `vfs::descriptor`), the app's unit tests; thirteen broken versions each failed (six only after tests
  were tightened). On the RP6: the `disc` program from an ISO and a CSO printed what the host expects, and cube runs
  from a CSO at 60 frames a second. The descriptor route ran on the host only (the RP6's app reads the SD card by
  path, and the shell can't hand the app a document).
- **Left on the RP6:** our test images and programs in the app's own `files/psp-test` folder (no games).

## PSP in the app: the core as an ares system, in Phobos's Android app — 2026-10-05

Branch `cursor/psp-app-2b67`, stacked on `cursor/psp-flash0-dump-2b67` (for stack #106). At the user's request ("the
remainder of the emulator core so that it can be hooked into the Phobos front end"; integration first, homebrew in
the app before retail games): the PSP as an ares system (`ares/psp/psp.cpp` and `system/`: node tree, frame loop,
controls, memory stick, the recompiler with the interpreter as fallback), mia's PSP medium and system, and the
Android app's entry (runner, JNI table, launch names, touch layout, icon, HUD label, a PSP Memory Stick path
setting). docs/psp-core.md, part 13, describes it.
- **Found on the way:** libchdr's `MIN`/`MAX` macros (Android's builds read CD images) collide with the Allegrex's
  instructions, so psp.hpp undefines them; the runner wrote empty states as saved (now refused for any core); the
  kernel kept the last game's mounts (the system now clears them at power-on); building sljit's C file and the C++
  with different build modes (sljitConfigPre.h's `SLJIT_DEBUG`) crashed the recompiler in a scratch host build, so
  the new test builds everything with `BUILD_DEBUG`, as the parts' tests do. nall's `memory::map()` passed `mmap`'s
  `MAP_FAILED` on as a pointer, so no bump allocator could see a refused mapping: it returns null now.
- **Review:** a general-purpose reviewer (three medium findings: the recompiler's fallback could never trigger, the
  `MAP_FAILED` above; every PSP quit would have said "Save Failed!" with Auto-Save State on, so the app now leaves
  states out for the PSP; ISO and CSO files listed but never run, so they're off the Library until the core reads
  them; ten low: the machine kept after unloading, a crash logged every frame, sceDisplaySetMode trusting any size,
  .prx modules in the Library, a copied program's disc being the shared cache, zipped programs refused, tests and
  docs claiming more than they checked, `ms0:/./` paths, two statements doing nothing, a dated comment; all fixed).
- **Checks:** `tests/psp/ares/run-tests.sh` (new; 93 checks), the parts' 86 groups, the CPU's 56, and the app's unit
  tests. On the RP6 (installed with `install -r`, data kept): cube, beginobject, controller and
  hello at 60 frames a second, from the app's own files folder through a launch intent (`am start` with the `psp`
  system extra); the memory stick folder was made under the user's saves path. The test programs pushed there
  (`Android/data/com.phobos.emulator/files/psp-test`) are ours, built from pspsdk's samples.
- **Not yet:** ISO/CSO images, save states, sceAudio, retail games (decryption, more HLE modules), sceFont.

## PSP tools: a flash0 dumper for the user's PSP — 2026-10-04

Branch `cursor/psp-flash0-dump-2b67`, stacked on `cursor/psp-ge-lighting-2b67` (for stack #106). At the user's
request (they'll provide their PSP's firmware files this way, for the fonts sceFont reads and the optional firmware
modules): `tools/psp-flash0-dump`, a homebrew program that walks flash0 and copies every file to `flash0/` beside its
EBOOT.PBP, with `SHA256SUMS` (checkable with `shasum -a 256 -c`) and `failed.txt`; it also tries the raw image
(`lflash0:0,0`), which user-mode homebrew is expected to be refused (PPSSPP refuses it with 0x80020321). SHA-256 is
our own, from FIPS 180-4. The dump is the user's copyrighted firmware: it stays on their devices, never committed.
- **Checks:** the SHA-256 code matches `shasum -a 256` on the empty input, "abc", sizes around each padding boundary
  and a megabyte fed in uneven pieces. The `SMOKE=1` build in PPSSPP's headless build copied a 23-file sample tree
  (PPSSPP's own fonts, nested folders, an empty file, exact and multi-piece sizes) with every copy verifying against
  `SHA256SUMS`; PPSSPP's virtual flash0 can't list its folders, hence the sample. Both builds compile without
  warnings. The release EBOOT.PBP (SHA-256 960b2db7dbc6ebbab6b929b3f2f5fc5003be75377b820a0febfd52e96ced3647) is in
  `.local/psp-flash0-dump` and on the RP6 at `Download/FLASH0DUMP/EBOOT.PBP`; it hasn't run on a PSP yet. Review: a
  general-purpose reviewer (seven low findings: the root folder made with a trailing slash, a listing error ending
  the walk silently, unchecked writes to SHA256SUMS and failed.txt, no `make clean` note, flash0 said to hold the
  settings, a button-wait comment, the image cap overshooting; all fixed), then a delta review with no findings.

## PSP core: lighting's share in 256ths, as the PSP measured it — 2026-10-04

Branch `cursor/psp-ge-lighting-2b67`, stacked on `cursor/psp-ge-round3-fixes-2b67` (for stack #106). From round 3's
lighting files (exact cosines, several material and light colors): a light's share counts 256ths, and the two colors
times the share shift down 18 bits (ge/lighting.cpp `share()` and `light()`), where PPSSPP (and the core) had
512ths, rounded up and one more, shifted 19. Fitted with a scratch script outside the repository against the values
with normals (a, 0, b) (`light-cosines` rows 0 and 8, all of `light-materials` and `light-colors`): 1620 of 1632
(PPSSPP's: 1494); no constant offset to the rounding fits the last 12 (two cases, level 32 at a cosine of 56/65 and
64 at 72/97, in six cells of each file, a level lower on the PSP). No measured share lands on a whole 256th, so
rounding up there (rather than down and one more; PPSSPP's rule agrees at the tests' 0.5 and 0.75) is assumed;
negative (darkening) shares are PPSSPP's reading carried over to 256ths, unmeasured, and now subtract 256 for a full
one (255 before).
- **Checks:** against the PSP's files: `light-cosines`, `light-powered`, `light-shine` and round 2's `light-specular`
  now identical; `light-materials` and `light-colors` 1536 pixels apart (were 15616); round 2's `light-diffuse`
  12288 (was 28160; the rest in its 64 and 192 channels at shares its white channel confirms, as `light-materials`'
  12), `light-point` 9728 (was 30720), `light-spot` 768 (was 2560), all a level each; every other file unchanged.
  Unit tests: the lighting tests' hand-worked values restated in 256ths (all unchanged), plus two cases where the
  rules differ (cosine 0.28 on white: 71; red 32 at 0.8: 25), which PPSSPP's rule fails, and a darkening spotlight.
  PSP tests 86 groups, 0 failures, on arm64 and x86_64 (Rosetta).
- Also refreshed: the comparisons with PPSSPP's software renderer in psp-core.md and the test programs' README,
  stale since the core took the PSP's measured rules (`compare-ppsspp.sh` in the container now: within 2 levels on
  99.88% of `blend`'s pixels, 99.64% of `clut`'s, 99.44% of `cube`'s, 99.10% of `celshading`'s, 98.42% of
  `envmap`'s; the same with this change and without it, and at #135 the 3D ones were a little further). Review: a
  general-purpose reviewer (one medium finding: what's left of `light-diffuse`, `light-point` and `light-spot`
  blamed on cosines the data clears; six low: the misfits' count, "rounded up" and negative shares stated as
  measured, the broken-versions list, a percentage's wording, wrapping; all fixed), then delta reviews (three low
  and one cosmetic, on the docs, fixed) ending with no findings.

## PSP core: the GE's rounding, 3D sprites and depth layout, as the PSP measured them — 2026-10-04

Branch `cursor/psp-ge-round3-fixes-2b67`, stacked on `cursor/psp-vfpu-round3-fixes-2b67` (for stack #106). From
round 3's GE data:
- The rounding onto the screen (ge/transform.cpp `project()`): a position's distance from screen coordinate 2048 is
  cut to a sixteenth toward 2048 (was PPSSPP's +0.375 then toward zero), in doubles. Off the screen: outside 0-65535
  once cut (a sixteenth or more left of 0, or 4096 or more), which follows from the rounding but wasn't measured.
  Whether it's 2048 or the viewport's center isn't settled (they were the same in the case).
- 3D sprites (ge/draw.cpp `rectangle()`, now told whether it's 3D): the fog split at the middle column, each half
  taking the fog of the corner on the other side (measured with the first corner at the top left; either order, as
  PPSSPP's rule has it, read for behavior; the middle halfway rounded down, the right half winning a column on it:
  assumed); texels with u / w and 1 / w across x and v / w down y, then divided. A turned 3D sprite isn't measured:
  the same rule with the coordinates swapped; a corner with no w in front of the camera falls back on 2D stepping.
- The depth buffer's layout (memory/memory.cpp, ge/pixel.cpp): VRAM's second copy flips offset bits 6 and 13, the
  fourth first turns bits 5-9 round by one place, and the GE reaches its depth buffer as the fourth copy shows it.
  `pointer()` maps those copies (a range must stay in one 32-byte piece), `copyIn()`/`copyOut()`/`fill()` go piece by
  piece there, and `changed()` reports each copy where it sees the change (16 KiB blocks for longer changes). The HLE
  kernel checks game buffers with the new `reaches()` (the range inside one area or copy), as `pointer()` would now
  refuse a long one through a rearranging copy; and `read()`/`write()` take an access that crosses a piece of one (an
  HLE function's, at a game's unaligned address) a byte at a time. Columns 256-511 land in the gaps the measurement
  left, by the rule; not measured.
- **Checks:** against the PSP's round-3 files (tests/psp with `PSP_GE_RESULTS`): `3d-rounding-middle`,
  `3d-sprite-fog`, `3d-sprite-flat` and all four `depth-layout` files now identical, `3d-sprite-texels` 72 pixels a
  level apart (was 20304), round 2's `3d-sprite` 53 (was 10152), `3d-floor-depth` 1388 by 1 (was 17424 by up to
  255), `3d-clip` 1512 (was 1535); every other file unchanged (60 identical, was 54). Unit tests: the rounding both
  sides of 2048 and the screen's edges once cut; a 3D sprite's fog halves in either vertex order and where its
  middle falls (even and odd widths); its perspective texels upright, turned, and with a corner behind the camera;
  the copies' mapping (exhaustively, each way the other's reverse, and against the measured formulas), reads, change
  reports (a long change through the first copy too), bulk copies and an unaligned word through them, and
  `sceCtrlPeekLatch` into an unaligned buffer in the fourth copy; the depth helpers read through the fourth copy.
  PSP tests 86 groups, 0 failures, on arm64 and x86_64 (Rosetta), the comparison identical on both; Allegrex tests
  56, 0 failures. Sixteen mutations (the old rounding or screen bounds; the fog unsplit, by vertex order, its middle
  rounded up or its tie to the left; affine, swapped-when-turned or unguarded 3D texels; linear depth; no
  rearranging; the fourth copy like the second; bulk copies in one piece, which crashes the sanitizer build; changes
  at one offset, or the long path keyed on the source alone; no byte-at-a-time writes) each fail them. Review: a
  general-purpose reviewer (eight low findings: the off-screen test against the cut position, the screen bounds and
  the fog's tie and vertex order stated as measured, HLE buffer checks refusing the rearranging copies, a latent
  overflow in the bulk copies' range check, "512 KiB" for 1.5 MiB, test gaps, two short lines; all fixed), then
  delta reviews (unaligned HLE accesses across a piece, and long lines; fixed) ending with no findings.

## PSP core: the VFPU's tiny products and round 3's prefix rules — 2026-10-04

Branch `cursor/psp-vfpu-round3-fixes-2b67`, stacked on `cursor/psp-fpu-measured-2b67` (for stack #106). From round
3's VFPU data (ares/psp/cpu/interpreter-vfpu.cpp; the recompiler hands VFPU instructions to the interpreter):
- Tiny results: `vfpuBits()` rounds a result to 24 bits as if exponents went on below 2^-126, then flushes it to a
  zero of its sign if it's still below, judged on the bits (an x87 FPU can keep a float wider). `vmul`, `vscl`,
  `vmscl` and `vcrs` hand it their exact products (doubles), `vdiv` its quotient to 53 bits (assumed to round the
  same way; not measured), instead of float results the host had already rounded as denormals.
- The math functions (`vrsq`, `vsqrt`, `vsin`, `vcos`, `vasin`, `vexp2`, `vlog2`, `vnrcp`, `vnsin`, `vrexp2`) take
  prefixes as `vrcp` does: `vrcp`'s code became the template `vfpuLastLaneFirst()`. The negating three ignore the
  negate setting.
- The adders (`vdot`, `vhdp`, `vfad`, `vavg`) add up all four lanes whatever the size, each read through its own
  prefix lane (`vfpuReadFour()`), leaving out a lane whose swizzle names a lane past the size (`outsideLanes()`, now
  over four lanes). The independent review found this (my first version zeroed only the lanes within the size and
  wrongly wrote that the data contradicted zeroing in `vfad` and `vavg`).
- `vavg`'s t prefix only negates; the sum is divided by the size (weighing by the constant 1/3 fit round 2's
  `vavg.t` worse). Found by fitting a weight per lane against every run, with a scratch tool outside the repository.
- A swizzle past the size gives 0 in `vf2i`'s, `vi2f`'s and `vbfy1`'s lane, a 0 half float in `vf2h`, and in `vh2f`
  a 0 that the negate setting makes -0 before it's split.
- Left open, documented in psp-vfpu-measurements.md: 32 of the second recorder list's 284 entries (`vavg` where its
  terms cancel, `vcmp`, `vcrs`, `vcrsp`, `vdet`, `vsocp`), all from its part built for swizzles past the size.
  Reading such lanes as 0 in every instruction was tried: it fixed `vh2f` and some `vf2h` and `vbfy1` entries (kept,
  per instruction) and made others worse.
- **Checks:** compare.sh: the second recorder list went from 36 exact (and 7 rounding only) to 198 (and 54), round 2's
  from 1157 to 1158 exact (its `vscl` entry; 49 rounding only, as before), `vmul-tiny` from 85.35% to 100%; every
  other row is unchanged. The measured group replays `vmul-tiny` (all 262144 results); the edge group gains a
  tiny-products case, a tiny-quotient case (the assumption above), and twenty recorded runs, one per rule (each math
  function's last lane, the three ignored negates, `vavg`'s t prefix, the adders' four lanes, and the stray lanes of
  `vdot`, `vhdp`, `vf2iz`, `vi2f`, `vh2f`, `vf2h` and `vbfy1`). Allegrex tests 56 groups, 0 failures, on arm64 and
  x86_64 (Rosetta); PSP tests 85, 0 failures. 22 mutations, each undoing one rule, fail them on both engines (the
  tiny-products one fails the replay too). One run of the suite, just before the mutations, failed the `vbfy1` case
  with the value the core gave before its rule, though the source had the rule; every run since (the 22 mutation
  builds and the full runs after them) passed. The reviewer found that case's code path deterministic; the likeliest
  cause is an overlapping build replacing the test binary, which every run links at the same path
  (`${TMPDIR:-/tmp}/phobos-allegrex-tests/allegrex`), so runs side by side need a `TMPDIR` each. Review: a
  general-purpose reviewer (two medium findings: the adders' four lanes, which it found by experiment, and recorded
  runs too few to guard every rule; four low: `vdiv`'s tiny quotients, `vavg`'s weight 1/3, the flush judged with a
  float compare, the edge test's header; all fixed), then delta reviews (two low, on the docs, fixed) ending with no
  findings.

## PSP core: the FPU as the PSP measured it — 2026-10-04

Branch `cursor/psp-fpu-measured-2b67`, stacked on `cursor/psp-measured-r3-2b67` (for stack #106). From round 3's
FPU data (ares/psp/cpu/interpreter-fpu.cpp; the recompiler hands FPU instructions to the interpreter):
- `add.s`, `sub.s`, `mul.s`, `div.s` and `sqrt.s` round as FCSR's mode says, and `cvt.s.w` too (as MIPS documents;
  not measured): `rounded()` switches the host's rounding (`fesetround`) for the one operation when the mode isn't
  the nearest, with the inputs and result in volatile variables so the compiler keeps the arithmetic between the
  switches; the nearest costs nothing extra.
- Their NaNs are picked as the PSP's (`pickNaN`: the first signaling input made quiet, else the first quiet one,
  else 0x7fc00000): x86 hosts give 0xffc00000 for invalid operations and pass on their first operand, which failed
  the replay on x86 (found in review; checked under Rosetta).
- The conversions out of range saturate by sign: 0x80000000 for negative numbers and -infinity (0x7fffffff for the
  rest and NaNs, as before).
- `cfc1` from register 0 (FIR) reads 0x00003351; a thread starts with FCSR 0x00000e00 (kernel/threads.cpp).
- FCSR's exception flags, enables and causes stay kept but unused (documented: the PSP's enabled ones end a program).
- **Checks:** the measured group now also replays `fpu-convert`, `fpu-convert-safe`, `fpu-arith` and
  `fpu-arith-safe` (all 4 x 81920 results, every mode, now exact) and FIR; unit tests for each mode (1 + 2^-30,
  1 - 1, 1/3, 2^24 + 1) and the negative conversion; a kernel test for the starting FCSR. Allegrex tests 56 groups,
  0 failures, on arm64 and on x86_64 (Rosetta); PSP tests 85, 0 failures; mutations (the old negative conversion,
  the mode ignored) fail them on both engines. compare.sh's 80 FPU rows all 100%. Not measured: flush to zero in the
  directed modes, `cvt.s.w`'s rounding. Review: a general-purpose reviewer (one high finding, the x86 NaNs, which it
  reproduced with GCC on amd64 and clang under Rosetta; four low; all fixed), then a delta review with no findings.

## PSP: what the user's PSP measured in round 3 — 2026-10-04

Branch `cursor/psp-measured-r3-2b67`, stacked on `cursor/psp-ge-round3-2b67` (for stack #106). The user ran round 3
(VFPU and GE), the FPU probes and rounds 1 and 2 again with tools/psp-measure; the results are in
`.local/psp-measure-2026-10-04-round3/results` (not in the repository). Nothing stopped the PSP, round 2's two FPU
tests finished this time (why isn't known), and every repeated result file is identical to the earlier sessions'
(the manifests differ in wording, and `controller-timing.bin`'s times are measured).
Published: docs/psp-vfpu-measurements.md ("Round 3") and docs/psp-core.md ("Round 3's results"); the SHA-256 of
every new file (`tests/allegrex/measured/SHA256SUMS3`, `tests/psp/measured/SHA256SUMS3`), both manifests, `ops3.txt`,
and, packed with xz (360 KB), `fpu-state`, the four FPU files and `vmul-tiny`. Findings, to be fixed next:
- FPU: FCSR starts as 0x00000e00 (overflow, divide-by-zero, invalid enabled); IEEE 754-2008 NaNs (the core already
  matches); arithmetic follows the rounding mode (the core rounds to nearest); conversions out of range saturate by
  sign (the core gives 0x7fffffff for negatives too).
- VFPU: products rounded to 24 bits, then flushed below 2^-126 (kept up to j = 1448; the core keeps 2^-126 for j
  from 1449 to 2048 too, and round 2's one `vscl` lane fits); the second recorder list matches in 36 of 284 entries
  (+7 by rounding).
- GE: screen positions truncated toward 2048 (here both the viewport's center and the middle of the GE's space;
  which one counts is still open; fits every cell of `3d-rounding-middle`); a 3D sprite's fog split across its
  middle as PPSSPP does (the core fogs it all with the second corner's), its texels stepped as u/w, 1/w across x
  and v/w down y; the depth buffer in order only through VRAM's fourth copy (formulas for the others fit all 16384
  values); ambient, and plain diffuse with exact cosines, the core's in all but two cells; the rest of lighting and
  the ramps a level apart in places (lower on the PSP but both ways in the vertical ramps); curves drawn.
- **Checks:** compare.sh and the host tests (85 groups, 0 failures) on the new files; the reruns compared byte for
  byte with the first sessions'; the depth formulas and the tiny products' model checked against every value.
  Review: a general-purpose reviewer checking every claim against the data (13 findings, two high: a 3D sprite's fog
  is split as PPSSPP does, not left off, and the tiny products are kept up to j = 1448; all fixed), then delta
  reviews (four low, fixed; then no findings).

## PSP tools: the GE's third round — 2026-10-04

Branch `cursor/psp-ge-round3-2b67`, stacked on `cursor/psp-measure-menu-2b67` (for stack #106). tools/psp-measure
gets "Round 3: the GE" on its menu (ge.c's round3, about 7 MB into results/ge, `manifest3.txt`), for what round 2
left open (docs/psp-core.md, "Round 3, ready for the PSP"):
- lighting with exact cosines, from normals built of Pythagorean triples and quadruples (also scaled by 2, 1/2, 64
  and 1/64 and turned toward y; the light's direction 1 and 3 long): plain diffuse (`light-cosines`), powered
  diffuse (`light-powered`: the GE's light kind 2, which `sceGuLight` sends for `GU_POWERED_DIFFUSE`; round 2's
  diffuse cases were kind 0, as `GU_DIFFUSE` gives, checked in pspsdk's disassembly), and the shine
  (`light-shine`), at powers 1 and 2; material, light and ambient levels (`light-materials`, `light-colors`,
  `light-ambient`);
- `ramp-colors`, `ramp-colors-vertical`, `ramp-colors-3d` (corners between pixels) and `ramp-fog` (w kept at 1):
  16 slopes each;
- `3d-rounding-middle` (edges 0 to 15 256ths past the pixel middle: truncated or rounded), `3d-wall-texels`
  (perspective along x), `3d-sprite-fog`, `3d-sprite-texels`, `3d-sprite-flat`;
- `bezier-flat`, `bezier-curved`, `bezier-divide-8`, `spline-edges-0`, `spline-edges-3` (the core doesn't draw
  curves yet);
- `depth-layout-0` to `-3`: a 256x64 area of depths y * 256 + x, the whole depth buffer (512x256 values) read back
  through each of VRAM's copies, last in the round.

The host test runs round 3 in the core too (every file written; the curves empty). Against PPSSPP's software
renderer (the SMOKE build), the core matches on the six lighting cases, the flat sprite and the four depth reads,
and differs on the ramps, the 3D edges, the wall, the sprite's fog and texels, and the curves.
- **Checks:** `make` and `make SMOKE=1`, no warnings; the SMOKE build in PPSSPPHeadless writes all 24 round-3 files
  (and round 2's, unchanged); PSP host tests 85 groups, 0 failures, still 52 of 63 round-2 pictures as the PSP's.
  Review: a general-purpose reviewer (eight low findings, all fixed: the depth case now sets its own depth range and
  reads the whole depth buffer, the host test says when the reference lacks a file, `GU_POWERED_DIFFUSE` by name,
  wording and widths), then delta reviews (one more width finding, fixed; then no findings).
- **Not checked:** anything on a PSP: that's the user's next session (round 3's VFPU and GE lines, then the
  probes).

## PSP tools: one measuring program, with a menu — 2026-10-04

Branch `cursor/psp-measure-menu-2b67`, stacked on `cursor/psp-vfpu-round3-2b67` (for stack #106). At the user's
request the two measuring programs are one, `tools/psp-measure` (moved with git mv: tools/psp-vfpu-measure's main.c
is vfpu.c, tools/psp-ge-measure's is ge.c, the host tools came along), and it no longer leaves after a round:
- main.c: a menu (up and down pick a line, X runs it) of every round of both: round 3's VFPU and FPU tests, the FPU
  probes, round 2's VFPU and FPU tests, round 2's GE tests, round 1; start afresh; leave. After a round, X goes back
  to the menu.
- Start afresh asks first, then renames `results` to `results-1` (or the next number free) and starts an empty one:
  every test runs again, nothing is deleted.
- results.c: the results folder and each test's file as the VFPU program had them (`.part` renamed to `.bin`, one
  more try after a stop, then `.stopped`), now for the GE's tests too, which are skipped once done. Files go to
  `results/vfpu` and `results/ge` (both programs wrote a `manifest.txt`).
- The SMOKE build runs round 3's VFPU and FPU tests, the probes and the GE's tests (PPSSPPHeadless with
  `-i --graphics=software`); its files are the two old programs', byte for byte, but for the manifests' wording and
  `controller-timing.bin` (measured times; the same reads waited).
- What round 1 and round 3 record "as found" (the generator's state, FCSR) is taken when the program starts
  (`vfpuStart`): round 1's `vrnd` runs only while the generator is still as it started (otherwise it says to start
  the program again and skips that test), and `fpu-state.bin` holds FCSR from the start.
- tests/psp/measure.cpp ("psp measure") drives the menu in the core: GE round 2, start afresh, again (the same
  pictures as the first time), a third time (nothing rewritten), the probes, leave; still 52 of 63 pictures as the
  PSP's. build.sh builds pspmeasure.elf.
- Paths updated in the docs and comments; the committed manifests (the old programs wrote them) and older entries
  here keep the old names. compare.sh and fit.py take the results folder or its `vfpu/`.
- **Checks:** `make` and `make SMOKE=1` with `-Wall`, no warnings; the SMOKE build in PPSSPPHeadless against the old
  programs' SMOKE files (as above); `tools/psp-test-programs/build.sh`, then `PSP_TEST_PROGRAMS=/tmp/psp-programs
  PSP_GE_RESULTS=.local/psp-measure-2026-10-04/ge tests/psp/run-tests.sh`: 85 groups, 0 failures, 52 of 63 pictures
  as the PSP's (unchanged); ops.py regenerates ops.h and ops3.h byte for byte. Review: a general-purpose reviewer (one
  medium finding, the "as found" state above; eight low ones, all fixed), then delta reviews (one more low finding,
  the round 1 note's wording, fixed; then no findings).
- **Not checked:** the menu, `choose()` and starting afresh (a directory renamed with `sceIoRename`) on a PSP or in
  PPSSPP (the SMOKE build skips the menu); they've run only in this core's HLE, whose rename is
  `std::filesystem::rename`.
Docker Desktop wedged part way (`docker exec` and `docker cp` hung on the shared folder); quitting and reopening it
fixed that, with the container and its files intact.

## PSP tools: the VFPU measurement's third round and the FPU probes — 2026-10-04

Branch `cursor/psp-vfpu-round3-2b67`, stacked on `cursor/psp-ge-filter-2b67` (for stack #106). tools/psp-vfpu-measure
gets round 3 (square) and the FPU probes (triangle), for the user's PSP:
- `fpu-convert-safe`, `fpu-arith-safe`: round 2's FPU tests on inputs that can't stop the PSP (no NaNs, infinities,
  denormals, too-big or too-small results); `fpu-state`: FCSR and FIR as a program finds them;
- `vmul-tiny`: products a sliver below the smallest normal number, 2^-126 (1 - j^2 2^-46), telling three answers
  apart: all 0 (flushed before rounding), 2^-126 up to j = 1448 (rounded to 24 bits, then flushed), or up to j =
  2048 (rounded as IEEE denormals, then flushed: the core today);
- `ops3.bin`: a second recorder list (ops.py's `entries3`, 284 entries, `ops3.h`): the math functions with prefixes,
  swizzles past the size in instructions that don't work lane by lane, vavg and vfad with t prefixes. ops.h is
  unchanged (its own seed);
- seventeen FPU probes, one value each, run once with flush to zero off (the three -fs ones with it on): a probe that
  stops the PSP is given up on at the next start (`beginTrying` with no retry). NaNs in MIPS's encoding (quiet: top
  fraction bit clear).
Reviews: Bugbot (three passes) and a general-purpose reviewer with a strict findings-only answer, which found what
Bugbot didn't (-2^31 in the safe inputs, mislabeled NaNs, tiny products that couldn't answer their question, an
unchecked rename, stale docs). From now on that reviewer gives the independent review docs/development-process.md
asks for (with delta reviews after edits); Bugbot is an optional second opinion, read in full.
compare.cpp reads all of it (`reportFpu` for the state and the probes). Built with pspdev; the SMOKE build runs to
the end in PPSSPPHeadless with `-i` (its ARM64 JIT asserts on an ops3 entry: PPSSPP's bug, not the program's).

## PSP core: the GE's filter and near-plane cut, as the PSP measured them — 2026-10-04

Branch `cursor/psp-ge-filter-2b67`, stacked on `cursor/psp-ge-sampling-2b67` (for stack #106). From the user's
PSP's pictures (docs/psp-core.md, "Fixed since"):
- the bilinear filter drops the fraction at each step, top pair, bottom pair, then between them (texture.cpp): all
  three `filter-magnify` files identical; a unit check in "draw filter" (0, 15 / 0, 17 at the middle gives 7, not 8);
- a triangle cut at the near plane blends its new corners from the kept corner (transform.cpp), as part 11 first
  had it, not PPSSPP's way: `3d-clip` 8410 pixels apart down to 1535 (no unit test pins the way round: on the
  tests' small canvas the corners' level of difference doesn't reach a pixel; the measured file shows it);
- 52 of 63 pictures identical. Tried and taken back: stepping vertex colors like texture coordinates (in 65536ths:
  no better; in 256ths: worse). Lighting fits no simple arithmetic (best about 60 of 768 values in light-diffuse
  off); the PSP's cosine looks a little off, as an approximate normalization would. Both written up as cases for the
  third measuring program.

Checks: PSP system tests 85 groups with the test programs; the measuring program against the PSP's results.

## PSP core: where the GE samples, as the PSP measured it — 2026-10-04

Branch `cursor/psp-ge-sampling-2b67`, stacked on `cursor/psp-vfpu-prefixes-2b67` (for stack #106). From the GE
pictures the user's PSP drew (docs/psp-core.md, "Fixed since"), in ares/psp/ge/draw.cpp:
- triangles are covered, and their colors, depth and texture coordinates blended, at each pixel's middle (8/16), not
  PPSSPP's 7/16: `coverage-triangles` identical in all 256 cells, `shared-edges`, `gouraud`, `3d-rounding`,
  `3d-cull` too;
- sprites: the top-left rule at the middle, the left edge a sixteenth further left (`coverage-sprites`);
- texture coordinates stepped from the leftmost corner with steps cut short to 1/65536 when inexact (`shortStep`):
  all four `texels-*` files identical, `filter-shrink` kept;
- 49 of 63 pictures identical (40 before). tests/psp's triangle, clipping and fog expectations moved to the middle
  sample point.

Next (GE): the filter's weights, lighting's and fog's rounding, perspective texels, the 3D sprite, the depth layout.
Idea: commit the 49 identical pictures (about 1.1 MB with xz) and make the measure test fail if one changes.

Checks: PSP system tests 84 groups with the test programs (the samples still within a level of PPSSPP); the measure
comparison against the PSP's results.

## PSP core: the VFPU's prefixes, as the PSP measured them — 2026-10-04

Branch `cursor/psp-vfpu-prefixes-2b67`, stacked on `cursor/psp-vfpu-nans-2b67` (for stack #106). The recorder's 240
random prefix combinations, worked out entry by entry (docs/psp-vfpu-measurements.md, round 2, "Prefixes"), in
ares/psp/cpu/interpreter-vfpu.cpp:
- `vrcp` takes the prefixes on its last lane alone with lane 0's settings (lanes 0-2 plain; a constant setting
  honoured, one naming another lane giving 0; the destination's mask and clamp on lane 3), like `vrndi`.
- `vfad`: t forced to constants (1, or 1/3 with the absolute bit, signs kept); `vhdp`: s's last lane forced the
  same way; `vscl`: t's swizzle ignored, its other settings per lane.
- `outsideLanes`: a swizzle past the operand's size gives 0 as that lane's result, in the lane-by-lane instructions
  (the helpers, vabs, vneg, vmin, vmax, vscmp, vscl).
- The recorder: 1157 of 1216 entries match, 49 more only by rounding; the last 10 are the adders' rounding beyond 4
  ulps and one vscl lane that may be flushed before rounding. Unmeasured, for a third round: the other math
  functions with prefixes, out-of-range swizzles in sums and conversions, vavg with prefixes.

Checks: Allegrex tests 56 groups (prefix cases from recorded runs in `vfpu edges`); PSP system tests; mutations of
vrcp's rule, the out-of-range rule and vfad's t prefix each fail the tests.

## PSP core: the VFPU's NaNs, denormals and comparisons, as the PSP measured them — 2026-10-04

Branch `cursor/psp-vfpu-nans-2b67`, stacked on `cursor/psp-vfpu-fixes-2b67` (for stack #106). Rules worked out from
the recorder's inputs and checked on every recorded run (docs/psp-vfpu-measurements.md, round 2), all in
ares/psp/cpu/interpreter-vfpu.cpp:
- `vfpuOrder`: how `vmin`, `vmax`, `vsrt1`-`vsrt4`, `vsgn` and `vscmp` order values (sign and magnitude, denormals
  as zero, a NaN past the infinity of its sign); they give back the lanes' own bits. Ties: t's lane for `vmin` and
  `vmax`; for the sorts, the pair's first lane in both (vsrt1, vsrt2) or its second (vsrt3, vsrt4).
- `vsat0`/`vsat1` keep NaNs and in-range lanes (denormals too; vsat0 zeroes a set sign bit); `vmov` copies bits;
  `vsbz` leaves denormals; `vlgb` of a NaN moves its low byte up 16 bits (7 of 7 recorded).
- The VFPU's NaN for `vavg`, `vbfy1`/`vbfy2`, `vcrs` (product sign), `vcrsp`, `vdet`, `vfad`, `vhdp`, `vhtfm`,
  `vmmul`, `vmscl` and `vscl` (product sign), `vocp`, `vqmul`, `vsocp`, `vtfm`; an infinity's sine and cosine are
  NaN (vsin, vcos, vnsin, vrot).
- compare.cpp counts each recorder entry's differing words by kind, and says how many entries differ only by
  rounding. The recorder: 1129 of 1216 entries match (963 before), 38 more only by the adders' rounding; the rest
  are prefixed entries.
- docs: the published finding that `vsgn`/`vscmp` see denormals was wrong (it's NaNs); corrected.

Checks: Allegrex tests 56 groups (a `vfpu edges` case for each rule, interpreter and recompiler); PSP system tests;
mutations of `vfpuOrder`'s denormal rule, vsrt3's tie and the sine of infinity each fail the tests.

## PSP core: the VFPU's matrices and divu by zero, as the PSP measured them — 2026-10-04

Branch `cursor/psp-vfpu-fixes-2b67`, stacked on `cursor/psp-measured-2b67` (for stack #106). The first two fixes
from round 2 (docs/psp-vfpu-measurements.md):
- `vmmul` is rs turned on its side times rt (rd[r][c] = the sum of rs[k][r] * rt[k][c]), and `vtfm`/`vhtfm` dot the
  matrix's columns with the vector (ares/psp/cpu/interpreter-vfpu.cpp). Replayed against the recorder, every run
  without a NaN or infinity in its inputs now matches up to the adders' rounding (it differed in 59-64 of 64 before).
  The matrix test's second matrix isn't symmetric any more (twice the identity hid the order), and a case is
  encoded as pspdev's assembler encodes `vmmul` (rs transposed).
- `divu` by zero gives lo 0xffff for a dividend below 0x10000 (interpreter-ipu.cpp). `ipu-divide.bin` is in
  tests/allegrex/measured/ (xz, 100 KB), and the measured test group replays its 8192 pairs.

Checks: Allegrex tests 54 groups (interpreter and recompiler), PSP system tests 84 groups with the test programs;
the old `vmmul` order and a wrong `divu` boundary (0x8000) each fail the tests.

## PSP core: the user's measurements, round 2 and the GE — 2026-10-04

Branch `cursor/psp-measured-2b67`, stacked on `cursor/psp-vfpu-awake-2b67` (for stack #106). The user ran VFPU
rounds 1 and 2 and the GE program on their PSP; the raw results are in `.local/psp-measure-2026-10-04/` (not in the
repository), their SHA-256 and manifests in `tests/allegrex/measured/` (`SHA256SUMS2`, `manifest2.txt`, `ops.txt`)
and `tests/psp/measured/`. Findings written up in docs/psp-vfpu-measurements.md ("Round 2") and docs/psp-core.md
("Results from the user's PSP"):
- round 1 repeats byte for byte; `vh2f`/`vf2h` exact; division by zero's results (the core's `divu` LO is wrong);
  both FPU tests switched the PSP off twice and were given up on (likely special inputs; a probe is next);
- the recorder: 963 of 1216 entries match. The core reads matrix operands the other way round (`vmmul` gives
  M000 × M100 where the core gives M100 × M000; `vtfm`/`vhtfm` dot columns, the core rows), the most important fix;
  then NaN results, kept denormals, NaNs in comparisons, some prefixes (6 entries where the core leaves a lane
  unwritten), and rounding; `vlog2` above 4 and the adders now have the data for their fits;
- the GE: 40 of 63 picture files identical (blending, texture functions, dithering, stencil, formats), the controller
  as uOFW reads it; of the 23 that differ, 21 differ from PPSSPP's software renderer at the same pixels: coverage,
  the rounding onto the screen (PPSSPP's +0.375 is wrong), interpolation, the filter, lighting's rounding by a level,
  and a depth buffer laid out differently in VRAM. Sprite edges measured: columns at 9/16, rows top 8/16, bottom 9/16.

Next: the fixes, one at a time against these files, the matrix orientation first.

## PSP core: the VFPU measurement keeps the PSP awake — 2026-10-04

Branch `cursor/psp-vfpu-awake-2b67`, stacked on `cursor/psp-lighting-2b67` (for stack #106). The user's first round-2
run switched the PSP off around the divide step. The divide is the bare instruction (checked in the built program), so
the likelier causes are the power-save timer or a crash in the FPU tests after it, which no PSP had run. The program
now keeps the PSP awake (`scePowerTick`), names each test as it starts, runs the FPU tests last, and retries a test
that didn't finish once before giving up on it (`<name>.stopped`), so a test that stops the PSP can't hold up the rest.
Its files are unchanged (byte for byte in PPSSPPHeadless), and the retry and give-up paths were tried there with
planted `.part` and `.again` files. Five Bugbot passes, each read in full (every summary said "no bugs"). Four
findings were fixed: the retry marker written before the file opened, a failed write counting as the PSP stopping,
long tests between progress ticks, and a marker that can't be written letting a test retry forever. The fifth (a
retry that stops between its last write and the rename to `.bin` is given up on) was left: the window is one close and
one rename, and giving up renames the file rather than deleting it, so a complete `.stopped` can be renamed by hand.
The revised EBOOT.PBP is on the RP6 in `Download/VFPUMEASURE2` from 10:19; the last two fixes wait for the next time
it's connected (neither changes a normal run).

## PSP core, part 12: lighting — 2026-10-04

Branch `cursor/psp-lighting-2b67`, stacked on `cursor/psp-3d-measure-2b67` (for stack #106). `ares/psp/ge/lighting.cpp`:
- the material (emissive, ambient, diffuse, specular, coefficient; the vertex's color standing in by MATERIAL_COLOR);
- the ambient light and four lights (directional, point, spot; diffuse, specular, powered), with fading and cones;
- the GE's arithmetic (2c + 1 colors, shares in 512ths rounded up, its quick power, the coefficient's 4-bit fraction);
- the shine kept apart and added after texturing (LIGHT_MODE 1);
- environment mapping's texture coordinates from two lights.

Checks:
- 84 groups on the Mac (UBSan), with five lighting groups worked out by hand;
- pspsdk's "celshading" matches PPSSPP's software renderer pixel for pixel, "envmap" within 1 level;
- 21 broken versions each failed the tests (two only after a case at a cosine of 0.501 was added).

Not yet: lines, mipmaps, curved surfaces, bounding boxes, PRIM kind 7.

A second commit adds five lighting cases to `tools/psp-ge-measure` (diffuse and the shine across the angles, a
spotlight's direction either way, a point light's fading, environment mapping), all identical to PPSSPP's software
renderer here; 61 of the program's 64 files now match it. While they were being written, an unfinished set of lighting
cases appeared in `main.c` from another writer (a Cursor agent worker is registered for this folder). The user said
nothing else was running, so the two sets were merged into one: the other set's cell helper, its finer angle sweep
and its resets in `start()`, with these halves and cases. Copies of both versions are kept outside the repository in
`.local/`. The refreshed EBOOT.PBP replaces the one on the RP6 in `Download/GEMEASURE`.

## PSP core: measuring 3D on a PSP — 2026-10-04

Branch `cursor/psp-3d-measure-2b67`, stacked on `cursor/psp-3d-2b67` (for stack #106). `tools/psp-ge-measure` gains
nine 3D cases (perspective texels, depths and fog on a receding floor; a 3D sprite; the screen rounding; the near
plane's cut with DEPTH_CLIP_ENABLE on and off; which depths stop triangles, points and sprites; culling). Against
PPSSPP's software renderer 56 of the 59 files match; the three that differ are for the PSP to settle (docs/psp-core.md).
The cases found a rounding difference in part 11's clipping, fixed there (a second commit on `cursor/psp-3d-2b67`).
The updated EBOOT.PBP replaces the one on the RP6 in `Download/GEMEASURE`.

## PSP core, part 11: drawing in 3D — 2026-10-04

Branch `cursor/psp-3d-2b67`, stacked on `cursor/psp-ge-measure-2b67` (for stack #106). The GE draws outside through
mode:
- vertices scaled to fractions, morphed and skinned;
- the world, view and projection matrices, the viewport and the GE's rounding to the sixteenth;
- the rules for what isn't drawn (off the screen, depths, z / w, negative w), with DEPTH_CLIP_ENABLE's clamp;
- triangles cut at the near plane;
- culling (strips flipping);
- perspective-correct texture coordinates, with the texture matrix's projection and TEX_SCALE/TEX_OFFSET;
- fog, the depth range test, and 3D sprites and points.

Checks:
- 77 groups pass on the Mac (UBSan) and in `phobos-linux` (ASan and UBSan, with the programs);
- pspsdk's "cube" sample is within 1 level of PPSSPP's software renderer on every pixel;
- 26 broken versions each failed the tests (five only after new tests).

The `PSP_TEST_PROGRAMS` readers now treat an empty value as unset; the loader's test hit undefined behavior on one.
Not yet: lighting, environment mapping, PRIM kind 7, lines, mipmaps, curved surfaces. Next: lighting (PPSSPP's
Lighting.cpp and TransformCommon read for behavior; "celshading" and "envmap" samples to compare).

## PSP core: measuring the GE and the controller on a PSP — 2026-10-04

Branch `cursor/psp-ge-measure-2b67`, stacked on `cursor/psp-draw-2b67` (for stack #106). `tools/psp-ge-measure` is a
homebrew program that records what a real PSP's GE draws where parts 8-10 follow PPSSPP or uOFW instead of our own
measurements: blending's and the texture functions' rounding, the filter, which pixels sprites and triangles cover,
dithering, the stencil's steps, the 16-bit formats both ways, texel mapping, and the controller's timing (the latch
question). 51 files, about 13 MB. `tests/psp/measure.cpp` runs the same program in the core and, with
`PSP_GE_RESULTS`, lists what differs from a results folder (docs/psp-core.md, "Measuring the GE and the controller on
a PSP"). Checks: 67 groups on the Mac (UBSan) and in `phobos-linux` (ASan and UBSan) with the programs; against
PPSSPP's software renderer 48 of the 50 result files match (first written here as "49 of 51", counting the
manifest), and the two that differ (a sprite edge exactly through pixel middles, a shrunk sprite's texels) are for the
PSP to settle. `HostFolder` and `testProgram` moved into `tests/psp/kernel-machine.hpp` so the test files share them.
The EBOOT.PBP went to the RP6 in `Download/GEMEASURE` for the user to run with the VFPU's round 2 (since replaced by
the version with 3D cases, above).

## PSP core, part 10: drawing in 2D — 2026-10-03

Branch `cursor/psp-draw-2b67`, stacked on `cursor/psp-ge-2b67` (for stack #106). The GE draws in through mode:
sprites (with the corner order's quarter turn), triangles, strips, fans and points; textures in every format but DXT,
with the palette, swizzling, repeat and clamp, nearest and filtered; the texture functions; the whole pixel pipeline
(alpha, color, stencil and depth tests, blending, dithering, logic operations, masks). Arithmetic as PPSSPP's software
renderer has it (checked by its authors on PSPs). Checks: 66 groups on the Mac (UBSan) and in `phobos-linux`; pspsdk's
"blit" (three ways) and "doublelist" pixel-exact; `tools/psp-test-programs/compare-ppsspp.sh` puts "blend" within 2
levels of PPSSPP's software renderer everywhere and "clut" on 99.94% of pixels; fifteen broken versions each failed.
The test script now turns off the address sanitizer's stack-use-after-return check, which made the per-pixel code
hundreds of times slower (95 seconds instead of 18 minutes in the container).
Not yet: lines, DXT, mipmaps, 3D. Next: 3D (transforms, clipping, lighting).

## PSP core, part 9: the GE's display lists — 2026-10-03

Branch `cursor/psp-ge-2b67`, stacked on `cursor/psp-files-2b67` (for GitHub stack #106, which the user keeps). The GE
(`ares/psp/ge/`) runs display lists by itself: commands kept for `sceGeGetCmd`, matrices, BASE and offset addressing,
two levels of CALL, stall addresses, FINISH/SIGNAL/END; every vertex layout; clearing and block transfers drawn (the
rest of drawing is next). Its driver (`kernel/ge.cpp`) follows uOFW's reading of the PSP's: the queue of 64 lists,
syncing and waiting, callbacks, the SIGNAL kinds (SUSPEND holds the GE until its callback returns), saved state. Calls
into the program (`kernel/interrupts.cpp`) run as interrupt handlers on top of whatever thread is running, held back
by `sceKernelCpuSuspendIntr`, unable to wait. Also event flags, exit callbacks, the clocks and `sceRtc` ticks, and
`Kernel::picture()` (the screen's pixels). Checks: 53 groups on the Mac (UBSan) and in `phobos-linux` (ASan and
UBSan, with the real programs: a GU program of Phobos's own and pspsdk's sample `copy`, built from source);
twenty-six broken versions each failed. Reviews (read in full): the PAUSE callback's id, BASE being cleared for the
next list, and the next list running before a finish callback fixed; event flags checked
against pspautotests' results from a PSP (a failed poll does tell the bits, as a reviewer doubted; the check order,
timeouts, deletion and zero timeouts fixed to match); two findings were the PSP driver's own behavior per uOFW, now
commented. Found on the way: the sample `copy` never waits, so a fixed number of frames took the
address sanitizer's interpreter minutes; it now runs until the screen has swapped four times. Next: drawing (textures,
blending, the tests) for pspsdk's other 2D samples.

## PSP core, part 8: files and controls — 2026-10-03

Branch `cursor/psp-files-2b67`, stacked on `cursor/psp-hle-2b67` (to be added to GitHub stack #106, which the user
keeps; the API can't add to stacks). Devices are host folders (`Kernel::mount()`), with paths normalized inside the
kernel so nothing outside a device's folder can be reached, names found whatever their case, and 15 file and folder
functions; the controller, following uOFW's ctrl.c, is sampled at each vertical blank or on the program's own
timer and keeps its last 64 samples (peek, read with how many are new, negative, latch, analog stick, one waiting
reader). `Kernel::run()` now budgets the PSP's time rather than instructions. A second test program
(`tools/psp-test-programs/system`) uses files and the controls through newlib and runs from start to end. Checks: 34
groups on the Mac (UBSan) and in `phobos-linux` (ASan and UBSan, with the real programs); fourteen broken versions
each failed (the case one on Linux only, macOS's file system ignoring case itself). Reviews (read in full) found
symlinks leading out of a device's folder, an overflowing controller sample count, the sampling cycle unused (now a
timer) and backslashed program paths, all fixed; the last found the latch emptied for a second waiting thread, and
uOFW showed the latch read doesn't wait and buffer reads wait only when nothing is new (always waiting halved the
speed of a program that waits for the frame and then reads), so the controller was reworked. Their claim about
`sceIoLseek`'s registers was disproved by psp-gcc's own code (offset in a2/a3, whence in t0). Open for the PSP: does
a second latch read in one sampling cycle wait (pspsdk's notes) or not (uOFW)? Next: the GE (phase 5).

## PSP core, part 7: the first HLE functions — 2026-10-03

Branch `cursor/psp-hle-2b67`, stacked on `cursor/psp-loader-2b67`: `ares/psp/kernel/` gets the HLE kernel. NIDs
are computed from names (SHA-1; all 46 of hello world's imports matched pspsdk's names), 54 functions (threads,
semaphores, lightweight mutexes, memory, standard output, display, exit), a priority scheduler with saved contexts,
and a clock at 333 MHz with the vertical blank. pspdev's hello world (now also printing to standard output, which
the tests read) runs from start to end on the host, as a static executable, a PRX and an EBOOT, on both engines.
Error codes pspsdk lacks come from uOFW's `errors.h` (recollected values were wrong, and corrected before commit).
A second Bugbot review (its full answer read, as the user asked for every review; the returned summary showed one
of the three) found start arguments copied past a thread's stack or from bad addresses, and the program's memory
reserved elsewhere when taken; a third, that loading after an exit ran nothing and a failed load left memory
reserved (load() now starts afresh, and resets again if it fails); a fourth, the program's path passed to its first
thread without the same check (now up to 4 KiB): all fixed, with tests the old code fails. Checks: 22 groups on the Mac (UBSan) and in
`phobos-linux` (GCC 13, ASan and UBSan, with the real programs); four broken versions each failed. Next: files
(`sceIo*` on a host folder), controls, then the GE (phase 5).

## PSP core, part 6: the loader — 2026-10-03

Branch `cursor/psp-loader-2b67`, stacked on `cursor/psp-memory-2b67`: `ares/psp/kernel/loader.cpp` loads static
executables, PRXs (relocations: 26-bit jumps, pointers, `lui` with its lower halves) and `EBOOT.PBP`'s program; reads
the module info; patches each import's stub to `jr ra; syscall n`; lists the exports; refuses with a reason
(encrypted programs named from their `~PSP` header, per the PSP Developer Wiki page the user saved). Checked against a
real hello world built by pspdev (`tools/psp-test-programs/build.sh`, source only; `PSP_TEST_PROGRAMS`), which shows
what part 7 needs: about 45 functions from 11 libraries. Checks: 12 groups on the Mac (UBSan) and in `phobos-linux`
(ASan and UBSan, with the real programs); three broken versions each failed. Open: whether relocation segment numbers
add a segment's address or its displacement, which only multi-segment retail modules can show (phase 7).

## PSP core, part 5: the memory map — 2026-10-03

Branch `cursor/psp-memory-2b67`, stacked on `cursor/psp-vfpu-round2-2b67`: `ares/psp/memory/` (the scratchpad,
VRAM and its four copies, 32 or 64 MiB of main RAM, the four address windows; `written()` for the recompiler,
`unmapped()` for accesses with nothing behind them; the page table; copies for the loader and HLE). The PSP
system gets its own test suite, `tests/psp/run-tests.sh` (6 groups, in the PSP Core Tests workflow as a second
job). Bugbot found that VRAM's copies in the page table let a compiled store through one copy change code compiled
from another unseen; now only the first copy is listed, and the recompiler interprets code wherever the table has
nothing (a rule written into the CPU's `pages` comment). Checks: passes on the Mac (UBSan) and in `phobos-linux`
(GCC 13, ASan and UBSan); four broken versions each failed it; the CPU's 54 groups still pass on both. Next in
phase 4: the ELF/PRX/PBP loader, then the first HLE functions.

## PSP core: a second round of VFPU measurements — 2026-10-03

Branch `cursor/psp-vfpu-round2-2b67`, stacked on `cursor/psp-vfpu-exact-2b67`. `tools/psp-vfpu-measure` now asks
which round to run (O: the first; X: the second, about 230 MB, `manifest2.txt`). Round 2 records what round 1
couldn't settle: `vlog2` over a whole binade per result size above 4; dot products, sums and averages built to show
how the VFPU adds several numbers; every half float through `vh2f` and a million floats through `vf2h`; `div` and
`divu` (by zero too) and the FPU's conversions and arithmetic in each rounding mode; and the instruction recorder:
every VFPU instruction, size and immediate pspdev's assembler accepts (1216 entries in `ops.h`, generated by
`ops.py`), each copied into a stub and run on 64 random register states, recording matrix 2 and the condition
codes. The recorder runs last, since a PSP stops a program at an instruction it doesn't have. `compare.sh` checks
whatever a folder holds from either round. Tried first in PPSSPP's PPSSPPHeadless (`make SMOKE=1`; built in
`phobos-linux` at `/opt/tools/ppsspp` without SDL, `HEADLESS_CROSS=ON`): every file written at its expected size
and parsed by `compare.sh`. Waiting on the user's PSP run.

## PSP core: VFPU log2 exact below 4 — 2026-10-03

A third commit on `cursor/psp-vfpu-exact-2b67`. `vlog2`'s table is fixed point (units of 2^-24 over [1, 2)),
which the same interpolator fits; the core adds the exponent as a whole number and truncates to 22 bits after the
point and 23 significant bits. Below 1 the PSP takes a cheaper path, found from the full sweep: a straight line
per segment (the table's first value cut to 17 bits after the point, its slope missing its low 9 bits, no squared
term) and the magnitude truncated to 15 bits after the point, whatever its size; `fit.py` checks it against all
2^23 results below 1. Exact on all 16.8 million results from 1/2 up to 2 and on every spread-out input below 4;
from 4 up about half are one unit high (88.35% of the spread-out set overall), which needs whole binades above 4
from a second round of measurements. The measured group replays vlog2's spread-out sample (inputs below 4) and
every 1024th result from 1/2 up to 2.

## PSP core: VFPU sine and cosine exact — 2026-10-03

A second commit on `cursor/psp-vfpu-exact-2b67`: the table behind `vsin` and `vcos` turned out to be cosine's (sine
is it read backwards), which fits the same interpolator; very large arguments wrap as the hardware's 5-bit shift
does (a shift of exactly 32, 64 or 96 gives 0). `vsin`, `vcos`, `vnsin` and `vrot` now use it: exact on every
full-range and spread-out input. Ten math instructions are exact; `vlog2` and `vdot` remain. Checks: 54 groups pass
on the Mac and in `phobos-linux`; the measured group replays the first 16384 results of each of the ten.

## PSP core: seven VFPU math functions exact — 2026-10-03

Branch `cursor/psp-vfpu-exact-2b67`, stacked on the measurements. `tools/psp-vfpu-measure/fit.py` fits the
quadratic interpolator published about the hardware to the user's PSP's results, and the core now computes `vrcp`,
`vnrcp`, `vrsq`, `vsqrt`, `vexp2`, `vrexp2` and `vasin` with those coefficients (`ares/psp/cpu/vfpu-segments.hpp`,
generated): exact on all of their full-range results and on a million spread-out inputs each.

- **Found on the way:** `vsqrt`/`vrsq` ignore the input's lowest bit; `vrexp2` reads `vexp2`'s table backwards,
  as `vexp2` does for negative inputs; `vcos` is `vsin` backwards; `vsqrt(-0)` is +0; `vsin` is fixed point and
  doesn't fit the model yet (nor does `vlog2`).
- **Checks run:** `tests/allegrex/run-tests.sh`, all 54 groups pass on the Mac (UBSan, which caught an
  out-of-range shift in the first version) and in `phobos-linux`; the measured group now also replays the first
  16384 spread-out results of each of the seven (0.2 MB). `compare.sh` on the full data: the table in
  psp-vfpu-measurements.md.

## PSP core: the VFPU against the user's PSP — 2026-10-03

Branch `cursor/psp-vfpu-measured-2b67`, stacked on the measurement program. The user ran it (firmware 6.61) and
left `results/` (455 MB) on the RP6; it's in `.local/psp-vfpu-measure/results/`, outside the repository.

- **Findings** ([psp-vfpu-measurements.md](psp-vfpu-measurements.md)): the random number generator matches all
  263,944 words; `vadd`, `vsub`, `vmul`, `vdiv` match every non-NaN result; the VFPU's NaN is always `0x7f800001`
  with a sign that depends on the instruction (now in the core, `NaNSign`, so those four match fully); the math
  functions keep 22 bits and work on fixed-point arguments (sine, cosine, arcsine), and `vdot` sums differently.
- **In the repository**, as the user chose: the write-up, `tools/psp-vfpu-measure/compare.sh` (checks the core
  against a results folder), and in `tests/allegrex/measured/` the manifest, the SHA-256 of all 27 files and the
  generator and arithmetic files (5.2 MB compressed), which a new test group checks the core against. The math
  tables stay with the user.
- **Checks run:** `tests/allegrex/run-tests.sh`, all 54 groups pass on the Mac and in `phobos-linux`; reverting
  `vadd`'s NaN rule fails the new group.
- **Firmware:** the user chose targeted reverse engineering of their own firmware where measurements can't answer
  a question, written up as specifications, never code, with the firmware kept out of the repository.

## PSP core: measuring the VFPU on a real PSP — 2026-10-03

`tools/psp-vfpu-measure`, a homebrew program that records what a PSP's VFPU computes (math functions over their
whole reduced ranges and a million spread-out inputs, the random number generator from 64 seeds, arithmetic edge
cases), for exact math functions fitted from our own data, as the user chose. Built in `phobos-linux` with
pspdev's ARM64 toolchain (installed in `/opt/pspdev`); the build is in `.local/psp-vfpu-measure/EBOOT.PBP`, not in
the repository. Its VFPU instructions were checked in the disassembly against the core's decoder. Waiting on the
user to run it on their PSP and bring back `results/` (about 450 MB). psp-core.md has the details.

## PSP core: the VFPU's random number generator — 2026-10-03

Branch `cursor/psp-vfpu-random-2b67`, stacked on part 4. The user chose, for the VFPU's accuracy: the real random
number generator, implemented from the published research (fp64's, PPSSPP issue 16946) with no PPSSPP code; and
exact math functions from measurements on the user's own PSP (a dump program comes next).

- **What it is:** `vfpuRandom()`, `VRNDS`, `VRNDI`, `VRNDF` in `interpreter-vfpu.cpp`, plus the RCX registers'
  power-on values and their 20-bit width. psp-core.md has the details.
- **Checks run:** `tests/allegrex/run-tests.sh`, all 53 groups pass on the Mac (UBSan) and in `phobos-linux`; the
  new group checks the generator's state after 3, 4, 14 and 19 draws against the states fp64 recovered from a PSP,
  the seeding layout, the lane order and the prefix quirk.

## PSP core, part 4: loads and stores straight to RAM — 2026-10-03

Branch `cursor/psp-fastmem-2b67`, stacked on part 3. The user approved parts 1-3 and asked for the whole PSP feature
to be stacked and merged at once, so the stack stays open as it grows.

- **What it is:** compiled loads and stores look up a page table the CPU's owner provides (`Allegrex::pages`) and
  reach host memory directly, with the interpreter as the slow path (misaligned addresses, hardware registers, and
  stores into pages holding compiled code, which `writePages` leaves out so the store drops that code).
  [psp-core.md](psp-core.md#the-recompiler) has the details.
- **Checks run:** `tests/allegrex/run-tests.sh`, all 51 groups pass on the Mac (UBSan) and in `phobos-linux` (GCC
  13, ASan and UBSan). The test machine's page table leaves one RAM page out, which the generated programs also
  load and store through, and a test counts that only the store to that page reached write(). Three deliberate
  mistakes (stores allowed into pages with compiled code, `lh` without sign extension, no alignment check) each
  failed the tests.
- **Speed:** the benchmark loop runs at 1206 million instructions a second on the Mac, against 505 without the page
  table and 150 on the interpreter (`SANITIZE= ALLEGREX_BENCHMARK=1 tests/allegrex/run-tests.sh`).

## PSP core, part 3: the VFPU — 2026-10-03

Branch `cursor/psp-vfpu-ares-2b67`, stacked on part 2 (`cursor/psp-recompiler-2b67`), for the user to review.
[psp-core.md](psp-core.md#part-3-the-vfpu) has what it covers and assumes.

- **What it is:** `ares/psp/cpu/interpreter-vfpu.cpp`, every VFPU instruction in ares's style (one function per
  mnemonic, decoder tables in `interpreter.cpp`), from the earlier stashed work rewritten. Two of its old test
  expectations were wrong and are fixed: vcst 8 is pi/2 (the constants are numbered from 1), and a prefix
  constant is picked by the swizzle and absolute bits together. The recompiler runs it through the interpreter.
- **Checks run:** `tests/allegrex/run-tests.sh`, all 51 groups pass on the Mac (UBSan) and in `phobos-linux` (GCC
  13, ASan and UBSan): ten VFPU groups on both engines, and the generated programs now mix in VFPU instructions.
- **Prefixes, from a review:** an instruction that raises an exception leaves the prefixes, and `vmfvc`/`vmtvc`
  don't use them up (as in PPSSPP, where pspdev's documentation is silent), so `vmtvc` can set one; `vnop` does use
  them up, as the documentation says.
- **The stashes** of the VFPU work before the rewrite were dropped, with copies kept outside the repository.

## PSP core, part 2: the recompiler — 2026-10-03

Branch `cursor/psp-recompiler-2b67`, stacked on part 1 (`cursor/psp-core-2b67`), for the user to review. As the user
directed, the Allegrex gets a recompiler from the start, with the interpreter as its fallback and reference.
[psp-core.md](psp-core.md#the-recompiler) has the design and what it doesn't do yet.

- **What it is:** `ares/psp/cpu/recompiler.cpp` and `recompiler-ipu.cpp`, on ares's sljit framework like the N64 CPU.
  The common integer instructions and the branches are native; everything else, including every instruction that
  can raise an exception, calls the interpreter's own path (`execute()`).
- **Checks run:** `tests/allegrex/run-tests.sh` runs every test group on both engines, recompiler cases, and 500
  generated programs that must end in the same state on both; all 31 groups pass on the Mac (ARM64, UBSan) and in
  the `phobos-linux` container (Ubuntu 24.04 ARM64, GCC 13, ASan and UBSan). sljit's own call checks are on in the
  tests. Four deliberately broken versions of the recompiler each failed the tests. The x86-64 backend runs first in
  CI (the PSP Core Tests workflow, on x86-64 runners).
- **The `phobos-linux` container** (Docker, at the user's suggestion, kept for later Linux checks): Ubuntu 24.04 with
  GCC 13, CMake and Python, the repository mounted read-only at `/phobos`; run
  `docker exec phobos-linux bash -c 'cd /phobos && tests/allegrex/run-tests.sh'`. It was created with
  `--security-opt seccomp=unconfined` because Docker Engine 20.10.8 blocks the `clone3` call Ubuntu 24.04's C
  library uses (fixed in 20.10.10).

## PSP core, part 1: the Allegrex CPU — 2026-10-03

Branch `cursor/psp-core-2b67`, for the user to review. The user chose an original PSP core over PPSSPP (whose GPL
can't cover the firmware compiled into the native library), with PPSSPP readable as a reference, the published keys
for retail executables, and firmware optional per module family. [psp-core.md](psp-core.md) has the decisions,
sources, design and phases.

- **Part 1:** `ares/psp/cpu/`, an interpreter for the Allegrex's integer and FPU instructions. It sits under
  `ares/` as the other systems do, written in ares's style (nall types, mnemonic-named instruction functions,
  macro decoder tables, memory through virtual `read()`/`write()` like the ARM7TDMI) with plain-language comments,
  as the user asked. Not in the app.
- **Next, as the user directed:** a recompiler from the start, on ares's sljit framework like the N64 CPU, with this
  interpreter as its reference and fallback, and its MIPS-generic parts reusable by other MIPS systems; then the
  VFPU. psp-core.md has the plan.
- **Checks run:** `tests/allegrex/run-tests.sh` on the Mac, 14 groups pass with the undefined-behavior sanitizer
  (the address sanitizer's runtime hangs at start on this macOS, so the script uses it on Linux only); the new
  PSP Core Tests workflow runs both sanitizers. The CPU is built against nall and ares's types alone
  (`tests/allegrex/prelude.hpp`). The binutils Allegrex test's assembled examples serve as vectors.
- **Not checked against hardware:** division by zero, FPU arithmetic outside round-to-nearest, conversions of NaN
  or out-of-range values (psp-core.md lists what the code assumes).

## N64 hack bugs narrowed — 2026-10-03

Docs only (branch `cursor/n64-hack-notes-2b67`, stacked on `cursor/cleanups-2b67`). The plan rows "Super Mario 64
B3313 v1.0.2 Hotfix 3 stops at boot" and "F-Zero ZX Overdrive's picture" have what N64 Debug Logging showed: B3313
v1.0.2 boots into RAM and spins taking exceptions, and Overdrive runs but its scanned-out framebuffer stays black.
On the RP6, N64 Debug Logging was switched on and Asynchronous RDP off for the runs, then both were set back
(logging off, Asynchronous RDP on); Phobos was force-stopped after each run, so no auto state was written.

## MSX 40-column text — 2026-10-03

Branch `cursor/msx-text-columns-2b67`. The plan row "MSX 40-column text" has the details: ares's TMS9918 and V9938
drew text mode across the whole line, so each row's first character showed again at the right edge; the 40 columns
now sit between eight-pixel borders.

- **Checks run:** the modern release builds. On the RP6, with the user's MSX BIOS and a cartridge written for the
  test that returns at once, BASIC's screen showed the stray column before and doesn't now. The test cartridge and
  the empty save folder it left were removed afterwards.
- **Not checked:** an SG-1000, SC-3000 or ColecoVision program in text mode (same code), an MSX2.

## Cleanups — 2026-10-03

Branch `cursor/cleanups-2b67`: the cleanups the user picked on 2026-10-03.

- NES and SNES loads no longer log `VFS: FAILED to allocate Gamepad on Expansion Port`. `connectDevices` gave every
  port with "Port" in its name a gamepad, and these consoles' Expansion ports take none; it now skips them, as it
  does the Mega Drive's Extension port.
- The FBNeo credit is gone from `neo-geo-compatibility.md`, which now credits MAME alone, as the licensing audit
  found. The plan's licensing row records the user's four answers, and queue item 7 is parked (the firmware stays,
  so no GPL cores come in).
- Stale notes: queue item 6 still had Pocket Challenge V2 on its branch (it's on master), and the 2026-09-24
  follow-up list now says the debug load's cold-start crash was fixed.
- `stash@{0}`, an early LaserActive work-in-progress superseded by PR #81, was dropped as the user asked, with a copy
  kept outside the repository.
- **Checks run:** the modern release builds, 238 host tests pass (no Kotlin changed). On the RP6, 8 Eyes (NES) and
  Chrono Trigger (SNES) got gamepads on both controller ports, nothing on the Expansion Port and no error, at
  60.2 FPS.

## Boot pass of every game on the RP6 — 2026-10-03

Docs only (branch `cursor/boot-pass-notes-2b67`, stacked on `cursor/load-failures-m3u-2b67`). With the build of that
branch, every game on the RP6's SD card outside the Neo Geo folder was cold-started through the adb load intent: 71
from 24 folders (the two 64DD IPL files in `64dd` are firmware). A screenshot and the log were taken after 25 s (40 s
for N64 and CD games), and Phobos was force-stopped in between, so no auto state or save was written. The Neo Geo sets
had their own pass (section "Neo Geo Z80 banks for M ROMs over 64 KiB").

- **All 71 run at their systems' rates, with no crashes:** Atari 2600, ColecoVision, Master System, Game Gear,
  SG-1000, Mega Drive and 32X at 59.9 FPS; Game Boy, Game Boy Color and Game Boy Advance at 59.8; NES and SNES at
  60.1–60.2; PC Engine, PlayStation and N64 at 59.9–60.0; Neo Geo CD at 59.2; Neo Geo Pocket at 60.0; WonderSwan Color
  at 75.5; ZX Spectrum at 50.1–50.8. The screenshots show titles, intros or demos. A second look at 20 and 55 s
  cleared the blank ones: both Link's Awakenings, Ape Escape, Castle of Illusion and Judgement Silversword were
  mid-intro.
- **Found:** Super Mario 64 B3313 v1.0.2 Hotfix 3 stops at boot, and F-Zero ZX Overdrive's picture is garbled (plan
  rows of those names). The F-Zero X Expansion Kit's `.ndd` on its own stays black, as expected: the disk needs
  F-Zero X in the cartridge slot, with the disk inserted from its pause menu (README). Metal Gear Solid's playlist led
  to the fix in the next section.
- **Logged but harmless:** every NES and SNES load logs `VFS: FAILED to allocate Gamepad on Expansion Port`, and the
  games play. ZX Spectrum tapes wait at the Sinclair screen until `LOAD ""` (the keyboard's tape controls).

## Playlists from frontends, and games that don't start — 2026-10-03

Branch `cursor/load-failures-m3u-2b67`, on master. The plan row "Playlists from frontends, and games that don't start"
has the details.

- **Checks run:** the modern release builds, 238 host tests pass. On the RP6, `am start -a android.intent.action.VIEW
  -d "file:///…/psx/Metal Gear Solid/Metal Gear Solid.m3u" --es system psx` logged `Launch: … -> Ready(system=
  PlayStation, rom=RomFile(name=Metal Gear Solid.m3u, uri=…[Disc 1].chd, …)`, loaded Disc 1 in place and reached the
  game's opening at 60 FPS. Before, it logged `MIA: Failed to load medium` and stayed on the loading screen until
  force-stopped. The bare playlist through the adb load intent, which doesn't expand playlists, makes the core refuse
  the load: the "Game Didn't Start" dialog came up, and OK went back to the Library. A file of random bytes named as a
  PlayStation disc isn't such a case: the core takes it, and the BIOS starts with no game.
- **Not checked:** a playlist from a frontend that hands Phobos a content URI with no path (its discs can't be found
  then, so it gets the "can't read" message), the "Android didn't open" reason, and returning to a real frontend after
  the dialog (the test launches came from adb).

## Neo Geo Z80 banks for M ROMs over 64 KiB — 2026-10-03

Branch `cursor/neo-geo-z80-banks-2b67`, on master. The plan row "Neo Geo Z80 banks for M ROMs over 64 KiB" and the
Known core status in `neo-geo-compatibility.md` have the details.

- **How it was found:** a boot pass of the RP6's 25 Neo Geo sets (its `neogeoaes` folder, MVS BIOS), each
  cold-started through the adb load intent, with a screenshot and the log taken after 35 s and Phobos force-stopped
  in between, so no auto state or save was written. 24 reached their title at 59.2 FPS; Blue's Journey showed the
  BIOS's Z80 ERROR, though every ROM in its zip matches the database's CRC.
- **Checks run:** the modern release builds, 238 host tests pass. On the RP6, Blue's Journey reaches its title at
  59.2 FPS, and the same 25-set pass on this build had all 25 at their titles at 59.2 FPS, with no crashes and the
  audio ring 46–59% full. A throwaway build, never committed, that also counted banked reads where the old
  mapping returns different bytes ran the same pass: Blue's Journey's Z80 got such bytes for nearly all its banked
  reads (24,376,799 of 24,379,392), and Alpha Mission II, Aggressors of Dark Kombat, the King of Fighters '94 and
  2002 and Puzzle Bobble each got some, in the $8000 or $C000 window at banks in the first 64 KiB, so part of their
  sound data was wrong before. The other 19 read none in their 35 s. All 25 again reached their titles at 59.2 FPS;
  one kof95 sample in the first pass read the audio ring empty just as `screencap` started, and this pass had it
  at 54%.
- **Not checked:** sound by ear (games whose drivers read banks in the first 64 KiB may sound different now), the
  AES BIOS, and the legacy APK on a device.

## Touch controls out of the edge gestures — 2026-10-03

Branch `cursor/touch-gesture-exclusion-2b67`, stacked on `cursor/msx-tape-bios-2b67`. The plan row "Touch controls
out of the edge gestures" has the details.

- **Checks run:** the modern release builds, 238 host tests pass (new: `touchAreasCoverEveryPointThatTakesAFinger`,
  `touchAreasFollowEachControlsReach`). On the RP6 (gesture navigation, 1920×1080 at 369 dpi), touch controls were
  switched on in the pause menu for the test and off again afterwards. With the installed build (`e0983c7c9`),
  `input touchscreen swipe 55 836 400 836 150`, from the left edge across Alpha Mission II's D-pad, brought in the
  status bar and the navigation handle (SystemUI's `EdgeBackGestureHandler` dump: `mIsNavBarShownTransiently=true`;
  the back gesture itself is disabled while the bars are hidden). With this build the dump's `mExcludeRegion` held
  the D-pad (49–429 × 651–1031 px), the face buttons, SELECT/START and the menu button, and the same swipe left the
  bars hidden. The same swipe at y = 300, away from the controls, still brought them in, and paused only the RP6's
  own strip on the right edge (1891–1920 × 402–678) remained. With Metal Gear's MSX keyboard open its area
  (0–1920 × 539–1080) was excluded, and not while paused. Phobos was force-stopped after each run, so no auto state
  was written.
- **Not checked:** the ZX Spectrum keyboard (the same modifier as the MSX one), portrait, 3-button navigation, and
  the legacy APK on a device.

## MSX tapes ask for a BASIC BIOS — 2026-10-03

Branch `cursor/msx-tape-bios-2b67`, stacked on `cursor/headered-system-cards-2b67`. The plan row "MSX tapes ask for a
BASIC BIOS" has the details.

- **Checks run:** both flavors build, 236 host tests pass (new `MsxTapesTest`: tapes need `fw_msx`, or both MSX2
  ROMs; cartridges and other systems need nothing). On the RP6 the stand-in tape loaded straight away with the
  user's BIOS set (no dialog), and was deleted afterwards.
- **Not checked:** the BIOS Required dialog on the RP6, which would mean clearing the user's MSX firmware picks.

## Headered PC Engine card dumps — 2026-10-03

Branch `cursor/headered-system-cards-2b67`, stacked on `cursor/msx-tape-speed-2b67`. The plan row "Headered PC
Engine card dumps" has the details.

- **Checks run:** both flavors build, 233 host tests pass (new: `aPcEngineCardDumpIsKnownWithoutItsCopierHeader`,
  `theUnheaderedHashIsWhatFollowsACopiersHeader`). On the RP6, `tail -c +513 syscard1.pce | sha256sum` gave
  ares's System Card 1.0 hash (the file never left the device), and the Firmware screen showed that slot Verified.
- **Not checked:** a headered System Card 3.0 or Games Express card booting a game (the RP6's are unheadered).

## MSX tape loading speed — 2026-10-03

Branch `cursor/msx-tape-speed-2b67`, stacked on `cursor/msx-tapes-2b67`. The plan row "MSX tape loading speed" has
the details.

- **Checks run:** both flavors build, 231 host tests pass. On the RP6, with the stand-in tape again (deleted
  afterwards with its states and folders): set to 8x in the pause menu, `CLOAD` ran at 433 to 466 FPS and loaded
  the tape in about 5 s (32 s at 1x), and the game went back to 59.9 FPS when BASIC stopped the motor. The
  setting was put back to Accurate (1x) afterwards.
- **Not checked:** a commercial tape's custom loader at 8x, the legacy APK on a device.

## MSX tapes — 2026-10-03

Branch `cursor/msx-tapes-2b67`, stacked on `cursor/pocket-challenge-v2-2b67`. The plan row "MSX tapes" has the
details; this records what was checked and how.

- **Checks run:** both flavors build (`./gradlew testModernDebugUnitTest assembleModernRelease
  assembleLegacyRelease`), 231 host tests pass. On the RP6 (modern release, data kept), with a stand-in tape
  written for the test (`PHOBOS.cas`: a file header of ten 0xD3 bytes and the name PHOBOS, then the tokenized
  program `10 PRINT "PHOBOS TAPE OK"` / `20 GOTO 10` ending in ten zero bytes; deleted afterwards with the save
  folder, states and empty folders it left) launched as a frontend would (`system=msx`): the user's MSX BIOS
  booted to MSX BASIC 1.0; `CLOAD` typed on the on-screen keyboard ran the tape (stripe "Loading", 32 s), BASIC
  printed "Found:PHOBOS" and Ok and stopped the motor (stripe "Stopped 99%"); `RUN` printed the line over and
  over; the stripe's rewind took the tape to the start; a state saved with the tape in (82,760 bytes) loaded back.
  Metal Gear (MSX2) boots to its title under the user's MSX2 BIOS pair.
- **Found on the way:** with a real BIOS the first try crashed (a null read in `Board::Konami::read`): the tape's
  pak had been connected to the cartridge slot too. Tape games now leave the slots empty.
- **Not checked:** a commercial tape, a `.wav` or `.tzx` tape, an MSX2 tape, the legacy APK on a device.
- **On the RP6:** typing goes through the on-screen keyboard (no hardware keyboard mapping); the pause menu's MSX
  section turns it on.

## Pocket Challenge V2 — 2026-10-03

Branch `cursor/pocket-challenge-v2-2b67`, from master. The plan row "Pocket Challenge V2" has the details;
this records what was checked.

- **Checks run:** both flavors build (`./gradlew testModernDebugUnitTest assembleModernRelease
  assembleLegacyRelease`), 231 host tests pass (new: `pocketChallengeV2GamesAndNames`; the glyph and touch
  layout tests cover the new icon and family). On the RP6 (modern release, data kept), a stand-in cartridge
  (a 64 KiB ROM whose reset vector jumps to an endless loop, with a valid footer and checksum; deleted
  afterwards with the save folder it left) launched as a frontend would (`system=pcv2`): it loaded in place,
  attached the bundled 4 KiB boot ROM and ran at 75.5 FPS (target 75.47), and the pause menu's Buttons page
  showed A as Circle, B as Clear, Select as Escape and Start as View.
- **Not checked:** a real game (none on the RP6), the touch layout on screen (touch controls are off on the
  RP6; the layout tests check overlap and placement), the legacy APK on a device.
- **On the RP6:** `input swipe` with a long duration hung over the pause menu; `input touchscreen swipe ...
  150` under a `perl -e 'alarm 10; exec @ARGV'` guard scrolls it (macOS has no `timeout`).

## CD backup RAM kept between sessions — 2026-10-02

Branch `cursor/cd-backup-ram-2b67`, stacked on `cursor/laseractive-2b67`. The plan row "CD backup RAM kept
between sessions" has the details; this records what was checked and how.

- **Checks run:** both flavors build (`./gradlew testModernDebugUnitTest assembleModernRelease
  assembleLegacyRelease`), 224 host tests pass (the change is native only). On the RP6 (modern release, data
  kept): with no saved copy, The Terminator flushed mia's formatted 8 KiB on pause. Then backup RAM files with a
  marker (`PHOBOS BRAM TEST` at 0x100; the PC Engine one formatted with the `HUBM` header) were put in the save
  folders of The Terminator, Rondo of Blood and the PC Engine LD stand-in disc. Each game logged the import (and
  for the PC Engine ones "restored backup.ram" before the system loaded); switching to the next game through the
  adb load intent wrote its auto state, which held the marker (so the core had the data), and flushed a file
  identical to the marked one. Sub-Terrania (Mega Drive) and Final Lap Twin (PC Engine HuCard) flushed nothing.
- **Test hygiene:** Auto-Load State is on, and states hold the backup RAM, so the four games' auto states were
  moved aside first and put back afterwards (original timestamps); the app was force-stopped at the end so the
  last game wrote no auto state over a restored one, and the test files, stand-in disc and its saves and states
  were deleted. Pausing (Home) flushes saves too, which avoids tapping the pause menu when the RP6 has rotated.
- **Not checked:** a save made in a game's own menu, a Mega CD 32X game (none on the RP6), the legacy APK on a
  device.
- **Device setting:** `screen_off_timeout` went from 600000 to 1800000 for the test and is restored to 600000.

## LaserActive (Mega LD and PC Engine LD) — 2026-10-02

Branch `cursor/laseractive-2b67`, stacked on `cursor/pce-cd-in-place-loading-2b67` (PR 2 of the LaserActive
plan). The plan row "LaserActive (Mega LD and PC Engine LD)" has the details; this records what was checked
and what wasn't.

- **Checks run:** both flavors build (`./gradlew testModernDebugUnitTest assembleModernRelease
  assembleLegacyRelease`), 224 host tests pass (new: `laserActiveHintsSelectMegaLdOrPceLd`, and
  `GameFileRouteTest` covers `.mmi`). On the RP6 (modern release over PR 1's build, data kept), with two
  stand-in `.mmi` discs made for the test (a `MediaInfo.json` with sides A and B and no streams, no game data;
  deleted afterwards with their saves and states): a frontend-style launch (`system` extra `megald`, then
  `pcengineld`) resolved each, loaded it in place and attached the PAC BIOS (SEGA PAC US, NEC PAC PAC-N10);
  both BIOS menus ran at 60 FPS; Side listed A, B and No disc, put in side B and took the disc out with the
  core running on (the SEGA PAC's prompt changed to "Select The Play Button"); the next game started with side A
  marked again; Mega LD's backup RAM was written to `Saves/Mega Drive/<game>/backup.ram` and imported on the
  next load. Regression loads of The Terminator (Mega CD), Sub-Terrania (Mega Drive), Rondo of Blood (PC Engine
  CD) and Final Lap Twin (PC Engine) logged "Loading in place" with the right firmware at 60 FPS.
- **Not checked:** a real LaserActive game (none on the RP6), so laserdisc video, analog audio, digital tracks
  and seeking aren't verified; `.mmi` games in the Library and their tile (none in the ROM folders); the legacy
  APK on a device.
- **Measuring on the RP6:** frame rates read while its screen is asleep are throttled (19 FPS for Mega LD here,
  60 once awake); check `dumpsys power | grep mWakefulness` first.
- **Device setting:** the RP6 wasn't charging, so `screen_off_timeout` went from 600000 to 1800000 for the
  test and is restored to 600000.

## PC Engine CD, games loaded in place, firmware by content — 2026-10-02

Branch `cursor/pce-cd-in-place-loading-2b67` (PR 1 of the LaserActive plan; LaserActive follows on top).
The plan rows "PC Engine CD boots", "Games load from where they are", "Firmware matched by content" and
"CHD pregaps left out of the image" have the details; this records what was checked and what wasn't.

- **Checks run:** both flavors build (`./gradlew testModernDebugUnitTest assembleModernRelease
  assembleLegacyRelease`, JDK 17, NDK 28.2), host tests pass (new: `GameFileRouteTest`, `FirmwareIdsTest`).
  On the RP6 (modern release over the nightly, data kept): Rondo of Blood boots through System Card 3.0 and
  plays its intro in place; regression loads through the adb load intent of Final Lap Twin (PC Engine),
  The Terminator (Mega CD CHD), Ape Escape (PS1 CHD), Pokémon Unbound (GBA zip), Alpha Mission II (Neo Geo)
  and F-Zero X (N64) all logged "Loading in place" and restored their saves from Phobos's folder.
- **Firmware on the RP6:** the six missing BIOS files (LaserActive SEGA PAC US/JP and NEC PAC PAC-N10, PAC-N1,
  PCE-LP1, from `Abdess/retrobios`, SHA-256 checked against upstream ares's list; `aleck64.zip` from MAME's
  set) went into `/storage/emulated/0/Emulation Settings/Phobos/Firmware/`, and a content scan filled their
  slots and corrected the ones the name matching had got wrong. Nothing was committed to the repository.
- **Not checked:** a frontend launch whose URI has no path (the copy fallback, and the PC Engine CD refusal
  dialog), a SuperGrafx game (none on the RP6), the Rondo of Blood audio against a redump image (the user heard
  about 2 s of offset left with the translated CHD), and the legacy APK on a device.
- **Device setting:** `stay_on_while_plugged_in` was 7 before testing and is restored to 7 afterwards.

## Upstream ares merge — 2026-09-30

Branch `merge/upstream-ares-2026-09`, at the user's request: a merge commit of upstream ares
`4cb8d92b4` (2026-09-22), 77 commits past the fork point `cdabe5c5a` (2026-07-25), so the next sync
starts from it. How each overlap was resolved, for the next merge:

- Desktop front end: Phobos deleted `desktop-ui`, `hiro` and `ruby`; upstream's edits to 24 of their
  files and its new `desktop-ui/emulator/atari-5200.cpp` stay deleted.
- N64 Count/Compare: Phobos's implementation stays (`countSinceSync`, `countWriteSkip`, the modular
  timer distance in `CPU::instruction`), so `cpu.cpp`, `interpreter-scc.cpp` and `serialization.cpp`
  are unchanged from master. Upstream fixed the same two bugs (`430def86f`, `e4217366c`) with
  `stepCount`/`flushCount`/`effectiveCount` and a `countClock` field, but still clamps the JIT's
  timer distance at zero (a sync after every block once Count passes Compare) and adds `countClock`
  to save states; mixing the two would count clocks twice.
- N64 RDRAM size (`8da242f5a`): taken; the JIT checks `rdram.ram.size` instead of 8 MiB. With the
  Expansion Pak on (the default) nothing changes; with it off, code above 4 MiB falls back to the
  interpreter (every `section()` caller handles a missing section).
- `ares/n64/vulkan`: `vulkan.hpp` keeps `#include "rdp_device.hpp"`, since `PhobosRunner.cpp`
  reaches `::Vulkan::Context` through it; upstream moved the include to `vulkan.cpp` for non-unity
  builds, and that copy is dropped.
- `nall` recompiler: `mov128` keeps Phobos's ARM64 path of two 64-bit moves (sljit's SIMD move
  crashes on some Snapdragons); other architectures take upstream's `SLJIT_TMP_DEST_VREG`. The N64
  JIT now declares its two float scratch registers (`beginFunction(3, 3, 6, 2)`) and passes flags
  to `fcmp32`/`fcmp64`, as the newer sljit needs.
- sljit: upstream's newer copy plus Phobos's `sljit.h`. Phobos had inverted the store size in the
  ARM64 integer-to-float conversion to memory (a double was stored as 4 bytes); upstream's is back.
  The N64 JIT always converts into a register, so nothing changes today.
- Clocks: the 64DD and the GBA's S-3511A keep Phobos's host-time seeding. Upstream now seeds too,
  but its GBA seed leaves status `0x82` (halted), which Pokémon's clock driver treats as unset and
  overwrites with 2000-01-01. Taken from upstream: the guard against a clock that went backwards,
  the 64DD's missing timestamp counting as a new save, and GBA saves older than five years catching
  up fully (the cap is gone).
- `mia/medium/mame.cpp`: upstream's rewrite (`AssemblyResult`, parent chains, strict record
  checks), with Phobos's `load32_word_swap` interleave (King of Fighters 2003, Metal Slug 5, SNK vs.
  Capcom) and
  basename matching of archive members added back. Every ROM record in the Neo Geo, Arcade and Vs.
  databases passes the new checks. `neo-geo.cpp` takes upstream's result-based loading and keeps
  Phobos's decryption and load log.
- Build: Phobos's hand-kept source list gains `armv6m.cpp` (the Atari 2600's Harmony carts), the
  `ares/resource/resource.hpp` stub gains `Sprite::Famicom::Crosshair` (the XG-1 light gun), and the
  unity files keep Phobos's `ares.hpp` header. The stub's images are empty, as they have been since
  the first commit, so a light gun would have no crosshair; none can be connected in Phobos
  (`connectDevices` in `PhobosRunner.cpp` always connects the gamepad), so exposing the Zapper,
  Super Scope, Justifier, Light Phaser or XG-1 needs ares's real sprites generated first, as mia's
  firmware is. The APK's own database copies
  (`android/app/src/main/assets/Database`) gain upstream's `Atari 2600.bml` (region, board and
  phosphor by checksum) and the updated `Famicom.bml`; they aren't synced from `mia/Database`
  automatically.
- The Atari 2600 core now reads the console switches once a frame, so a press shorter than a frame
  (such as `adb shell input keyevent`) can be missed; hold it (`--longpress`) in device scripts.

Checked on the RP6, A/B against master's nightly `1.1.0-32-gec1a0b25`: Mario Tennis fast-forward
work per frame 6.36 / 6.23 ms before, 6.40 / 6.35 ms after (within run-to-run spread; fast-forward
was capped at 2x, 118 FPS), and 59.8 FPS at normal speed in both; Dig Dug went from garbage at
236 FPS to correct play; Asteroids, Yars' Revenge, 8 Eyes, Pokémon Unbound, Link's Awakening DX,
King of Fighters 2003, Garou and Alpha Mission II run. Not exposed yet: the Atari 5200 core (not in
the build), Vs. UniSystem games (need `VsSystem.bml` in the assets plus coin and DIP-switch inputs)
and the N64 GameCube controller.

## Neo Geo CD sound — 2026-09-30

Branch `feature/ngcd-audio-2026-09`, at the user's request. Before it, Neo Geo CD games had no
sound: the Z80 had no program (see the NGCD-M3 section below) and the core had no CD audio.

What changed, all in `ares/ng`:
- The Z80 (`apu/`) has 64 KiB of RAM on the CD, which the BIOS loads through the transfer area's
  Z80 zone and the Z80 runs from. It is held in reset from power-on until the BIOS writes a non-zero
  value to REG_Z80RST (`$FF0183`), and released with a Z80 and YM2610 reset (`APU::setReset`,
  `OPNB::reset`). The 68000 reads the zone back byte-wide on the low lane (`cpu/memory.cpp`).
- ADPCM-B reads the 1 MiB PCM RAM, as ADPCM-A does (`System::readVB`).
- `$FF0004` (`System::IO::irqMask2`) holds the VBL (0x030) and raster timer (0x300) interrupt
  enables, and reads back. The BIOS turns VBL off while it loads, because its VBL handler points the
  transfer area at the Z80's RAM; without the mask that corrupted sound programs mid-upload.
  `lspc/lspc.cpp` gates both interrupts on it.
- CD audio is `Disc::stream` ("CD-DA", 44.1 kHz stereo). Every 75 Hz drive tick feeds it a sector
  of audio or of silence, since the frontend mixes streams in lockstep; starts and stops ramp over
  220 samples (`Cdd::playSector`).
- The BIOS's tables of the disc (`Cdd::writeTrackTable`, layout in its comment). The BIOS fills them
  from the drive's table of contents on the disc-detect path (`$C0BFC6`), which the core skips with
  its "disc detected" poke at settle, so every play command it built was FF:FF:FF. The core fills
  them when the BIOS clears a music request it has taken from the sound driver: the byte at the Z80
  address from the BIOS pointer at `$10F6EA` (`$E1FDF0`, the Z80's `$FEF8`, on the discs seen).
  Filling them at the poke instead stalled the boot.
- The drive plays from a track's start on command `0xB`. Audio sectors go to the stream and only
  refresh the decoder's header registers, as in libretro neocd, instead of entering the decoder's
  ring buffer, which the BIOS never empties of audio: after the first music, the next data read
  found the ring full and failed with DISC I/O ERROR 0002.
- `Cdd::serialReset` (`$FF0181` low) no longer sets the drive status to 9. The BIOS pulses that line
  every frame while a track plays (`$C0B514`), and with the drive reporting 9 instead of 1
  (playing), its pause and stop commands (queue handlers `$C0C506`, `$C0C566`) waited for the
  queue's 20 s timeout (`$C0BBD8`, 1,500 exchanges). That froze the continue countdown, and
  one-shot tracks played on into the next track.
- CD save states carry a format number after the header (`CDSerializerFormat`, now 1), so older CD
  states are refused rather than read misaligned; AES and MVS states are unaffected.

BIOS behavior found on the way (CDZ BIOS):
- Music requests: the sound driver posts a mode and track at Z80 `$FEF8`/`$FEF9`. The BIOS copies
  them to `$10F6F6`–`$10F6F8` and `$10F64B` and queues command 7 in its queue at `$10F472` (read and
  write pointers `$10F650`/`$10F651`, dispatch table `$C0BC44`); command 7 (`$C0C5EE`) builds
  `3 M S F` from the three-byte table.
- Track ends: the BIOS doesn't watch the drive's position. It counts frames down from the track's
  length in the two-byte table (`$C0B73C`: length × 60 − 570 at 60 Hz), and when the count runs out
  it pauses a one-shot track (mode `$0202`). The drive plays on into the next track until told
  otherwise, as real drives and libretro neocd do.
- After its disc check (about 28 s after boot on the RP6) the BIOS stays at its CD-player menu,
  "PUSH START BUTTON", and loads the game only on START; the auto-boot gate the settle comment in
  `cdd.cpp` mentions (`$C14C40`) isn't reached. This predates the branch. (Fixed the same day by
  branch `feature/ngcd-cdz-drive-2026-09`; see its note in the active-work section.)
- libretro neocd reads data at 150 sectors a second on the CDZ, a double-speed drive, and 75 on the
  front and top loaders; the core reads everything at 75. Relevant to the faster-loading task. (The
  CDZ reads at 150 since the same branch.)

Checked on the RP6 with Samurai Spirits ~ Samurai Shodown (CHD): the BIOS jingle and menu effects;
the character-select, stage, continue and game-over music; the stage loading after the
character-select music; the continue countdown running out through the save prompt to the title;
three games in a row. Load times are unchanged: 40 s from the BIOS's START to the title, 13.5 s for
a stage. Not yet seen: a looping stage track reaching its loop point, since idle fights ended first
(the stage tracks run 3.7 to 4.5 minutes).

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
  item below. (Fixed 2026-09-30: [Neo Geo CD sound](#neo-geo-cd-sound--2026-09-30).)
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
