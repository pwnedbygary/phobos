# Touch controls overhaul — design, findings and status

Status: **implemented 2026-09-24; compiles for both flavors and the 41 host unit tests
pass; not yet run on a device.** It was written while the user's phone was in use by
another session, so the build came afterwards. See
[handoff](handoff.md#touch-controls-overhaul-and-performance-scan--2026-09-24-in-progress)
for the session state, constraints and next steps.

This document covers implementation-plan tasks 13a (per-core layouts), 15a (pause-menu
quick actions), 16 (resize / reposition) and 31 (PS1 authentic shapes), plus
input-mapping gaps found while mapping every core's controls. References consulted
(read-only): the user's
[mupen64plus-ae-turnip](https://github.com/pwnedbygary/mupen64plus-ae-turnip) touch
controller and [Argosy Launcher](https://github.com/rommapp/argosy-launcher); see
[Reference comparison](#reference-comparison). No code was copied from either (both are
GPL-3.0 and Phobos is ISC-licensed); every behavior borrowed from them was re-implemented
from a written description.

## Findings in the previous implementation

Observed by reading `ui/TouchControls.kt`, `input/GameInputState.kt`,
`ui/EmulatorScreen.kt` and `PhobosRunner.cpp` at base `6acee27cb`:

| # | Finding | Evidence | Disposition |
|---|---|---|---|
| F1 | **PlayStation face buttons were mislabeled.** The overlay drew `○` on the A bit, `✕` on B, `△` on X and `□` on Y, but natively Cross = A, Circle = B, Square = X, Triangle = Y. Every PS1 face button sent a different button than its label. | old `TouchControls.kt` `FaceCluster`; `PhobosRunner.cpp` `resolveButtonBit()` | Fixed in the new PS1 layout (`TouchLayouts.playStation`). |
| F2 | D-pad was 4-way only (no diagonals); a finger's direction was re-evaluated only inside the D-pad box. | old `DpadCross` | 8-way with configurable diagonal zones and hysteresis (`TouchEngine.dpadDirection`). |
| F3 | Buttons were hit-tested only on finger down; sliding a thumb from one button to another did nothing. | old `ClusterDispatcher` | Sliding between any buttons, optional (`TouchPrefs.slideBetweenButtons`). |
| F4 | One generic layout for every system: two analog sticks even on digital systems, and no N64 C buttons or Z, so several N64 buttons were unreachable by touch. | old `TouchControls` | Per-family layouts (`TouchLayouts`). |
| F5 | Fixed positions/sizes; no customization (plan task 16). | old `TouchControls` | Layout editor with per-family, per-orientation overrides (`TouchLayoutEditor`). |
| F6 | `Log.i` with `String.format` on every touch mask change, plus a periodic push log. | old `GameInputState.updateVirtualButtons` / `push` | Removed. |
| F7 | `GameInputState.push()` coalesced pushes to 5 ms and **dropped** a push inside the window. The final "stick back to center" event could be dropped, leaving a stale deflection until the next event (drift). | old `GameInputState.push` | Every change is pushed now. Native `setInput` is atomic stores, so the cost is negligible. |
| F8 | The hat-D-pad handler re-ran hotkey matching on **every** joystick motion event, so a held hat combo (e.g. Z + D-pad Right = next slot) could fire repeatedly while a stick moved. | old `GameInputState.updateHotkeyDpad` | Hotkeys re-evaluate only when the hat state changes. |
| F9 | Axis bindings were re-parsed with `String.split` for every mapping on every motion event, and the key-to-bits lookup loop (`"k:$keyCode"` string compares) was duplicated in `EmulatorScreen` and `MainActivity`. | old `GameInputState.handleMotionEvent`, `EmulatorScreen`, `MainActivity` | New `input/InputBindings.kt` parses once per mappings change; `GameInputState`, `EmulatorScreen` and `MainActivity` use it. |
| F10 | **Unreachable console inputs.** `resolveButtonBit()` has no case for Atari 2600 `Reset` (Game Reset, needed to start most 2600 games), `Left Difficulty`, `Right Difficulty`, `TV Type`; Master System `Pause`; Neo Geo Pocket `Option`. They are polled every frame but always read "not pressed". | `ares/a26/system/controls.cpp`, `ares/a26/riot/io.cpp`, `ares/ms/system/controls.cpp`, `ares/ngp/system/controls.cpp` | Fixed natively: 2600 Reset→Start, Left/Right Difficulty→L1/R1, TV Type→L2 (ares flips the three switches on each rising edge, so momentary bits are correct); SMS Pause→Start; NGP Option→Start. Physical controllers get these too (Start/L1/R1/L2 bindings). |
| F11 | **ColecoVision keypad keys 3–9, 0, `*`, `#` and all MSX keyboard keys are unreachable** from any input, because `setKeyboardKey()` only accepts ZX Spectrum. Many ColecoVision games need the keypad to start. | `PhobosRunner.cpp` `setKeyboardKey`, `input()` | Fixed natively: the keyboard path accepts ColecoVision and MSX (held keys OR'd with any existing bit mapping; ZX keeps its keyboard-only semantics). Touch layouts add a ColecoVision keypad and MSX SPACE/RETURN (+ F1–F5/ESC in the editor). |
| F12 | WonderSwan: the WS-specific `A`/`B` branches in `resolveButtonBit()` are dead code (the generic `A`/`B` branch matches first), so A/B are always the A/B bits. In vertical mode `X4` and `X3` also resolve to the A and B bits, so they collide with A/B. | `PhobosRunner.cpp` `resolveButtonBit()` | Documented. The vertical touch layout shows no separate A/B buttons, but its X4/X3 still press the core's A/B natively because they share those bits. Native cleanup deferred (behavior change for hardware users). |
| F13 | "Core Provided" aspect ratio was a fixed 4:3 for every system, so handhelds were stretched (GBA 3:2, GB/GBC and Game Gear 10:9, WonderSwan 14:9, NGP 20:19); "Integer Scaled" assumed 320×240 and scaled in dp, not pixels. | old `EmulatorScreen` picture sizing (`ratio = 4f / 3f`) | Fixed: `PhobosRunner.cpp` `recordVideoGeometry()` computes the logical display size per frame the way ares desktop does (viewport × `scaleX/Y`, × `aspectX/aspectY`, undoing core rotation and applying the WonderSwan frontend rotation); JNI `getVideoGeometry()`; `MainViewModel.videoGeometry` (polled with the stats loop); `GamePicture` uses it for Core Provided and integer-scales in physical pixels. Falls back to 4:3 / 320×240 until the first frame. |
| F14 | The default `analog_toggle` hotkey (C button alone) matched, was consumed as a hotkey, and had **no handler**: the C button did nothing on any system and the PS1 analog toggle hotkey never worked. | old `EmulatorScreen` hotkey `when` | Fixed: PS1 toggles DualShock analog mode; on other systems the key is not consumed and reaches game mapping. |
| F15 | `MainActivity.setEmulatorKeyHandled()` and its flag were never called/set (dead code). | grep: no callers | Removed; no behavior change. |
| F16 | The on-screen key set was never cleared, and `setKeyboardKey()` dropped releases once `root` was gone, so a key held during unload could stay "pressed" into the next keyboard-system game. | `PhobosRunner.cpp` `setKeyboardKey`, `unloadSystem` | Fixed: releases always apply; `unloadSystem()` clears the set. |
| F17 | **N64 screenshots used the wrong buffer.** N64 Vulkan frames are presented straight from the parallel-RDP scanout and never pass through `lastFrameBuffer`, so the Screenshot action saved a stale or empty image (or failed) on N64. | `PhobosRunner.cpp` `takeScreenshot`, `video()` | Fixed: N64 Vulkan reads the retained scanout with `Vulkan::readScanout()` (takes `vulkan.mutex` directly, never the screen thread's `scanoutLock`; bounded 100 ms fence wait) and converts RGBA to the ARGB the PNG encoder expects. Save-state previews (plan task 50) use the same path. |
| F18 | In portrait the game picture was centered in the whole screen, so the lower controls covered part of it while the space above sat empty. | old `EmulatorScreen` `GamePicture` | Fixed: in portrait the picture is top-aligned (below the status bar/cutout in full-screen mode) and the controls use the space below it. |
| F19 | A quick tap could be lost: a press and release that both land between two core input polls never reach the core. | `GameInputState` → `PhobosCore.setInput` (level-only state) | Fixed: native per-bit press counters (`bitPressCount`, `pressGeneration`); each node reports a press it has not yet seen even if the button is already released, if the press is under 100 ms old. Presses are not counted while paused, so menu navigation or presses during a loading screen never replay as phantom input. On-screen keyboard keys (set membership, no counter) are held at least 50 ms by `TouchInputSink`. |
| F20 | **The Neo Geo button map never ran.** nall's `string::contains()` matches *any single character* of its argument (like `strpbrk`), so `!systemName.contains("Pocket")` was false for "Neo Geo AES/MVS/CD" and the documented 2026-09-01 default (X→A, Y→B, A→C, B→D, shoulder combos) was dead code; Neo Geo used the generic map (A, B, C→R1, D→R2). | `PhobosRunner.cpp` `resolveButtonBit` | Fixed with `beginsWith("Neo Geo Pocket")`. **User-visible for physical controllers:** they now get the documented layout. |
| F21 | The same `contains()` misuse applied WonderSwan's vertical-mode rotation (a global, persisted setting) to every system's video. | `PhobosRunner.cpp` `video()` | Fixed with `beginsWith("WonderSwan")`. The third misuse (`contains("Neo Geo")` when naming the ROM temp file) is left as is: every system has been running with ROM-named temp files, and MIA may rely on the name. |
| F22 | Every joystick motion event rewrote all four D-pad bits from the hat, releasing a D-pad that the controller reports as keys whenever a stick moved. | `GameInputState.updateHatDpad` | Fixed: the hat changes only the bits it set, and only when the hat changes. |
| F23 | ZX CUSTOM rebinding never captured a button (the emulator screen consumed keys before the keyboard overlay's listener), and the ZX scheme shown started at KEMP each session while the core ran the saved scheme; the keyboard's cycler did not save. | `EmulatorScreen`, `ZXKeyboard` | Fixed: the emulator screen captures the rebind itself; the scheme is read from settings and every change persists. |
| F24 | While paused, mapped keys were consumed, so a controller could not drive the pause menu; cancelling the quit dialog from the pause menu resumed the game. The pre-build review also found hotkeys live under the new layout editor. | `EmulatorScreen` | Fixed: while paused, mapped keys and repeats go to focus navigation (`BUTTON_A` falls back to `DPAD_CENTER`), and resuming takes focus back for game input; the editor gets every key; cancel returns to where the dialog was opened. |

## Architecture (new)

All under `android/app/src/main/java/com/phobos/emulator/`:

| File | Responsibility |
|---|---|
| `ui/touch/TouchModel.kt` | Pure data: anchors/placements, elements (D-pad, analog stick, button cluster), buttons (bits and/or keyboard key, shape, glyph, accent, action, toggle), per-element overrides + codec, global `TouchPrefs`, palette. |
| `ui/touch/TouchLayouts.kt` | `TouchFamily` (systems sharing one layout/customization) and the default layout of every family. |
| `ui/touch/TouchPlacement.kt` | Resolves default placements + user overrides to screen positions; avoids display cutouts; clamps on screen. |
| `ui/touch/TouchEngine.kt` | Pure-Kotlin multi-touch state machine: per-pointer capture, sliding, between-buttons double press, 8-way D-pad with hysteresis, fixed/floating sticks, toggles, actions, background tap. Unit-testable (no Android types). |
| `ui/touch/TouchRenderer.kt` | `TouchPainter`: glass material (shadow, gradient body, lit rim, gloss), pressed accent fill + glow, vector glyphs (PlayStation symbols, arrows, menu, fast-forward, keyboard), cached text layouts and D-pad paths. |
| `ui/touch/TouchControlsOverlay.kt` | Compose overlay: one pointer handler for the whole screen, one Canvas redrawn per gesture event without recomposition; forwards diffs to `GameInputState` / `PhobosCore.setKeyboardKey`; rate-limited haptics. |
| `ui/touch/TouchLayoutEditor.kt` | Editor: drag to move, pinch or −/+ to resize, hide/show (hidden controls drawn as ghosts), reset one/all, snap-to-8dp grid, cancel/save; per orientation. |
| `ui/TouchSettingsScreen.kt` | Settings → Inputs → Touch Controls (all `TouchPrefs`, per-family "customize layout", reset all) and the Settings-side editor screen over a stand-in game picture. |
| `input/InputBindings.kt` | Parsed controller mappings (`k:`/`a:` bindings) and `mapKeyCodeToBit` (moved from the old `ui/TouchControls.kt`). |
| `input/GameInputState.kt` | Refactored shared input state (F6–F9), `setTouchStick`, `onPhysicalInput` for auto-hide. |
| `input/Hotkeys.kt` | Hotkey action ids plus shared combo matching; the emulator screen's key path and hat path now use one dispatcher (F14). |

Refactors that came with the integration (behavior preserved unless a finding above says otherwise):

- `ui/EmulatorScreen.kt` (was ~1,050 lines) split into `EmulatorScreen.kt` (screen, input,
  touch/editor wiring, `GamePicture`, `TopBar`, `LoadingOverlay`), `EmulationMenu.kt`
  (pause menu, now with a quick-action row and a Touch Controls section; plan task 15a),
  `EmulatorDialogs.kt` and `ZxTapeProgress.kt`.
- `ui/Components.kt` gained `SettingsDropdownItem` and `SettingsSliderItem` (slider
  persists on release instead of on every drag step), reused by the pause menu and
  the touch settings screen.
- `data/SettingsStore.kt`: touch preferences and layouts, plus an `enumOrDefault`
  helper replacing four `try { valueOf } catch` blocks.
- `MainActivity.kt`: key fallback uses `InputBindings`; dead flag removed (F15).
- `MainScaffold.kt`: routes `settings/touch` and `settings/touch-editor/{family}`; one
  set of full-screen routes that hide the bottom bar.

Native (`android/app/src/main/cpp/PhobosRunner.cpp`), input only — no emulation change:

- `resolveButtonBit()`: Atari 2600 Reset→Start, Left/Right Difficulty→L1/R1, TV Type→L2;
  Master System Pause→Start; Neo Geo Pocket Option→Start (F10).
- On-screen keyboard state renamed from ZX-only (`zxKeysPressed`, `zxKeyboardMutex`) to
  `keyboardKeysPressed`/`keyboardMutex` with an atomic count; `setKeyboardKey()` accepts
  MSX/MSX2/ColecoVision; `input()` ORs a held key with the button's bit mapping for those
  systems (ZX keeps keyboard-only sourcing) (F11); cleared on unload (F16).
- Removed the `setInput` log that fired every 30th call while a stick was deflected.

Removed: `ui/TouchControls.kt` (superseded). `mapKeyCodeToBit` moved to `input`; the ZX
rebind capture now lives in `ui/EmulatorScreen.kt`, which imports it (the listener in
`ui/ZXKeyboard.kt` is gone).

### Input rules (TouchEngine)

- A finger that lands on the D-pad or a stick keeps it until lifted (it may leave the
  element's area); a stick takes only one finger. Fingers on buttons may slide to other
  buttons when sliding is on; sliding off every button releases. A pressed button stays
  pressed 8% past its edge so a resting thumb does not chatter into the neighbouring
  pair, and sliding never newly presses an action button (menu, fast-forward, keyboard).
- A gesture the system takes over (for example a navigation swipe) is cancelled: no
  tap, menu or auto-hold effect.
- Hit priority on finger down: direct button hit → between-buttons corridor (clusters
  with `multiHit`, neighbours only) → D-pad → fixed stick → near-miss button (18% slop,
  and never less than 24 dp from the center on each axis, i.e. a 48 dp target) →
  floating-stick zone (1.8× radius) → background. The nearest button wins where padded
  targets overlap, so small keys (ColecoVision keypad, Neo Geo combos) stay reachable
  without stealing touches from their neighbours.
- D-pad: 18% center dead zone; 8-way diagonal zone width 20°–70° (`dpadDiagonal`
  0..1, 0.5 = eight equal sectors on first touch); 4° hysteresis toward the held
  direction; 4-way mode.
- Sticks: full deflection at 80% of the radius (× sensitivity), optional radial dead
  zone (default 0, because cores apply their own, e.g. N64 7/85 axial dead zone and
  octagonal gate in `ares/n64/controller/gamepad/gamepad.cpp`). Screen-down positive,
  matching the physical-axis path. Fixed or floating origin.
- Toggle buttons latch on press. Actions: fast-forward and keyboard fire on press; menu
  fires on release over the button.
- A quick tap on empty space (≤350 ms, ≤12 dp travel, outside every control's bounds) is
  reported as a background tap (the overlay covers the screen, so it replaces the old
  tap-to-show-top-bar handler). A tap that misses the buttons inside a cluster is not.
- The stick thumb graphic shows the output: it reaches its travel limit exactly at full
  deflection.
- Idle fade starts only after the last finger lifts.
- Editor: a tap only selects; dragging starts past the system touch slop.
- Auto-hold (off by default): releasing a single ordinary button held ≥800 ms latches it
  (double-click haptic); tapping it again unlatches. D-pad, sticks, toggles and action
  buttons never latch. Auto-hold latches clear when the layout changes (rotation,
  settings) or the overlay goes away (pause, editor, hidden for a controller, leaving
  the game); toggle latches survive a relayout.

### Features and defaults (`TouchPrefs`)

| Setting | Default | Notes |
|---|---|---|
| Landscape / portrait opacity | 0.8 / 1.0 | Separate because portrait controls sit below the picture (from Argosy). |
| Control size | 1.0 | Global; per-element scale 0.5–2.0 in the editor. |
| Haptics | Light | Off / Light / Medium / Strong map to predefined TICK / CLICK / HEAVY_CLICK effects, rate-limited to one per 25 ms; also on D-pad direction changes. |
| Slide between buttons | On | Across clusters, not only within one (Argosy stops at the cluster edge). |
| Auto-hold | Off | Long-press latch, from Mupen64Plus-AE's long-press mode. |
| Swap hands | Off | Mirrors element positions left↔right (whole groups move; button order inside a group is kept, as in Argosy). The editor stores unmirrored positions. |
| Idle fade | Off | Dims to 25% after 5 s without a new touch; any touch restores (Mupen's auto-hide, as a fade). |
| D-pad | 8-way, diagonal 0.5 | Angle sectors with 4° hysteresis; 4-way mode available. |
| Sticks | Fixed, dead zone 0, sensitivity 1.0 | Floating mode recenters under the finger (Mupen's "relative joystick"). No extra dead zone by default because cores apply their own. |
| Hide with controller | On | Hides on the first physical controller input, not on connect, so handhelds with built-in pads (Retroid, AYN) keep touch until the pad is actually used. |
| Menu / fast-forward buttons | On / Off | On-screen buttons; the menu fires on release. |

### Layout model and persistence

- Default position = anchor point + (dx, dy) dp; offsets scale with the global control
  size so larger controls move away from the edges together.
- User override per element: normalized center (fraction of overlay width/height),
  element scale (0.5–2.0) and hidden flag. Stored per family and orientation as
  `id|fx|fy|scale|hidden;…` (`TouchLayoutCodec`), DataStore key
  `touch_layout_<familyKey>_<land|port>` (`SettingsStore.setTouchLayout`); global
  preferences use `touch_*` keys (`SettingsStore.updateTouchPrefs`, a read-modify-write
  in one DataStore transaction).
- Default positions stay inside the display-cutout insets; every element, including
  custom positions, is clamped on screen.
- Editors: pause menu → Touch Controls → Edit layout (over the paused game), or
  Settings → Inputs → Touch Controls → a system (over a stand-in picture, in the
  current orientation).

### Bit mapping used by the layouts

Derived from `resolveButtonBit()` (virtual bits in `PhobosCore.Input`):

| Family | Touch button → virtual bit |
|---|---|
| N64 | A→A, B→B, Z→L2, L→L1, R→R1, Start→START, C-Up/Down/Left/Right→RS_UP/DOWN/LEFT/RIGHT, stick→left axes (octagonal gate drawn) |
| PlayStation | Cross→A, Circle→B, Square→X, Triangle→Y, L1/L2/R1/R2, Select, Start; sticks (when DualShock analog mode is on; hidden by default in portrait), L3/R3 (hidden by default) |
| Super Famicom | A, B, X, Y, L→L1, R→R1, Select, Start (SFC colors) |
| Famicom | B, A, Select, Start |
| Game Boy / Color, GBA | B, A, Select, Start (+ L/R on GBA) |
| Mega Drive / Mega CD | A→A, B→B, C→R1, X→X, Y→Y, Z→R2, Start, Mode→SELECT (hidden) — six-button Fighting Pad is what the bridge connects |
| Master System | 1→A, 2→B, Pause→START (needs F10 native fix) |
| SG-1000 | 1→A, 2→B |
| Game Gear | 1→A, 2→B, Start |
| PC Engine / CD / SuperGrafx | II→B, I→A, Select, Run→START |
| Neo Geo / CD | A→X, B→Y, C→A, D→B (the native Neo Geo map is a bitmask per core button, active since the F20 fix); combos AB→R1, CD→R2, BC→L1, ABC→L2, BCD→R3 (hidden) |
| Neo Geo Pocket | A, B, Option→START (needs F10) |
| WonderSwan | horizontal: X pad = D-pad, B, A, Y1..Y4→L1/R1/X/Y, Start; vertical: D-pad (Y pad), X1..X4→X/Y/B/A, Start |
| Atari 2600 | joystick, Fire→A, Reset→START, Select→SELECT; L/R difficulty→L1/R1 and TV→L2 (hidden) (needs F10) |
| ColecoVision | fire L→L1, R→R1 (as the hardware map), keypad keys via keyboard path (needs F11) |
| MSX / MSX2 | joystick, A, B, SPACE / RETURN keys, F1–F5/ESC (hidden) (needs F11) |
| ZX Spectrum | Kempston joystick, Fire→A, ENTER / SPACE keys, keyboard toggle |

## Verification plan

Not yet run (the user asked that nothing be executed while their phone is in use):

1. Host unit tests (run 2026-09-24: 41 tests, 0 failures) in
   `android/app/src/test/java/com/phobos/emulator/ui/touch/`:
   - `TouchEngineTest`: D-pad sectors, hysteresis and 4-way mode; stick math; press,
     slide, corridor double press, D-pad capture, multi-touch, background tap, toggle,
     menu action, stick release, floating stick, `releaseAll`; auto-hold latch, short
     press and default-off; minimum touch target; a small menu button opening across its
     whole target; auto-hold cleared by `releaseAll` and relayout (toggles kept);
     cancelled gestures; sliding never pressing an action button.
   - `TouchLayoutCodecTest`: round trip and malformed input.
   - `TouchLayoutsTest`: unique ids, family mapping, PS1 bits, and no overlapping
     elements and everything on screen at 740×360, 851×393, 915×412, 972×437 and
     1280×800 dp plus the portrait transposes, for every family.
   Run with `./gradlew :app:testModernDebugUnitTest`.
2. `bash scripts/build-replit.sh assembleModernDebug` (or the Gradle wrapper with the
   local SDK/JDK) — requires `git submodule update --init thirdparty/libadrenotools` and
   the NDK the build selects.
3. Device checklist (user): every family in both orientations; multi-touch (D-pad +
   two buttons); slide B→A and one-thumb A+B in the corridor; diagonal D-pad; N64 C
   buttons/Z; PS1 labels vs in-game prompts; stick return-to-center; floating stick;
   quick taps (menu navigation) register; auto-hold latch/unlatch; swap hands; idle
   fade; portrait/landscape opacity; editor move/resize/hide/reset persistence in both
   orientations; haptics levels; hide with a controller; 2600 Game Reset; SMS Pause; NGP
   Option; ColecoVision keypad; MSX SPACE; N64 screenshot; portrait picture above the
   controls. From the pre-build review: Neo Geo with a physical controller (new layout,
   F20); a controller whose D-pad sends keys, holding the D-pad while moving a stick
   (F22); ZX CUSTOM long-press rebind and the scheme shown after reload (F23); pause menu
   driven by a controller, and Cancel in the quit dialog from the menu (F24); a
   navigation swipe starting on a control does not pause or latch; WonderSwan vertical
   mode on, then another system (not rotated, F21).

## Reference comparison

Read-only research, 2026-09-24: Mupen64Plus-AE from the user's local
`mupen64plus-ae-turnip` checkout (`TouchController`, `TouchMap`, `VisibleTouchMap`,
`GameOverlay`, the profile editor and `preferences_touchscreen.xml`), and Argosy Launcher
`main` (2.17.0-beta.3, `app/src/main/kotlin/com/nendo/argosy/libretro/touch/` and
LibretroDroid's `input.cpp`).

| Aspect | Mupen64Plus-AE | Argosy | Phobos (new) |
|---|---|---|---|
| Rendering | Skin PNGs plus color-mask bitmaps; no pressed art except auto-hold overlays | Flat Compose `drawBehind` shapes, pixel-width outlines, no press animation | One Canvas: glass material with press scale, glow and accent fill; vector glyphs; per-console colors |
| Hit testing | Mask pixel color lookup; first mask in load order wins | Per-cluster rectangles padded to 48 dp; list order wins | Circles/pills with 18% slop and a 48 dp minimum target; nearest button wins |
| Sliding | Any button (mask re-test on move) | Within one cluster only | Any button; optional |
| Two buttons with one thumb | Diagonal pseudo-buttons on the D-pad only | No | Corridor between neighbouring face buttons |
| D-pad | Mask diagonals | Square dead zone per axis (large diagonals); 4-way unimplemented | Angle sectors, adjustable diagonal width, 4° hysteresis, 4-way mode |
| Stick | Annulus capture; absolute or relative; linear strength then 0.07 per-axis square dead zone | Fixed; linear; no dead zone | Fixed or floating; sensitivity and optional radial dead zone (0 by default because the ares N64 gamepad applies the 7/85 axial dead zone itself) |
| Layout model | Percent of remaining screen; one landscape profile warps on aspect change; no cutout awareness | Fractions of the safe area plus dp sizes; portrait and landscape defaults | Anchor + dp offsets per orientation; normalized overrides; cutout insets; clamped |
| Editor | Drag; tap for X/Y/scale sliders; no reset | Drag, pinch (buttons only), double-tap disable, snap guides; placeholder rectangles | Real controls; drag with 8 dp snap; pinch or −/+ on any element; hide (ghosted); reset one or all; over the paused game or from Settings |
| Auto-hold | Long press (1 s) or slide-off | None | Long press (800 ms), off by default |
| Auto-hide | Fade after 5 s idle; any gamepad motion hides | Hides when a controller connects | Optional idle fade; hides on the first physical input |
| Quick taps | Not handled | LibretroDroid holds releases until the core polls | Native per-bit press counters; 50 ms minimum for keyboard keys |
| Extras | Tilt sensor, FPS overlay, skins | Swap hands, 180° mirror, colored PS buttons, Genesis 6-button | Swap hands, per-orientation opacity, haptic levels, menu / fast-forward / keyboard buttons, every Phobos system |

Not adopted: Mupen's bitmap skins and tilt input, Argosy's 180° mirror (Android already
re-lays the screen out on a flip, so anchors follow) and the hotkey dispatch of touch
buttons (Phobos touch buttons are bitmasks, not key codes). Worth revisiting: user skins
or themes, named or per-game layout profiles, turbo buttons.

Licensing: both references are GPL-3.0 (LibretroDroid GPL-3.0-or-later). Phobos is
ISC-licensed (ares). Nothing was copied; the new code was written from the reports'
behavioral descriptions only.
