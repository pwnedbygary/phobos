# NES-001 diorama: modelling reference

This is a text-only reference note for the NES diorama generator
(`phobos_console/systems/nes.py`). **No reference photograph is committed**: the
eight photos used during modelling are scratch inputs in `refs/nes/` (outside
this repository's tracked files) and are not redistributed here.

## Sources

- eXretro NES console gallery: <https://exretro.com/galleries/nintendo/nes/Console/index.html>
  - `nes-01.jpg` — top/rear three-quarter: vent grille, black end caps, rear panel.
  - `nes-02.jpg` — top/front-right three-quarter: lid, wordmark, grille, tower.
  - `nes-03.jpg` — front panel: lid wordmark, taupe lower chassis, POWER/RESET/LED, ports.
  - `nes-04.jpg` — front-right low angle: tower and controller sockets.
  - `nes-05.jpg` — the side with the AUDIO/VIDEO RCA outputs and the rear oval vent.
  - `nes-06.jpg` — rear-right three-quarter: side outputs and rear connectors.
  - `nes-07.jpg` — rear panel: power connector, channel switch, antenna jack.
  - `nes-08.jpg` — the plain side; the gallery caption calls this the left side.
- Dimensions.com, Nintendo Entertainment System: <https://www.dimensions.com/element/nintendo-entertainment-system-nes>
  — lists 256 x 203.2 x 88.9 mm overall.
- The owner's revision brief (2026-11-07, revised the same day) supplied the
  working dimensions and the final colour calls: **256 mm wide x 203 mm deep x
  76 mm high**, warm ivory upper shell, and a **warm taupe-grey** lower chassis
  (sRGB about 0.58/0.55/0.50) whose contrast comes from hue more than value.
  The model follows that 76 mm figure; the dimensions.com overall height is
  noted as a discrepancy rather than silently averaged.
- NES-004 controller: the owner's brief supplies 120 x 56 x 20 mm and the
  colour layout; the console gallery has no controller photos.

## Dimensions and layout used

All values are millimetres in the generator's Blender space: X width, Y depth,
+Z up, front toward -Y, base at Z=0. `export_yup=True` maps that to glTF +Y up.

| Item | Value |
|---|---|
| Shell | 256.0 W x 203.0 D x 76.0 H (including 4 mm feet) |
| Upper/lower seam | Z = 27.0 mm; the taupe lower chassis is about 30% of the height |
| Cartridge lid | the whole upper front panel: seam from X -126 (body edge) to +55 (tower), lower seam at Z 44.5 |
| Lid top hinge | thin line at Y = -76.5 with side seams down the top to the front edge |
| Wordmark | printed on the lid, left at X -112, baseline Z 67.0/55.2; Nintendo 15 mm + ® 4 mm, letter-spaced caps 4.6 mm |
| Controller tower | 43 mm wide (X 54..97), 45 mm deep, full height, front face flush at Y -101.5 |
| Controller sockets | two 24.6 mm tall oval plates in 28.4 mm rims, centres X +64.5/+86.5, Z 23; 7 pins each |
| Vent grille | 14 cream slats on the top right, 37 mm wide, black end caps front/rear |
| Lower front panel | 121 x 20 mm recessed outline; flat taupe buttons and LED, LED left of POWER |
| Side RCA panel | left side, 60 x 24 mm, AUDIO (red) and VIDEO (warm metal) |
| Rear | label recess, RF channel switch, antenna jack, power connector |
| Controller | 120 x 56 x 20 mm; body crown, 26 mm D-pad, 11.6 mm A/B |

The overall diorama bounds are 641.6 x 659.3 x 352.0 mm; the console itself is
256 x 203 x 76 mm. The diorama is centred on X=Y=0 with the base at Z=0 before
the export, so the turntable spins in place.

## Feature checklist (photo -> model)

| Feature | Reference evidence | Model |
|---|---|---|
| Two-tone shell | nes-02/03/08: warm ivory upper over a slightly darker warm taupe-grey lower chassis, crisp seam | `nes_upper` (ivory `shell`) and `nes_lower` (taupe `accent_light`) with a dark seam ring at Z=27 |
| Flip cartridge lid | nes-02/03: the whole upper front is the lid, thin seam, top hinge line, lower lip and thumb notch | no raised plaque: `nes_lid_seam_bottom`, `nes_lid_topseam`, `nes_lid_topside`, `nes_lid_lip`, `nes_lid_thumb`; the wordmark is printed on the body lid face |
| Wordmark | nes-03: large red "Nintendo" with a small ® immediately after, then smaller letter-spaced "ENTERTAINMENT SYSTEM" | `_left_text` wordmark at X -112, Z 67/55.2; red `accent`, `nes_wordmark_reg`; no inner panel |
| Vent grille | nes-01/02: ~14 cream slats running across a black-end-capped top-right strip | `nes_grille_recess`, 14 `nes_grille_bar`, `nes_grille_rear`; the tower is the front end cap |
| Controller tower | nes-02/03/04: flat black front-right bezel flush with the front, two tall oval sockets, red "1"/"2" | `nes_port_tower` flush at Y -101.5, flat 0.35 mm rims, dark oval socket plates, 7 `metal` pins each, red numerals |
| Front-left panel | nes-03: recessed taupe panel, square red LED to the left of POWER, two mid-grey buttons with dark bezels and red legends | `nes_btn_panel_groove`, flat `nes_power`/`nes_reset` in taupe, red labels and `glass` LED |
| Side outputs | nes-05/06: AUDIO/VIDEO RCA pair in a side recess, with red labels | left-side `nes_av_recess`/`nes_av_panel`, red AUDIO jack, warm-metal VIDEO jack, red `_face_text` labels |
| Rear | nes-06/07: power connector, RF channel switch, antenna jack, moulded label recess | `nes_rear_label_recess`/`_plate`, `nes_rf_switch`/`_slider`, `nes_antenna`, `nes_power_jack`, small red labels |
| Feet | nes-01/08: four recessed rubber feet under the corners, with dark moulded cutouts | four `nes_foot` pads plus front/side `nes_foot_cutout` notches |
| Controller | owner brief: cream 120 x 56 x 20 body, dark-grey cross D-pad, dark-grey Start/Select pills, dark-red A/B, black cable from the centre top | `nes_controller`, `nes_dpad_*`, `nes_select`/`nes_start`, `nes_btn_a`/`_b`, `nes_ctrl_strain` and the cable |

## Measured, inferred, stylised

- **Measured from the brief**: 256 x 203 x 76 mm, controller 120 x 56 x 20 mm,
  warm ivory upper and warm taupe-grey lower. The 76 mm height is the brief's
  figure and is used as the source of truth; dimensions.com gives 88.9 mm
  overall, and the photos are not a calibrated orthographic source, so no
  photo-derived correction was applied.
- **Measured from the photos** (proportions, not absolute): the seam is below
  mid-height; the lid is the whole upper front panel and its seam runs from the
  left body edge to the tower; the tower is full-height and flush; the two
  sockets are tall ovals side by side with the numerals above them; the grille
  is a narrow top-right strip with black ends; the lower panel sits toward the
  left of the taupe chassis and the LED is left of POWER. Two of those were
  then taken to millimetres against nes-03's front face, 1985 px = 256 mm
  (7.75 px/mm): the black tower measures 330 px, about 42.6 mm, so the model
  uses 43 mm (43 x 7.75 = 333 px, 0.9% over the photo); the two socket plates
  measure 132 px, about 17.0 mm, with 2.4 mm between them and 1.8/4.3 mm to
  the tower's edges. The lower band's value came the same way, as a ratio of
  the two coplanar front faces rather than a colour picked by eye: the reference
  reads 75/165 = 0.46, the committed render 0.63 after the palette nudge.
- **Stylised / approximated**:
  - The wordmark uses Blender's bundled Bfont and a separate ® glyph, not
    Nintendo's licensed logo artwork; the letter-spaced second line is built by
    adding spaces between the capitals.
  - POWER/RESET lettering is solid red rather than outlined red; the legends
    are text meshes, not decals.
  - The VIDEO RCA jack uses the NES palette's warm satin `metal` rather than a
    separate yellow material, keeping the seven-material contract.
  - The side carrying the RCA outputs is the left side (the brief's wording and
    the side visible in the 120-degree rear-left preview). The gallery is
    ambiguous here: its caption calls photo 08, the plain side, the left side,
    while photo 05 shows the outputs.
  - The flap is modelled closed; there is no tray, cartridge or slot interior.
  - The vent has 14 cream slats; the exact slot count in the photos is hard to
    count and is treated as a family-level approximation.
  - The 7 pins per socket are simplified cylinders; the socket interior is a
    flat dark plate with a thin rim rather than a hollow moulded cavity.
  - No texture or artwork is baked into the GLB. The preview test frame is
    wired in only after export; `--screen` can put a real capture there for
    preview renders only.

## Asset contract and evidence

- Asset: `android/app/src/main/assets/carts/consoles/nes.glb`, Phobos's own
  model, GPL-3.0-or-later.
- sha256 `ef71cfc1bb00ed612cbfb8ac7b375cb9619ed18533ba5322e76846bbb058766c`,
  4,414,624 B, 191,772 triangles, 100,897 Blender mesh vertices, 136,479 GLB
  POSITION entries (the mesh is the same 204/203-node topology with values
  changed; the exporter welds nine duplicates fewer than at 46 mm).
- One `nes_diorama` root; 204 nodes / 203 meshes, all named. Seven single-sided
  materials `shell`, `dark`, `accent`, `accent_light`, `metal`, `glass`,
  `screen`. No textures, images, cameras or lights. Only `tv_screen` carries
  TEXCOORD_0, with u/v 0..1.
- Rebuild command:
  ```sh
  blender --background --python tools/carts/console/make_console.py -- \
      --system nes --output android/app/src/main/assets/carts/consoles/nes.glb
  ```
- Two no-preview rebuilds and the committed asset compare equal. The SNES asset
  is unchanged: `63a34016613490bb877ae57696e504f38b7d93eb4766d4072d07afcb99f0640c`,
  2,659,776 B. Determinism is proven only on Blender 5.2.3 built 2026-10-22.
- Committed previews (`nes-front.png`, `nes-three-quarter.png`,
  `nes-angle-120.png`, 880 x 660, 40 samples) use the generated facsimile frame,
  as the owner's real NES capture was not available. They are documentation,
  not assets; `--screen refs/screens/nes.png` renders a real capture into a
  scratch directory without changing the GLB, and no capture is committed.
