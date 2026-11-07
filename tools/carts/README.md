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
with the NES controller and its cable in front. Like `snes.glb` it is not a `bake-carts.mjs` output and not a
cart shell. The console and controller are per-system; the CRT cabinet, screen target, material contract, studio
lighting and preview rig are shared, so the family matches.

Units are millimetres: width on X, depth on Y, up on Z, with the console and TV facing -Y and the base on Z=0.
The 638.2 x 665.7 x 352.0 mm bounding box is centred on X=Y=0, so a turntable about +Z spins the whole scene in
place; `export_yup=True` turns that into the glTF convention (up +Y, the TV screen toward +Z, footprint centred on
X=Z=0, base on Y=0). One root node `nes_diorama` carries 188 named parts: the Blender mesh has 77,243 vertices and
146,556 triangles, and the GLB is 3,414,076 B with 105,858 POSITION entries after the exporter's normal splits.

The NES-001 is modelled as the low two-tone slab it is: a rounded light-grey upper shell over a darker warm-grey
lower chassis, with the front-loading flap (closed) carrying the red Nintendo wordmark, the black front-right band
with two stacked 7-pin controller ports and their red "1"/"2" numerals, the ribbed top vent, POWER/RESET and the
red power LED in the lower front panel, side AV jacks and channel switch, rear vents and four rubber feet. The
controller has the black inset face plate with the D-pad, two red A/B buttons, Start/Select pills, the two grey
grip slots and the red Nintendo wordmark, and a cable whose tall plug sits in port 2.

Materials (seven, authored NES colours - not the carts' tintable look): `shell` is the warm light-grey upper
shell, front flap, controller body, grille ribs and RESET button; `dark` is the black trim (port band, rims,
flap recess, controller face plate, D-pad, Start/Select, cable and CRT cabinet); `accent` is Nintendo red (flap
wordmark and sub-line, POWER, A/B, port numerals); `accent_light` is the darker warm-grey lower chassis and
button panel (on the SNES the same name is the lavender X/Y buttons); `metal` covers the port pins, knob caps,
badge and jacks; `glass` is the red LED lens (shared with the TV's LED); and `screen` is the CRT face. All seven
are single-sided (backface culling): every part is a closed solid except the screen sheet. The TV's 290 x 217 mm
CRT face is the same 64 x 48 grid with a 6.5 mm bulge and clean 0..1 UVs; it is the only mesh the GLB carries
TEXCOORD_0 for (the converted text meshes have their font UV layer stripped). In the GLB `u` increases with +X and
`v` is 0 at the top of the screen, so a conventional top-left-origin image lands upright. The preview test frame
is generated inside the script and wired into `screen` only after the GLB has been written, so no artwork or
textures are baked into the asset. The lettering is Blender's bundled Bfont converted to meshes: no font files
ship.

`tools/carts/console/make_console.py` rebuilds it with Blender 5.2.3 (bpy/bmesh only, no external assets):

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system nes --output android/app/src/main/assets/carts/consoles/nes.glb
```

Add `--previews tools/carts/console/previews` to render the three Cycles checks as `nes-front.png`,
`nes-three-quarter.png` and `nes-angle-120.png` (880 x 660, 40 samples); they are illustrative renders, not
assets, and re-running the command regenerates them in place. The same `--system snes` path still rebuilds the
committed `snes.glb` byte for byte (sha256
`63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c`, 2,659,776 B), and both systems are entries
in the `SYSTEMS` table in `phobos_console/systems/__init__.py` (palette, console/controller builders, placement,
root name, preview prefix), so the remaining systems should be table entries rather than new scripts.

Two no-preview NES rebuilds and the committed file compare equal (sha256
`3b3bd5e65982aa263d6615488068f8fd2701b53b32e9cc1256b3d9ddf905cee7`; determinism is proven only on Blender 5.2.3
built 2026-10-22, the build used here). `bake-carts.mjs --check` regenerates and compares the whole `assets/carts`
tree, so it reports this authored file (like `snes.glb` and `carts/tape.glb`) as missing from its regeneration; no
CI runs it, and this asset is verified by its own byte-identical rebuild instead.

Limits and caveats: the model is a recognisable stylised approximation, not a measured replica. The NES-001 body
is about 256 x 202 x 64 mm and the TV is generic; nothing here is measured against a source. The front-loading
flap is modelled closed (no tray or cartridge) and the wordmarks are embossed text, not decals. There are no baked
textures. The 188 parts stay separate so the screen remains a clean target and the parts remain editable;
merging by material would be a consumer-side optimisation. The CRT rig is shared with `snes.glb`, so changing it
changes both assets. The 120-degree preview is a rear view: the controller is mostly hidden behind the console and
TV there.

## The diorama generator's layout (and how to add a system)

`tools/carts/console/make_console.py` is the CLI entry point and keeps the same `--system`, `--output`,
`--previews` and `--samples` flags. The modelling code is the `phobos_console` package beside it:

| Path | Contents |
|---|---|
| `tools/carts/console/make_console.py` | CLI, argument parsing, system selection and the build/export/preview sequence |
| `tools/carts/console/phobos_console/common.py` | shared helpers: scene and material construction, the bmesh primitives, recentre/parent, glTF export and the preview rig |
| `tools/carts/console/phobos_console/crt.py` | the shared 14-inch 4:3 CRT TV (`build_tv`, `TV_AT`, `add_crt`) |
| `tools/carts/console/phobos_console/palette.py` | the authored linear-RGB palettes (`SNES_PALETTE`, `NES_PALETTE`) |
| `tools/carts/console/phobos_console/systems/__init__.py` | the `SYSTEMS` table and the `STAGES` kinds |
| `tools/carts/console/phobos_console/systems/<name>.py` | one module per system: its constants, console, controller, cable and `build(mats, stage)` |

The `SYSTEMS` row is the whole registration: `root` (the exported root node), `output` (the committed GLB path),
`palette`, `builder`, `preview_prefix`, `preview_res` and `stage`. The stage kind selects a shared set piece:
`diorama` places the CRT TV between the console and the controller, and `handheld` is `None`, so a handheld system
can later omit the CRT. The system module's `build(mats, stage)` places the console, calls `stage(mats)` (if any),
then places the controller and cable, and returns its `ROOT` name; `make_console.py` checks that name against the
table.

To add a system, add only two things:

1. `tools/carts/console/phobos_console/systems/<name>.py`, exposing `ROOT`, `OUTPUT`, `PALETTE`,
   `PREVIEW_PREFIX`, `PREVIEW_RES`, `STAGE` and `build(mats, stage)`, and using `common.py`/`crt.py` for shared
   geometry. Authored colours live on the module's `PALETTE`; shared colours can be imported from `palette.py`.
2. one row in `SYSTEMS` in `systems/__init__.py`.

No change to `make_console.py` or the shared modules is needed for a normal console.

Rebuild both committed assets (the determinism check; run each twice if you are changing the generator):

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system snes --output /tmp/snes.glb
blender --background --python tools/carts/console/make_console.py -- \
    --system nes --output /tmp/nes.glb
sha256sum /tmp/snes.glb /tmp/nes.glb
```

The SNES GLB must be `63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c` (2,659,776 B) and the
NES GLB `3b3bd5e65982aa263d6615488068f8fd2701b53b32e9cc1256b3d9ddf905cee7` (3,414,076 B); both are checked
against the committed files by hash. `--previews` writes the same three renders per system with the row's prefix
and resolution (`front.png`/`three-quarter.png`/`angle-120.png`, `1000 x 765` for the SNES;
`nes-front.png`/`nes-three-quarter.png`/`nes-angle-120.png`, `880 x 660` for the NES) into the directory you pass,
so point it at a scratch directory when you do not intend to replace the tracked previews.
