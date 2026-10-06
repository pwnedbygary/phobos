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

Next action: establish matched Phobos/v336 APK, device, driver, settings and scene
identities, then collect baseline traces. No optimization or performance parity
has been demonstrated. Do not resume the cancelled signing or audio-buffer tasks
without new authorization. Preserve saves and do not assume APK update compatibility.

Publication preparation: the plan reconciliation received independent PASS,
including byte-for-byte preservation of the original plan in its archival appendix.
This follow-up corrects cancelled signing-task status and adds this portable entry
point, and restores tracking of the existing `.gitmodules` file for fresh clones.
Documentation and Git-metadata checks/review accompany the commit; no APK/device test
is implied. Verify GitHub's branch tip against local HEAD after publication.

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
