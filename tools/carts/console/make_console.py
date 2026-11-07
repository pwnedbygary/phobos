#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake a Phobos system diorama (console + CRT TV + controller) to a GLB.

The model is Phobos's own work and is generated here, not downloaded: a 3DS
Virtual-Console-style diorama of a real console next to a CRT TV whose screen is
a clean UV 0..1 target for the app's artwork.  The systems implemented so far
are the SNES (the North-American SNS-001 shell) and the NES (the NES-001 front
loader), each with its own controller and cable; the CRT TV, stand lighting and
preview rig are shared so the family matches.

The CLI lives here; the modelling code is the ``phobos_console`` package beside
this file: ``common.py`` has the shared helpers and preview rig, ``crt.py`` the
shared 14-inch TV stage, ``palette.py`` the colours every stage draws the same,
and ``systems/<name>.py`` one module per system (its own palette, console,
controller and cable).  ``systems/__init__.py`` discovers those modules, so
adding a system is a new file and nothing else: there is no shared table, import
or palette to edit and two authors never touch the same file.  The layout and
the determinism check are documented in ``tools/carts/README.md``.

Run it with Blender 5.2 (it needs only bpy/bmesh):

    blender --background --python tools/carts/console/make_console.py -- \
        --system snes \
        --output android/app/src/main/assets/carts/consoles/snes.glb

Add `--previews tools/carts/console/previews` to also render the three Cycles
checks (front, three-quarter, 120 degrees round).  The screen artwork exists only
in those previews: it is wired into the `screen` material after the GLB has been
written, so the committed asset stays a plain material the app stamps.

Units are millimetres: width on X, up on Z, the TV/console faces toward -Y, the
base on Z=0 and the footprint centred on X=Y=0, so a turntable about the Z axis
through the origin spins the whole diorama in place.  `export_yup=True` turns
that into the glTF convention (up +Y, the TV screen toward +Z, footprint centred
on X=Z=0).  Materials are the CartMaterials names: `shell` (the colour-tintable
plastic), `dark`, `accent` (SNES purple), `metal`, `glass` (the LED lenses) and
`screen` (the CRT target); every object uses exactly one of them.

All placement transforms are baked into the meshes, so every exported node has an
identity transform under the single `<system>_diorama` root node.  Nothing is
randomised and Blender's bundled font supplies the embossed lettering: the same
Blender build rebuilds the GLB byte for byte.
"""
import argparse
import os
import sys

import bpy

# The package sits beside this script.  Blender's --python does not put the
# script's directory on sys.path, so add it before the package imports.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Re-exported so the public names of the old single-file generator stay
# importable from the entry point; the CLI uses the ones main() calls.  The
# __all__ at the end of this block is what marks every re-export as public.
from phobos_console.common import (  # noqa: E402
    bake_matrix, bake_place, clear_scene, cylinder, dome, export_glb,
    loft_smooth, loft_xz, make_material, make_materials, model_bounds,
    model_stats, new_obj, parent_to_root, place, recenter, render_previews,
    ring_solid, round_drop, rounded_box, rounded_solid, rr_loop, screen_image,
    spline, srgb_to_linear, text_mesh, tube, wire_screen_art)
from phobos_console.crt import TV_AT, add_crt, build_tv  # noqa: E402
from phobos_console.systems import STAGES, SYSTEMS  # noqa: E402
from phobos_console.systems.snes import (  # noqa: E402
    build_cable, build_snes, build_snes_controller, snes_slope, snes_top)
from phobos_console.systems.nes import (  # noqa: E402
    build_nes, build_nes_cable, build_nes_controller, nes_top)

#: The public surface: the shared helpers, the CRT stage, the registry, and the
#: SNES/NES builders the pre-split generator exposed from this module.  A new
#: system needs no entry here - it lives only in its own
#: ``phobos_console/systems/<name>.py`` and its CLI row is discovered.
__all__ = [
    "SYSTEMS", "STAGES", "TV_AT", "add_crt", "bake_matrix", "bake_place",
    "build_cable", "build_nes", "build_nes_cable", "build_nes_controller",
    "build_snes", "build_snes_controller", "build_tv", "clear_scene",
    "cylinder", "dome", "export_glb", "loft_smooth", "loft_xz", "main",
    "make_material", "make_materials", "model_bounds", "model_stats",
    "nes_top", "new_obj", "parent_to_root", "place", "recenter",
    "render_previews", "ring_solid", "round_drop", "rounded_box",
    "rounded_solid", "rr_loop", "screen_image", "snes_slope", "snes_top",
    "spline", "srgb_to_linear", "text_mesh", "tube", "wire_screen_art",
]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--system", default="snes", choices=sorted(SYSTEMS))
    ap.add_argument("--output", default=None,
                    help="GLB output path (default: the system's asset path)")
    ap.add_argument("--previews", default=None,
                    help="directory for the front / three-quarter / 120 degree renders")
    ap.add_argument("--samples", type=int, default=48)
    args = ap.parse_args(argv)
    cfg = SYSTEMS[args.system]

    clear_scene()
    mats = make_materials(cfg["palette"])
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 0.001

    stage = STAGES[cfg["stage"]]
    root = cfg["builder"](mats, stage)
    if root != cfg["root"]:
        raise SystemExit(
            f"{args.system}: builder returned root {root!r}, "
            f"SYSTEMS expects {cfg['root']!r}")

    shift = recenter()
    parent_to_root(root)
    verts, tris = model_stats()
    print(f"recentre shift {tuple(round(v, 3) for v in shift)}")
    (x0, x1), (y0, y1), (z0, z1) = model_bounds()
    print(f"STATS verts={verts} tris={tris}")
    print(f"BOUNDS x {x0:.1f}..{x1:.1f} (w {x1 - x0:.1f}), y {y0:.1f}..{y1:.1f} (d {y1 - y0:.1f}), "
          f"z {z0:.1f}..{z1:.1f} (h {z1 - z0:.1f})")
    print("MATERIALS " + ", ".join(sorted(m.name for m in bpy.data.materials)))
    digest, size = export_glb(args.output or cfg["output"], root)

    if args.previews:
        wire_screen_art(mats)
        render_previews(args.previews, samples=args.samples,
                        prefix=cfg["preview_prefix"], res=cfg["preview_res"])
    print(f"DONE sha256={digest} bytes={size} verts={verts} tris={tris}")


if __name__ == "__main__":
    main()
