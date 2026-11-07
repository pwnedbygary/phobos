# Sega Mega Drive / Genesis Model 1 — reference record

This is the reference record for the Phobos Mega Drive diorama. The model is
Phobos's own work (GPL-3.0-or-later). The reference photographs were inspected
in scratch only and are not committed. No photograph, box art, scan or logo
image is embedded in the GLB or in the repository; the lettering on the model is
our own extruded type, modelled from the photographs, and the screen artwork
exists only in preview renders.

## Sources

- **Console, Model 1 (Japanese Mega Drive)** — Wikimedia Commons,
  [File:Sega-Mega-Drive-JP-Mk1-Console-Set.jpg](https://commons.wikimedia.org/wiki/File:Sega-Mega-Drive-JP-Mk1-Console-Set.jpg),
  photo by Evan-Amos, public domain. 4,240 × 2,120. Front three-quarter, oval
  cartridge door, blue RESET, volume slider, controller and cable.
- **Console, US Genesis Model 1** — Wikimedia Commons,
  [File:Sega-Genesis-Mod1-Set.jpg](https://commons.wikimedia.org/wiki/File:Sega-Genesis-Mod1-Set.jpg),
  photo by Evan-Amos, public domain. 3,800 × 1,640. The 16-BIT-era oval lid,
  vents, front ports and the controller's shape.
- **Three-button controller** — Wikimedia Commons,
  [File:Sega-Genesis-3But-Cont.jpg](https://commons.wikimedia.org/wiki/File:Sega-Genesis-3But-Cont.jpg),
  photo by Evan-Amos, public domain. 3,780 × 2,580. Front and underside; D-pad,
  START, A/B/C diagonal, TRIGGER lettering and cable strain relief.
- **Top / three-quarter / side views** — Wikimedia Commons,
  [File:Mega Drive (01).jpg](https://commons.wikimedia.org/wiki/File:Mega_Drive_(01).jpg),
  [File:Mega Drive (02).jpg](https://commons.wikimedia.org/wiki/File:Mega_Drive_(02).jpg),
  [File:Mega Drive (03).jpg](https://commons.wikimedia.org/wiki/File:Mega_Drive_(03).jpg),
  photos by Sintegrity, CC BY-SA 4.0. Used for the top oval proportions, the
  sloping lid, the rear-top slot and the side panels; attribution retained here,
  not in the model.
- **Controller close-ups** — Wikimedia Commons,
  [File:Mega Drive Controllers.jpg](https://commons.wikimedia.org/wiki/File:Mega_Drive_Controllers.jpg)
  (ThomasHarte, public domain) and
  [File:Sega-Mega-Drive-controllers.jpg](https://commons.wikimedia.org/wiki/File:Sega-Mega-Drive-controllers.jpg)
  (X201, public domain), for the A/B/C row and the cable plug.

The documented external dimensions supplied with the task are **280 × 212 ×
79 mm** (width × depth × height). The photographs were taken with perspective,
so the measurements below are approximate pixel ratios cross-checked against
that box, not a caliper measurement of a physical unit.

## Proportions used in the model

Front is -Y, up is +Z, width is X. The console is centred at x=-220, the shared
CRT at its usual place, and the controller in front at x=-68.

| part | model dimension | basis / note |
|---|---:|---|
| body footprint | 280 × 212 mm | documented box |
| body height | rear 79 mm, front ~55.5 mm | side photographs; wedge top |
| top edge round | 5.5 mm | reading of the side/top photos |
| oval cartridge door | 238 × 164 mm, centre y=+34 | top photos; spans most of the lid |
| lid badge | 184 × 108 mm | raised silver SEGA / 16-BIT area |
| cartridge | 152 × 23 × 96 mm | inserted rear-top, visibly taller than the lid |
| slot | 154 × 33 mm at y=+69 | rear-top of the sloping lid |
| front control panel | 154 × 44 mm at x=-83 | POWER / RESET / VOLUME side |
| reset button | 16 × 10 mm, blue | Japanese front reference |
| controller port pitch | 46 mm between centres | front-right recess |
| controller body | 154 × 96 mm, ~27 mm thick | controller photos, slightly wider than the Sega pad to read at this scale |
| D-pad | 33 mm cross | controller photo |
| A/B/C | 7.4 mm domes on ~21 mm diagonal pitch | controller photo; red |
| Start | 20 × 7 mm blue pill | controller photo |

The console's top slopes continuously from the low front to the high rear, with
the oval lid let into it. The cartridge is a separate inserted slab leaning
about 7° back, with a printed label recess, 16-BIT/Sega type, and moulded ribs on
its back. The large oval is not a label: it is the Model 1's cartridge door.

## Feature checklist

- [x] black/charcoal wedge body with a sloping top and rounded edges
- [x] large oval cartridge door on the top
- [x] rear-top cartridge slot, with a separate ridged cartridge
- [x] front POWER slider, RESET button, VOLUME slider, LED and headphone jack
- [x] red POWER/ABC accents and blue RESET/START accents
- [x] recessed front-right panel with two DB9-style controller ports
- [x] SEGA / 16-BIT era wordmarks, rebuilt as extruded type: front MEGA DRIVE /
      SEGA, the raised oval SEGA / 16-BIT badge, cartridge label lettering, and
      POWER / RESET / VOLUME / CONTROLLER / 1 / 2 labels
- [x] top vent slots and side vent slots
- [x] rear panel with AV/RF jacks, channel switch, vents and 16-BIT marking
- [x] four inset rubber feet
- [x] three-button controller: rounded charcoal body, black face inset,
      D-pad, blue START, red A/B/C diagonal row, TRIGGER and SEGA lettering,
      cable and strain relief to port 1
- [x] the shared 14-inch CRT, with the screen as the only 0..1 UV target

## Lettering and material note

The SEGA, MEGA DRIVE, 16-BIT, POWER, RESET, VOLUME, START, TRIGGER, A/B/C and
CONTROLLER lettering is our own text converted to meshes in Blender's bundled
font; it is not copied from a logo file or a font asset. The plastic is one
charcoal ABS family but the roughness is deliberately varied by part: the
shell/bevels, the darker recesses, the matte red and blue buttons, the satin
metal lettering and the glass LED. There are no baked textures. If a texture is
ever proposed it must be discussed first: the current GLB contract is plain
materials the app can tint, no textures or images.

## Preview screen

The shared preview rig takes `--screen <png>` and centre-crops a real 4:3
gameplay capture to the CRT's 0..1 UVs. That is a preview/documentation input
only: it is wired into the `screen` material after the GLB is written, never
exported, and the capture is never committed. The Mega Drive committed previews show one such real frame (documentation only); the owner
may direct a return to the facsimile frame.

## Limits

- The reference photography was viewed at scratch resolution; no physical
  console or calipers were available. The dimensions and proportions are the
  documented box plus approximate photo ratios.
- The rear-panel layout is modelled from the visible front/side/top views and
  common Model 1 layout knowledge; a dedicated rear close-up was not retained
  (Wikimedia was rate-limiting the download).
- One game/state will be used for the preview capture, so the screen artwork does
  not expand the evidence beyond that frame.
- This is a stylised diorama model, not a manufacturing CAD reproduction; the
  target is a recognisable, reference-anchored Model 1 at diorama scale.
