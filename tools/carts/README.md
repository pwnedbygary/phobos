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
`--preview out.png` renders a front three-quarter check with a test inlay. `bake-carts.mjs --check` covers its own 25
outputs and does not include this file.

## The system dioramas: SNES + CRT TV (authored, not generated by `bake-carts.mjs`)

`android/app/src/main/assets/carts/consoles/snes.glb` is Phobos's own model and GPL-3.0-or-later: a 3DS
Virtual-Console-style scene of the real SNES (SNS-001) beside a 14-inch 4:3 CRT TV whose screen is a live
artwork target, with the SNES controller and its cable in front. It is not a `bake-carts.mjs` output (Cartridge
has cart and disc geometry only) and not a cart shell: it is a whole diorama the app can display and rotate in
place.

Units are millimetres, width on X, up on Z and the console/TV facing -Y, matching the carts' convention. The
580.6 x 688.0 x 352.1 mm bounding box is centred on X=Y=0 with its base on Z=0, so a turntable about +Z spins the
whole scene in place; `export_yup=True` turns that into the glTF convention (up +Y, the TV screen toward +Z,
footprint centred on X=Z=0, base on Y=0). One root node `snes_diorama` carries 160 named parts,
59,365 vertices and 111,920 triangles in 2.95 MB.

Materials are the CartMaterials names plus one extra: `shell` (the colour-tintable plastic: console and
controller shells and the controller's shoulder buttons), `dark`, `accent` (SNES purple: POWER/RESET and the
convex A/B buttons), `metal`, `glass` (the two LED lenses), `screen` (the CRT face) and `accent_light` (the
lavender X/Y buttons, a colour the six-name palette does not otherwise carry). The TV's 290 x 217 mm CRT face is
a 64 x 48 grid with a gentle 6.5 mm bulge and clean 0..1 UVs (`u` left to right, `v` bottom to top), and its
`screen` material is plain and slightly emissive: the app stamps the running game onto it. The preview test
frame is generated inside the script and wired into `screen` only after the GLB has been written, so no artwork
is baked into the asset.

`tools/carts/console/make_console.py` rebuilds it with Blender 5.2.3 (bpy/bmesh only, no external assets):

```sh
blender --background --python tools/carts/console/make_console.py -- \
    --system snes --output android/app/src/main/assets/carts/consoles/snes.glb
```

Add `--previews tools/carts/console/previews` to also render the three Cycles checks (front, three-quarter and
120 degrees round). The same Blender 5.2.3 build produces byte-identical output (sha256
`9339d430417ff06ea67da80e850c8da629c73fbbc37c9c5f859ba335a0fdd018`, 2,949,664 B); two runs and the committed
file compare equal. `bake-carts.mjs --check` covers its own outputs and does not include this file.

Limits: `--system` accepts `snes` only. The model is a recognisable stylised approximation - the SNES footprint
is the SNS-001's approximate 200 x 190 mm at 74 mm high including feet, the TV is a generic 14-inch set, and
nothing here is measured against a source. The only UVed part is the screen and there are no baked textures.
Parts stay separate named meshes (160 nodes) so the screen remains a clean target and `shell` remains tintable;
merging by material would be a consumer-side optimisation. The previews are illustrative renders, not assets.
Byte-for-byte determinism is proven only on Blender 5.2.3 built 2026-10-22 (the build used here).
