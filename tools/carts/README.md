# tools/carts - the 3D cart and disc generator (PRs 4a and 4b)

Builds every cart and disc shell Phobos presents into one GLB per shape, writes the procedural plastic and
glitter normal maps as PNGs, turns `studio.hdr` into IBL KTX with Filament's `cmgen` and compiles the disc
material with `matc`. The generator writes straight into the app's assets,
`android/app/src/main/assets/carts/`; `--check` regenerates into `out-verify/` and compares every generated file
byte-for-byte against the committed assets. The LaunchBox label index (`labels.json.gz`) is bundled there
separately: it is not generated and `--check` excludes it.

History: PR 4a first committed the outputs under `tools/carts/out/`; PR 4b moved them, byte-for-byte, into the
app's assets.

## Requirements

- Node 26.11.1 (`.nvmrc`; `engines` in `package.json`).
- Dependencies, pinned in `package.json`/`package-lock.json`: `three` 0.186.0, `pngjs` 7.0.0,
  `@xmldom/xmldom` 0.9.8. Install with `npm ci --offline` (the local npm cache holds the three tarballs;
  `npm-evidence.txt` in this folder records their sha256 values, the lockfile integrities and the 22:24 fetch).
- Filament 1.77.3's Linux archive for `cmgen` and `matc`:
  `https://github.com/google/filament/releases/download/v1.77.3/filament-v1.77.3-linux.tgz`,
  sha256 `6d3391197dcbdec318dd8a7f4e8d6fd0766794231d53c756a1565fd92bba31b2`. Point `FILAMENT_BIN` at its
  `filament/bin`.

## Regenerate

```sh
cd tools/carts
npm ci --offline
FILAMENT_BIN=/path/to/filament-v1.77.3/bin node bake-carts.mjs
```

`out/MANIFEST.json` lists every output (name, bytes, sha256) and every input hash (the Cartridge copies,
`studio.hdr`, `disc.mat`, this script, `package.json`, the lockfile, `.nvmrc`, the three loader shims,
`npm-evidence.txt`, and the `cmgen`/`matc` binaries used) plus the Filament archive hash.

## Determinism

```sh
FILAMENT_BIN=/path/to/filament-v1.77.3/bin node bake-carts.mjs --check
```

Regenerates **every** output - the GLBs and PNGs from the Node bake plus the IBL KTX, `sh.txt` and
`materials/disc.filamat` from the archive's `cmgen`/`matc` - into `out-verify/`, and compares all 25 files
byte-for-byte with the committed `out/` (a file missing from either side counts as a difference). It prints
`CHECK OK` and removes the scratch folder on success, leaves the folder and exits 1 on any difference.
`FILAMENT_BIN` is required in both modes: without it the run stops with exit 2 before writing anything, because
the manifest covers cmgen/matc outputs too. The generator uses fixed PRNG seeds for the two normal maps and no
timestamps, and the pinned cmgen/matc produce stable bytes, so two runs produce identical output.

## What it bakes

- 15 cart GLBs: 13 cart shells (`famicom`, `sfc`, `gb`, `gbc`, `gba`, `n64`, `sms`, `md`, `small`, `aes`,
  `hucard`, `a26`, `pcb`) plus the two GBA engraving variants, and 3 discs (`cd`, `psx`, `minidisc`), each
  with named materials per part (`shell`, `dark`, `metal`, `screws`, `pcb`, `glass`, `window`, `label`;
  `disc`, `hub`, `stack`, `label`). The label is its own node and primitive with 0..1 UVs.
- GBA engraving variants `gba-badge.glb` and `gba-lines.glb`, verified at the geometry level: each must add
  vertices over `gba.glb` and carry its own BIN chunk (the bake fails loudly otherwise). The check is blind to
  the root node name, which is how the review's blocking finding slipped through the first snapshot: Cartridge's
  `buildEngravingGeometry` has an empty catch, and without `querySelectorAll` in xmldom the SVG parse threw and
  the engraving silently vanished. `dom-shim.mjs` now provides the two tag-name selectors three's SVGLoader
  needs, and the verbatim `pipeline.js` copy stays verbatim (the assertion makes its empty catch harmless).
- `textures/plastic-normal.png` (128x128) and `textures/glitter-normal.png` (256x256), ported byte-for-byte
  from Cartridge's `getPlasticNormalMap`/`getGlitterNormalMap`.
- `ibl/ibl_ibl.ktx`, `ibl/ibl_skybox.ktx`, `ibl/sh.txt` from `studio.hdr` at `--size=256`, and
  `materials/disc.filamat` from `disc.mat` (per-pixel iridescence; Filament 1.77.3).

## Provisional shells and nearest shapes

- **Famicom and Super Famicom are provisional.** The specs use approximate public shell dimensions (HVC
  about 105.5 x 72 x 17 mm; SHVC about 126 x 77 x 22 mm) and adapt Cartridge's `nes`/`snes` outlines to those
  rectangles. They are **Hypothesis until measured**; no authoritative measured source was captured for this
  PR. Their labels reuse Cartridge's `nes`/`snes` regions, and JP label crops stay out until checked against
  real scans (a 4b limit, stated there).
- **PSP uses `minidisc`.** Cartridge has no UMD disc spec (its disc keys are `cd`, `dvd`, `bluray`, `psx`,
  `gdrom`, `minidisc`); the UMD is nearest to the small caddied MiniDisc. Unmapped disc systems fall back to
  `cd` at run time (4b).
- Sculpted from Cartridge's own builders: Cartridge's 17 cart and 6 disc specs and its material names come
  from the MIT-licensed `pipeline.js` copy under `cartridge/` (verbatim; see `PROVENANCE.md`).

## Limits

- The two normal maps are pure functions of fixed seeds (matching Cartridge); no texture compression.
- `@xmldom/xmldom` 0.9.8 carries an npm advisory ("critical issues; update to the latest version"). It stays at
  the scope's pin: this is a local build tool, never shipped, and it parses only the repo's own trusted SVG
  engraving paths. The upgrade path is one registry fetch to 0.9.12, recorded for a later change, not done here.
- Node 26 prints a deprecation warning for `module.register()` (used to load the `?url` and `react/jsx-runtime`
  shims); behavior is unaffected. Moving to `module.registerHooks()` is a later cleanup.
- `dom-shim.mjs` patches only the parsed document's prototypes, in this process: comma-separated tag-name
  selectors, nothing else; an unsupported selector throws.

## Notes for 4b

- `discs/cd.glb` and `discs/psx.glb` are identical except the root node name (same geometry, same materials);
  the disc `under` colour differs at run time and is applied by the renderer, not baked per shape.
- `out/` is about 10 MB on disk (the two IBL KTX files are 3,668,404 B = 3.67 MB; the 18 GLBs total
  6,225,340 B = 6.23 MB); PR 4b needs an APK-size decision before
  moving these into `assets/` - whether the IBL ships at `--size=256`, whether one shared disc mesh is used, and
  whether quantisation is worth it.

## The Spectrum tape shell (authored, not generated)

`android/app/src/main/assets/carts/carts/tape.glb` is Phobos's own model, not a `bake-carts.mjs` output: Cartridge's
pipeline has cart and disc geometry only, and the ZX Spectrum's games are tapes. It is a standard 109 x 70 x 17 mm
compact-cassette case with a printed inlay card, width on X, height on Z, thickness on Y and the printed face toward
-Y, matching the baked carts' convention; its materials are `shell` (takes the per-game colour and finish), `dark`
(the tray and seam) and `label` (the printed card, UV 0..1). `tools/carts/tape/make_tape.py` rebuilds it with
Blender 5.2 (bpy/bmesh only):

```sh
blender --background --python tools/carts/tape/make_tape.py -- \
    --output android/app/src/main/assets/carts/carts/tape.glb
```

The same Blender build produces byte-identical output (sha256 `3cf8bd96c515928750038207e2a843554b740f1bed7de40b73a6bef73c82d59d`);
`--preview out.png` renders a front three-quarter check with a test inlay. `bake-carts.mjs --check` regenerates and
compares the whole `assets/carts` tree, so it reports this authored file as missing from its regeneration; no CI
runs it.

## The system dioramas: SNES + CRT TV (authored, not generated by `bake-carts.mjs`)

`android/app/src/main/assets/carts/consoles/snes.glb` is Phobos's own model and GPL-3.0-or-later: a 3DS
Virtual-Console-style scene of the real SNES (SNS-001) beside a 14-inch 4:3 CRT TV whose screen is a live
artwork target, with the SNES controller and its cable in front. It is not a `bake-carts.mjs` output (Cartridge
has cart and disc geometry only) and not a cart shell: it is a whole diorama the app can display and rotate in
place.

Units are millimetres, width on X, up on Z and the console/TV facing -Y, matching the carts' convention. The
580.6 x 688.0 x 352.1 mm bounding box is centred on X=Y=0 with its base on Z=0, so a turntable about +Z spins the
whole scene in place; `export_yup=True` turns that into the glTF convention (up +Y, the TV screen toward +Z,
footprint centred on X=Z=0, base on Y=0). One root node `snes_diorama` carries 160 named parts: the Blender mesh
has 59,365 vertices and 111,920 triangles, and the GLB is 2,659,776 B with 82,935 POSITION entries after the
exporter's normal splits.

Materials (seven): `shell` (the colour-tintable plastic: console and controller shells and the controller's
shoulder buttons), `dark`, `accent` (SNES purple: POWER/RESET and the convex A/B buttons), `metal`, `glass`
(the two LED lenses), `screen` (the CRT face) and `accent_light` (the lavender X/Y buttons). `shell`, `dark`,
`metal` and `glass` reuse Cartridge's material names; `accent`, `screen` and `accent_light` are new. All seven are
single-sided (backface culling): every part is a closed solid except the screen sheet. The TV's 290 x 217 mm CRT
face is a 64 x 48 grid with a gentle 6.5 mm bulge and clean 0..1 UVs, and it is the only mesh the GLB carries
TEXCOORD_0 for (the converted text meshes have their font UV layer stripped). In the GLB `u` increases with +X and
`v` is 0 at the top of the screen (the exporter flips Blender's bottom-up v), so a conventional top-left-origin
image lands upright. Its `screen` material is plain and slightly emissive: the app stamps the running game onto
it. The preview test frame is generated inside the script and wired into `screen` only after the GLB has been
written, so no artwork is baked into the asset. The lettering is Blender's bundled Bfont, converted to meshes:
no font files ship.

`tools/carts/console/make_console.py` rebuilds it with Blender 5.2.3 (bpy/bmesh only, no external assets):

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system snes --output android/app/src/main/assets/carts/consoles/snes.glb
```

Add `--previews tools/carts/console/previews` to also render the three Cycles checks (front, three-quarter and
120 degrees round); the three PNGs tracked there are illustrative renders, not assets, and re-running the command
regenerates them in place (use a scratch directory to keep the tracked files untouched). The same Blender 5.2.3
build produces byte-identical output (sha256
`63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c`, 2,659,776 B); two runs and the committed
file compare equal. `bake-carts.mjs --check` regenerates and compares the whole `assets/carts` tree, so it reports
this authored file (and the pre-existing `carts/tape.glb`) as missing from its regeneration; no CI runs it, and
this asset is verified by its own byte-identical rebuild instead.

Limits: the generator now also takes `--system nes` (the `SYSTEMS` table; `snes` is still the default). The
model is a recognisable stylised approximation - the SNES footprint is the SNS-001's approximate 200 x 190 mm at
74 mm high including feet, the TV is a generic 14-inch set, and
nothing here is measured against a source. There are no baked textures. Parts stay separate named meshes
(160 nodes) so the screen remains a clean target and `shell` remains tintable; merging by material would be a
consumer-side optimisation. Byte-for-byte determinism is proven only on Blender 5.2.3 built 2026-10-22 (the build
used here).

## The system dioramas: NES + CRT TV (authored, not generated by `bake-carts.mjs`)

`android/app/src/main/assets/carts/consoles/nes.glb` is Phobos's own model and GPL-3.0-or-later: a 3DS
Virtual-Console-style scene of the NES-001 front loader beside the same 14-inch 4:3 CRT TV as the SNES diorama,
with the NES-004 controller and its cable in front. Like `snes.glb` it is not a `bake-carts.mjs` output and not a
cart shell. The console and controller are per-system; the CRT cabinet, screen target, material contract, studio
lighting and preview rig are shared, so the family matches. The modelling inputs, feature checklist and the
measured-vs-stylised notes are in `tools/carts/console/reference/nes.md` (text only; the reference photos are not
committed).

Units are millimetres: width on X, depth on Y, up on Z, with the console and TV facing -Y and the base on Z=0.
The 641.6 x 659.3 x 352.0 mm diorama bounding box is centred on X=Y=0, so a turntable about +Z spins the whole
scene in place; `export_yup=True` turns that into the glTF convention (up +Y, the TV screen toward +Z, footprint
centred on X=Z=0, base on Y=0). The console itself is 256 x 203 x 76 mm including its feet. One root node
`nes_diorama` carries 204 named nodes / 203 meshes: the Blender mesh has 100,897 vertices and 191,772 triangles,
and the GLB is 4,414,624 B with 136,479 POSITION entries after the exporter's normal splits.

The NES-001 follows the reference photos: a warm ivory upper housing over a warm taupe-grey lower chassis (sRGB
about 0.53/0.50/0.48, a band-to-shell luminance step of 0.63 measured off the reference's coplanar front faces,
against the reference's 0.46 - hue and value, not a black skirt), with a crisp seam at 27 mm, about 30% of the
height. The whole upper front is the flip cartridge lid: a thin seam runs from the left body edge to the black port
tower at Z=44.5, with a top hinge line and side seams on the top surface, a lower lip and thumb notch, and the red
two-line "Nintendo / ENTERTAINMENT SYSTEM" printed straight onto the lid face (with the registered mark and a
letter-spaced smaller second line). The top-right vent is 14 cream slats with flat black end caps. The front-right
tower is a 43 mm flat black bezel flush with the front, carrying two tall oval 7-pin controller sockets with thin
rims and red "1"/"2" numerals. The front-left lower panel is a recessed outline on the taupe chassis, with the
square red LED to the left of two flat mid-grey POWER/RESET buttons and their red legends. The left side has the
AUDIO/VIDEO RCA recess (red and warm-metal jacks) and both sides have the rear oval vent; the rear carries the
moulded label recess, RF channel switch, antenna jack and power connector, and four recessed rubber feet sit under
the corners. The controller is 120 x 56 x 20 mm: cream body, dark-grey cross D-pad, dark-grey Start/Select pills,
dark-red A/B and a black cable from the centre top into port 2.

Materials (seven, authored NES colours - not the carts' tintable look): `shell` is the warm ivory upper housing,
lid and controller body; `dark` is the black trim (port tower, lid seams, D-pad, Start/Select, cable and CRT
cabinet); `accent` is Nintendo red (wordmark, POWER/RESET legends, A/B, port numerals); `accent_light` is the warm
taupe lower chassis and button faces; `metal` covers the pins, badge, knobs and the AUDIO/VIDEO jacks (the VIDEO
jack is warm-metal rather than a separate yellow material, keeping the seven-material contract); `glass` is the
red LED lens; and `screen` is the CRT face. All seven are single-sided (backface culling): every part is a closed
solid except the screen sheet. The TV's 290 x 217 mm CRT face is the same 64 x 48 grid with a 6.5 mm bulge and
clean 0..1 UVs; it is the only mesh the GLB carries TEXCOORD_0 for (the converted text meshes have their font UV
layer stripped). In the GLB `u` increases with +X and `v` is 0 at the top of the screen, so a conventional
top-left-origin image lands upright. The preview test frame is generated inside the script and wired into `screen`
only after the GLB has been written, so no artwork or textures are baked into the asset. The lettering is
Blender's bundled Bfont converted to meshes: no font files ship.

`tools/carts/console/make_console.py` rebuilds it with Blender 5.2.3 (bpy/bmesh only, no external assets):

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system nes --output android/app/src/main/assets/carts/consoles/nes.glb
```

Add `--previews tools/carts/console/previews` to render the three Cycles checks as `nes-front.png`,
`nes-three-quarter.png` and `nes-angle-120.png` (880 x 660, 40 samples); they are illustrative renders, not
assets, and re-running the command regenerates them in place. The committed `nes-*` previews are documentation
only and currently show the generated facsimile frame, as the owner's real NES capture is not in hand; when it
lands, the source capture file stays outside the repository and no game art is in the shipped asset (`--screen
refs/screens/nes.png` puts a capture on the CRT in a scratch render without changing the GLB). The same
`--system snes` path still rebuilds the committed `snes.glb` byte for byte (sha256
`63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c`, 2,659,776 B), and both systems are
discovered modules under `phobos_console/systems/` (see "The diorama generator's layout" below), so the remaining
systems are one new `systems/<name>.py` each rather than edits to a shared script.

Two no-preview NES rebuilds and the committed file compare equal (sha256
`ef71cfc1bb00ed612cbfb8ac7b375cb9619ed18533ba5322e76846bbb058766c`; determinism is proven only on Blender 5.2.3
built 2026-10-22, the build used here). `bake-carts.mjs --check` regenerates and compares the whole `assets/carts`
tree, so it reports this authored file (like `snes.glb` and `carts/tape.glb`) as missing from its regeneration; no
CI runs it, and this asset is verified by its own byte-identical rebuild instead.

Limits and caveats: the model is a recognisable stylised approximation, not a measured replica. The 76 mm height
is the revision brief's working figure; dimensions.com lists 88.9 mm overall, and the photos are not a calibrated
orthographic source, so no photo-derived height correction was applied. The gallery is ambiguous about which side
carries the RCA outputs (its caption calls the plain side the left one); the generator puts them on the left, per
the brief and to keep them visible in the 120-degree rear-left preview. The wordmark uses Bfont rather than
Nintendo's licensed logo, the POWER/RESET legends are solid red rather than outlined, and the flap is closed (no
tray or cartridge). There are no baked textures. The 203 parts stay separate so the screen remains a clean target
and the parts remain editable; merging by material would be a consumer-side optimisation. The CRT rig is shared
with `snes.glb`, so changing it changes both assets. The 120-degree preview is a rear view: the controller is
mostly hidden behind the console and TV there.

## The diorama generator's layout (and how to add a system)

`tools/carts/console/make_console.py` is the CLI entry point and keeps the same `--system`, `--output`,
`--previews` and `--samples` flags. The modelling code is the `phobos_console` package beside it:

| Path | Contents |
|---|---|
| `tools/carts/console/make_console.py` | CLI, argument parsing, system selection and the build/export/preview sequence |
| `tools/carts/console/phobos_console/common.py` | shared helpers: scene and material construction, the bmesh primitives, recentre/parent, glTF export and the preview rig |
| `tools/carts/console/phobos_console/crt.py` | the shared 14-inch 4:3 CRT TV (`build_tv`, `TV_AT`, `add_crt`) |
| `tools/carts/console/phobos_console/palette.py` | only the colours every stage draws the same (`SCREEN`, `SCREEN_GLOW`); each system owns its own palette |
| `tools/carts/console/phobos_console/systems/__init__.py` | the registry: it discovers the modules beside it, validates them and builds `SYSTEMS`, plus the `STAGES` kinds |
| `tools/carts/console/phobos_console/systems/<name>.py` | one module per system and its whole registration: `ROOT`, `OUTPUT`, `PALETTE`, `PREVIEW_PREFIX`, `PREVIEW_RES`, `STAGE`, `build(mats, stage)`, plus the console, controller and cable |

`systems/__init__.py` imports every non-underscore module in `systems/`, checks that it has the metadata above,
that it has a `build(mats, stage)` callable and that its `STAGE` is a known kind, then builds the `SYSTEMS` row
from it - keyed by the module name, so `--system n64` means `systems/n64.py`. Each row is `root` (the exported root
node), `output` (the committed GLB path), `palette`, `builder`, `preview_prefix`, `preview_res` and `stage`. The
stage kind selects a shared set piece: `diorama` places the CRT TV between the console and the controller, and
`handheld` is `None`, so a handheld system can omit the CRT. The system module's `build(mats, stage)` places the
console, calls `stage(mats)` (if any), then places the controller and cable, and returns its `ROOT` name;
`make_console.py` checks that name against the row.

To add a system, add exactly one thing: `tools/carts/console/phobos_console/systems/<name>.py`, exposing `ROOT`,
`OUTPUT`, `PALETTE`, `PREVIEW_PREFIX`, `PREVIEW_RES`, `STAGE` and `build(mats, stage)`, and using
`common.py`/`crt.py` for the shared geometry. The registry finds it at import time, so there is no table row, no
import line and no palette edit in any shared file: two authors adding two consoles write two different files and
never conflict. The module's own `PALETTE` is the whole colour choice for that console (tuning it never touches
another system); `palette.py` keeps only what the shared stage has to draw identically everywhere. The old
single-file generator's builder names (`build_snes`, `build_nes`, `snes_top`, ...) are still importable from
`make_console.py`, but a new system does not extend that list - its builders live in its own module.

Rebuild both committed assets (the determinism check; run each twice if you are changing the generator):

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system snes --output /tmp/snes.glb
blender --background --python tools/carts/console/make_console.py -- \
    --system nes --output /tmp/nes.glb
sha256sum /tmp/snes.glb /tmp/nes.glb
```

The SNES GLB must be `63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c` (2,659,776 B) and the
NES GLB `ef71cfc1bb00ed612cbfb8ac7b375cb9619ed18533ba5322e76846bbb058766c` (4,414,624 B); both are checked
against the committed files by hash. `--previews` writes the same three renders per system with the row's prefix
and resolution (`front.png`/`three-quarter.png`/`angle-120.png`, `1000 x 765` for the SNES;
`nes-front.png`/`nes-three-quarter.png`/`nes-angle-120.png`, `880 x 660` for the NES) into the directory you pass,
so point it at a scratch directory when you do not intend to replace the tracked previews.

### Showing a real game on the previews' CRT

`--previews` draws a generic facsimile frame on the screen by default. `--screen <png>` puts a real capture there
instead, centre-cropped to the screen's 4:3 (a wider source loses the sides, a taller one loses the top and bottom;
the mapping nodes do the crop, so nothing is stretched):

```sh
blender --background --python tools/carts/console/make_console.py -- --system snes \
    --output /tmp/snes.glb --previews /tmp/previews --screen refs/screens/snes.png
```

This is a preview-render feature only, and it is deliberately outside the asset: the art is wired into the
`screen` material **after** the GLB has been written, so the exported GLB is byte-identical with and without
`--screen` (checked: `63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c` either way) and still carries
no texture, image or baked screen art. Keep the source capture file outside the repository: preview images that
show real gameplay are documentation, not a shipped asset, and a committed source capture would be someone
else's copyright. The SNES/NES committed `tools/carts/console/previews/*.png` continue to use the facsimile
frame; the Mega Drive set shows a real frame as documentation only, as noted in its section.

## The system dioramas: Sega Mega Drive / Genesis Model 1

`android/app/src/main/assets/carts/consoles/megadrive.glb` is Phobos's own model and GPL-3.0-or-later: a
3DS-Virtual-Console-style scene of the black Model 1 Mega Drive beside the shared 14-inch CRT, with its
rear-top cartridge and the three-button controller and cable in front. It is not a `bake-carts.mjs` output and
not a cart shell; the CRT cabinet, screen target, material contract, studio lighting and preview rig are the
shared pieces, so the family matches.

It is one new module, `tools/carts/console/phobos_console/systems/megadrive.py`, and nothing else: the
auto-discovered registry finds it as `--system megadrive`, with its own `PALETTE` (charcoal ABS, black trim,
red A/B/C/POWER, blue START/RESET, satin metal, red LED and the shared `SCREEN`/`SCREEN_GLOW`). There is no
registry row, import line or shared-palette edit.

Units are millimetres: width on X, depth on Y, up on Z, console and TV facing -Y, base on Z=0, footprint centred
on X=Y=0. The 665.0 x 680.5 x 352.2 mm bounding box is centred on X=Y=0; `export_yup=True` gives the glTF
convention (up +Y, screen toward +Z, base on Y=0). The Blender mesh is 100,949 vertices and 184,484 triangles;
the GLB is 5,445,984 bytes with sha256
`663b0b97bc0488e1023720d10fbf9f87b788b306e96b40713efea634ec1f67a6`.

The console is the 280 x 212 x 79 mm Model 1: a charcoal wedge with a low front, high rear and sloping crown; the
large oval cartridge door and raised oval SEGA/16-BIT badge; a rear-top slot with a separate ridged cartridge and
its own label; the front POWER slider, blue RESET, VOLUME and headphone jack, red LED and the recessed front-right
pair of DB9 controller ports; top/side/rear vents; rear AV/RF jacks and channel switch; and four inset feet. The
three-button controller has the rounded black body, recessed face, ringed cross D-pad, blue START, the red A/B/C
diagonal row with TRIGGER lettering, SEGA and "COMPUTER VIDEO GAME CONTROL PAD" type, and the ribbed cable plug into
port 1. The lettering is our own extruded type modelled from the reference photographs, not a copied image or font
file; no photographs, scans, box art or logo images are embedded. `tools/carts/console/reference/megadrive.md`
records the sources, photo-derived proportions and the feature checklist.

Seven materials, all single-sided (backface culling): `shell` (charcoal console/controller plastic), `dark`
(black trim, recesses and cartridge), `accent` (red POWER and A/B/C), `accent_light` (blue RESET/START), `metal`
(satin badge, pins, jacks), `glass` (the LED lens) and `screen` (the CRT face). The plastic roughness is varied by
part rather than one grey default: shell 0.46, dark 0.42, red/blue buttons 0.32/0.34, metal 0.30 at 0.35 metallic,
glass and screen low. The screen is the only mesh with `TEXCOORD_0` (clean 0..1 UVs); it has a plain, slightly
emissive material the app stamps.

Rebuild:

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system megadrive --output android/app/src/main/assets/carts/consoles/megadrive.glb
```

Add `--previews tools/carts/console/previews` for the three `megadrive-front.png`,
`megadrive-three-quarter.png` and `megadrive-angle-120.png` renders (1000 x 765). A real gameplay capture can be
put on the CRT for the preview only:

```sh
blender --background --python tools/carts/console/make_console.py -- --system megadrive \
    --output /tmp/megadrive.glb --previews /tmp/previews --screen refs/screens/megadrive.png
```

`--screen` centre-crops the capture to the screen's 4:3 through the image lookup's mapping nodes, and runs only
after the GLB is written, so the asset is byte-identical with and without it and carries no image/texture. Keep
the source capture file outside the repository. The `megadrive-*` previews in this snapshot are documentation
only: they show a real frame from a Phobos desktop run of *Golden Axe Plus* and therefore contain that frame,
while the source capture file is not committed and no game art is in the shipped assets. The default/facsimile
screen is still available when no `--screen` is passed. (The SNES/NES committed previews continue to use their
facsimile frame.)

The GLB contract is verified: one root `megadrive_diorama`; exactly seven single-sided materials; only
`tv_screen` has `TEXCOORD_0`; no images, textures, cameras, lights or baked screen art. Two no-preview builds from
the same Blender 5.2.3 build are byte-identical; the committed GLB matches both. The reference record is
`tools/carts/console/reference/megadrive.md`.

Limits: the photos were inspected at scratch resolution and the documented 280 x 212 x 79 mm box was cross-checked
against their ratios; no physical console was measured, and the rear layout is based on the visible front/side/top
views because a dedicated rear close-up was not retained. The three-button controller is a simplified, slightly
oversized interpretation of the Sega pad, not a per-part reconstruction. One game state supplies the screen
capture. The model is a recognisable stylised diorama, not a manufacturing CAD replica, and determinism is proven
only on Blender 5.2.3 built 2026-10-22.
