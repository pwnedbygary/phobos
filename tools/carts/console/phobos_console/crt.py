# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The shared 14-inch 4:3 CRT TV stage.

Both the SNES and NES dioramas stand their console beside the same television;
the cabinet, screen target, bezel and controls live here so the family matches
and a future system can choose a stage kind that has no CRT.
"""
import math

import bmesh

from .common import (bake_place, cylinder, loft_smooth, new_obj, place,
                     ring_solid, rounded_box, rr_loop, text_mesh)

# ---------------------------------------------------------------------------
# CRT TV
# ---------------------------------------------------------------------------
TV_AT = (105.0, 30.0, 0.0)


def build_tv(mats, location):
    objs = []
    dark, metal = mats["dark"], mats["metal"]
    glass, screen = mats["glass"], mats["screen"]

    # One lofted housing instead of a box plus a separate rear tube: the bottom
    # stays flat on the feet while the top and the CRT tube taper toward the back.
    objs.append(loft_smooth("tv_housing", [
        (-163.0, 356.0, 330.0, 20.0),
        (-110.0, 350.0, 324.0, 20.0),
        (-40.0, 336.0, 312.0, 18.0),
        (30.0, 314.0, 292.0, 18.0),
        (90.0, 282.0, 262.0, 18.0),
        (140.0, 240.0, 224.0, 18.0),
        (185.0, 210.0, 200.0, 18.0),
        (220.0, 170.0, 160.0, 16.0),
        (243.0, 130.0, 120.0, 14.0),
        (252.0, 110.0, 100.0, 12.0),
    ], samples=46, seg=36, mat=dark, bottom_z=20.0))

    # feet: rounded pads, not blobs
    # The housing tapers in plan toward the rear, so the rear pair sits inboard of
    # the front pair: at y=+90 the bottom's half-width is about 139 mm, and a
    # 42 mm pad at x=105 spans 84..126 mm, clear of the taper.
    for sx in (-1, 1):
        for fy, fx in ((-148.0, 132.0), (90.0, 105.0)):
            objs.append(rounded_box("tv_foot", 42.0, 34.0, 20.0, 6.0, 5, dark,
                                    center=(sx * fx, fy, 10.0)))

    # front bezel: a rounded picture frame, 24 mm deep, outer 360 x 334
    bez = ring_solid(
        "tv_bezel", rr_loop(360.0, 334.0, 22.0, 22),
        [(x, y + 37.0) for x, y in rr_loop(292.0, 219.0, 12.0, 22)],
        0.0, 24.0, dark, bevel=2.2, bevel_seg=4)
    bake_place(bez, (0.0, -163.0, 185.0), rot_x=math.radians(90.0))
    objs.append(bez)

    # the CRT face: a gently bulged 4:3 grid with clean 0..1 UVs
    sw, sh, bulge, zc = 290.0, 217.0, 6.5, 222.0
    nx, nz = 64, 48
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    grid = []
    for iz in range(nz + 1):
        row = []
        for ix in range(nx + 1):
            x = -sw * 0.5 + sw * ix / nx
            z = zc - sh * 0.5 + sh * iz / nz
            r2 = (2.0 * x / sw) ** 2 * 0.35 + (2.0 * (z - zc) / sh) ** 2 * 0.35
            row.append(bm.verts.new((x, -176.0 - bulge * max(0.0, 1.0 - r2), z)))
        grid.append(row)
    for iz in range(nz):
        for ix in range(nx):
            f = bm.faces.new((grid[iz][ix], grid[iz][ix + 1],
                              grid[iz + 1][ix + 1], grid[iz + 1][ix]))
            f.normal_update()
            for loop in f.loops:
                v = loop.vert.co
                loop[uv].uv = ((v.x + sw * 0.5) / sw,
                               (v.z - (zc - sh * 0.5)) / sh)
    scr = new_obj("tv_screen", bm, screen, smooth=True)
    scr.data.update()
    objs.append(scr)
    surround = ring_solid(
        "tv_screen_surround", rr_loop(300.0, 227.0, 12.0, 16),
        rr_loop(290.0, 217.0, 10.0, 16), 0.0, 13.0, dark)
    bake_place(surround, (0.0, -163.0, zc), rot_x=math.radians(90.0))
    objs.append(surround)

    # speaker grille: raised dark panel with satin bars
    objs.append(rounded_box("tv_grille_panel", 146.0, 2.4, 58.0, 10.0, 4, dark,
                            center=(-108.0, -188.2, 62.0)))
    for k in range(9):
        objs.append(rounded_box("tv_grille_bar", 132.0, 2.0, 2.6, 1.0, 3, metal,
                                center=(-108.0, -189.6, 38.0 + k * 5.6)))

    # two knobs, axis along Y
    for kx in (100.0, 142.0):
        objs.append(cylinder("tv_knob", 19.0, 18.0, 80, dark,
                             center=(kx, -196.0, 72.0), axis='Y', bevel=1.6,
                             bevel_seg=3))
        objs.append(cylinder("tv_knob_cap", 6.8, 2.4, 48, metal,
                             center=(kx, -204.8, 72.0), axis='Y', bevel=0.7,
                             bevel_seg=2))
        for i in range(24):
            a = 2.0 * math.pi * i / 24.0
            ridge = rounded_box("tv_knob_ridge", 3.0, 16.0, 3.0, 0.8, 2, dark)
            bake_place(ridge, (kx + 18.2 * math.cos(a), -196.0,
                               72.0 + 18.2 * math.sin(a)), rot_y=-a)
            objs.append(ridge)
        objs.append(rounded_box("tv_knob_mark", 1.8, 4.0, 1.2, 0.4, 2, metal,
                                center=(kx, -207.4, 78.0)))

    objs.append(rounded_box("tv_power", 26.0, 8.0, 14.0, 3.0, 5, dark,
                            center=(40.0, -190.4, 72.0)))
    objs.append(cylinder("tv_led", 3.4, 2.4, 32, glass,
                         center=(66.0, -188.6, 72.0), axis='Y'))
    badge = text_mesh("tv_badge", "PHOBOS", 6.2, 0.5, 0.12, metal,
                      resolution_u=5, bevel_resolution=1)
    bake_place(badge, (6.0, -187.4, 100.0), rot_x=math.radians(90.0))
    objs.append(badge)

    # rear panel and vents on the small flat tail
    objs.append(rounded_box("tv_rear_plate", 96.0, 2.4, 80.0, 5.0, 3, dark,
                            center=(0.0, 252.0, 70.0)))
    for k in range(8):
        objs.append(rounded_box("tv_vent_rear", 72.0, 1.4, 3.0, 0.8, 2, metal,
                                center=(0.0, 253.6, 38.0 + k * 8.0)))
    for sx in (-1, 1):
        for sz in (36.0, 104.0):
            objs.append(cylinder("tv_screw", 2.6, 1.2, 24, metal,
                                 center=(sx * 41.0, 253.4, sz), axis='Y'))

    place(objs, location)
    return {"objs": objs}


def add_crt(mats):
    """Place the shared CRT TV as the diorama's stage.

    The systems table names this stage kind ``diorama``; a handheld system can
    use the ``handheld`` kind and skip it entirely.
    """
    return build_tv(mats, TV_AT)
