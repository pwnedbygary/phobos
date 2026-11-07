# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The SNES (SNS-001) diorama: console, controller and cable.

The shared CRT TV is placed by the stage callable passed to :func:`build`, so
the console is authored here and nowhere else.
"""
import math

from mathutils import Matrix, Vector

from ..common import (bake_place, cylinder, dome, place, ring_solid,
                      rounded_box, rounded_solid, rr_loop, spline, text_mesh,
                      tube)
from ..palette import SNES_PALETTE

ROOT = "snes_diorama"
OUTPUT = "android/app/src/main/assets/carts/consoles/snes.glb"
PREVIEW_PREFIX = ""
PREVIEW_RES = (1000, 765)
STAGE = "diorama"
PALETTE = SNES_PALETTE

SNES_CTRL_AT = (-80.0, -345.0, 0.0)
SNES_CTRL_ROT = -8.0

# ---------------------------------------------------------------------------
# SNES (SNS-001) + controller + cable
# ---------------------------------------------------------------------------
SNES_W, SNES_D = 200.0, 190.0
SNES_BASE = 2.0
SNES_AT = (-195.0, -165.0, 0.0)


def snes_top(x, y):
    t = max(0.0, min(1.0, (y + 70.0) / 80.0))
    s = t * t * (3.0 - 2.0 * t)
    h = 59.0 + 15.0 * s
    h += 1.0 * math.cos(math.pi * x / 200.0) * math.cos(math.pi * y / 190.0) ** 2
    return h


def snes_slope():
    return math.atan2(snes_top(0.0, -49.0) - snes_top(0.0, -51.0), 2.0)


def build_snes(mats, location):
    objs = []
    shell, dark = mats["shell"], mats["dark"]
    accent, metal, glass = mats["accent"], mats["metal"], mats["glass"]

    objs.append(rounded_solid(
        "snes_body", SNES_W, SNES_D, 26.0, snes_top, SNES_BASE, shell,
        top_edge=10.0, bottom_edge=3.0, seg=44, rings=38, edge_rings=8,
        bottom_rings=9, bottom_edge_rings=4))

    objs.append(ring_solid(
        "snes_seam", rr_loop(200.5, 190.5, 26.2, 24),
        rr_loop(199.5, 189.5, 25.8, 24), 21.8, 22.7, dark))
    for sx in (-1, 1):
        for sy in (-1, 1):
            objs.append(cylinder("snes_foot", 7.0, 2.4, 40, dark,
                                 center=(sx * 72.0, sy * 60.0, 1.1),
                                 bevel=0.5, bevel_seg=2))

    # cartridge slot: raised rim, dark slot plate, closed flap, eject button
    slot_y = 52.0
    slot_top = snes_top(0.0, slot_y)
    objs.append(ring_solid(
        "snes_slot_rim", rr_loop(106.0, 68.0, 1.0, 20), rr_loop(94.0, 58.0, 5.0, 20),
        slot_top - 1.0, slot_top + 2.8, shell))
    objs.append(rounded_box("snes_slot_plate", 94.0, 58.0, 1.2, 0.4, 2, dark,
                            center=(0.0, slot_y, slot_top + 1.0)))
    flap = rounded_box("snes_flap", 93.0, 57.0, 2.4, 1.0, 4, dark)
    bake_place(flap, (0.0, slot_y, slot_top + 2.5), rot_x=math.radians(-1.5))
    objs.append(flap)
    objs.append(rounded_box("snes_eject", 20.0, 14.0, 4.5, 1.8, 6, shell,
                            center=(-68.0, slot_y, slot_top + 1.8)))
    objs.append(rounded_box("snes_eject_groove", 12.0, 2.0, 1.0, 0.4, 2, dark,
                            center=(-68.0, slot_y + 3.0, slot_top + 4.1)))
    eject_label = text_mesh("snes_eject_label", "EJECT", 3.6, 0.3, 0.06, dark,
                            resolution_u=4, bevel_resolution=1)
    bake_place(eject_label, (-68.0, slot_y - 12.0, slot_top + 0.1))
    objs.append(eject_label)

    # purple POWER / RESET switches on the sloping front deck
    slope = snes_slope()
    for name, sx, sw, sd in (("snes_power", -66.0, 30.0, 13.0),
                             ("snes_reset", -38.0, 15.0, 11.0)):
        top = snes_top(sx, -50.0)
        b = rounded_box(name, sw, sd, 4.0, 1.2, 5, accent)
        bake_place(b, (sx, -50.0, top + 1.2), rot_x=slope)
        objs.append(b)
    objs.append(cylinder("snes_led", 2.6, 1.2, 32, glass,
                         center=(-21.0, -50.0, snes_top(-21.0, -50.0) + 0.55)))
    for name, sx, label in (("snes_power_label", -66.0, "POWER"),
                            ("snes_reset_label", -38.0, "RESET")):
        t = text_mesh(name, label, 4.2, 0.35, 0.06, dark,
                      resolution_u=4, bevel_resolution=1)
        bake_place(t, (sx, -62.5, snes_top(sx, -62.5) + 0.15), rot_x=slope)
        objs.append(t)

    # two controller ports: recessed dark plate, rim, seven pins, numbers
    pins = []
    for side, sx in (("1", -34.0), ("2", 34.0)):
        objs.append(rounded_box("snes_port_recess", 40.0, 3.0, 18.0, 2.0, 3, dark,
                                center=(sx, -94.6, 15.0)))
        rim = ring_solid("snes_port_rim", rr_loop(42.0, 20.0, 1.0, 16),
                         rr_loop(34.0, 14.0, 2.0, 16), 0.0, 1.2, shell)
        bake_place(rim, (sx, -94.8, 15.0), rot_x=math.radians(90.0))
        objs.append(rim)
        for k in range(7):
            pins.append(cylinder("snes_pin", 0.9, 3.2, 16, metal,
                                 center=(sx - 12.0 + 4.0 * k, -95.0, 15.0),
                                 axis='Y'))
        t = text_mesh("snes_port_number", side, 5.0, 0.4, 0.08, dark,
                      resolution_u=4, bevel_resolution=1)
        bake_place(t, (sx + (-26.0 if sx < 0.0 else 26.0), -95.0, 15.0),
                   rot_x=math.radians(90.0))
        objs.append(t)
    objs.extend(pins)

    # side vents (six dark slots a side)
    for side in (-1, 1):
        for k in range(6):
            objs.append(rounded_box(
                "snes_vent", 0.9, 26.0, 3.0, 0.8, 3, dark,
                center=(side * 100.2, 32.0 - 1.0 * k, 30.0 + k * 5.0)))

    # embossed front wordmark
    word = text_mesh("snes_wordmark", "SUPER NINTENDO", 9.5, 0.8, 0.18, shell,
                     resolution_u=8, bevel_resolution=2)
    bake_place(word, (0.0, -95.35, 35.0), rot_x=math.radians(90.0))
    objs.append(word)
    small = text_mesh("snes_wordmark_sub", "ENTERTAINMENT SYSTEM", 3.4, 0.3, 0.05,
                      dark, resolution_u=4, bevel_resolution=1)
    bake_place(small, (0.0, -95.3, 27.0), rot_x=math.radians(90.0))
    objs.append(small)

    place(objs, location)
    world = Matrix.Translation(Vector(location))
    return {
        "objs": objs,
        "port_1": world @ Vector((-34.0, -95.0, 15.0)),
        "port_2": world @ Vector((34.0, -95.0, 15.0)),
    }


def build_snes_controller(mats, location, rot_z):
    objs = []
    shell, dark, accent = mats["shell"], mats["dark"], mats["accent"]
    lavender = mats["accent_light"]

    def top(x, y):
        r2 = (x / 76.0) ** 2 + (y / 52.5) ** 2
        return 25.0 + 4.5 * max(0.0, 1.0 - r2)

    objs.append(rounded_solid(
        "snes_controller", 152.0, 105.0, 32.0, top, 0.0, shell,
        top_edge=9.0, bottom_edge=3.0, seg=36, rings=28, edge_rings=7,
        bottom_rings=7))

    # D-pad
    objs.append(rounded_box("pad_h", 41.0, 14.0, 5.2, 2.4, 6, dark,
                            center=(-46.0, 0.0, top(-46.0, 0.0) + 1.4)))
    objs.append(rounded_box("pad_v", 14.0, 41.0, 5.24, 2.4, 6, dark,
                            center=(-46.0, 0.0, top(-46.0, 0.0) + 1.4)))

    # face buttons: A/B convex purple, X/Y lavender (shell) flatter
    zx = top(46.0, 0.0)
    for name, dx, dy, dz, mat in (("btn_x", 0.0, 13.5, 0.30, lavender),
                                  ("btn_y", -13.5, 0.0, 0.30, lavender),
                                  ("btn_a", 13.5, 0.0, 0.50, accent),
                                  ("btn_b", 0.0, -13.5, 0.50, accent)):
        objs.append(dome(name, 8.4, dz, 44, 22, mat,
                         center=(46.0 + dx, dy, zx - 1.2)))
    for dx, dy in ((0.0, 13.5), (-13.5, 0.0)):
        objs.append(cylinder("pad_dim", 2.4, 0.6, 24, dark,
                             center=(46.0 + dx, dy, zx + 1.5)))

    # Start / Select, angled like the real pair
    for sx, name, rot in ((-12.0, "SELECT", -18.0), (12.0, "START", 18.0)):
        b = rounded_box("ctrl_" + name.lower(), 17.0, 6.4, 3.0, 1.4, 4, dark)
        bake_place(b, (sx, 17.0, top(sx, 17.0) + 0.9), rot_z=math.radians(rot))
        objs.append(b)
        t = text_mesh("ctrl_" + name + "_label", name, 2.4, 0.18, 0.02, dark,
                      resolution_u=3, bevel_resolution=1)
        bake_place(t, (sx, 24.0, top(sx, 24.0) + 0.12), rot_z=math.radians(rot))
        objs.append(t)

    # shoulder buttons on the top rear edge
    for sx, name, rot in ((-55.0, "L", 10.0), (55.0, "R", -10.0)):
        b = rounded_box("ctrl_shoulder_" + name, 30.0, 10.0, 6.0, 3.2, 5, shell)
        bake_place(b, (sx, 49.0, 21.5), rot_z=math.radians(rot))
        objs.append(b)

    objs.append(ring_solid(
        "ctrl_seam", rr_loop(152.6, 105.6, 32.3, 20), rr_loop(151.2, 104.2, 31.6, 20),
        6.0, 7.2, dark))

    m = place(objs, location, rot_z)
    return {"objs": objs, "cable_anchor": m @ Vector((0.0, 49.0, 20.0))}


def build_cable(mats, start, end):
    dark, metal = mats["dark"], mats["metal"]
    pts = [
        start,
        Vector((-62.0, -281.0, 24.0)),
        Vector((-55.0, -269.0, 32.0)),
        Vector((-62.0, -262.0, 38.0)),
        Vector((-92.0, -261.0, 40.0)),
        Vector((-122.0, -263.0, 36.0)),
        Vector((-147.0, -264.0, 28.0)),
        Vector((-154.0, -262.0, 20.0)),
        Vector((-156.0, -261.0, 16.0)),
        end,
    ]
    path = spline(pts, 88)
    objs = [tube("snes_cable", path, 2.1, 14, dark)]
    objs.append(rounded_box("cable_strain", 11.0, 15.0, 10.0, 3.6, 5, dark,
                            center=tuple(start + Vector((0.0, 5.0, 0.5)))))
    objs.append(rounded_box("cable_plug", 19.0, 20.0, 11.0, 3.4, 5, dark,
                            center=tuple(end + Vector((0.0, -6.0, 0.0)))))
    for dy in (-2.0, 2.0):
        objs.append(rounded_box("cable_plug_rib", 15.0, 1.6, 7.0, 0.6, 2, metal,
                                center=tuple(end + Vector((0.0, -4.0 + dy, 0.0)))))
    return objs


def build(mats, stage=None):
    """Place the console, the shared stage, the controller and the cable.

    Returns the name of the diorama's root node.  ``stage`` is the callable the
    systems table selected for this system's stage kind (the CRT TV for a
    ``diorama``); it is invoked between the console and the controller so the
    exported node order is unchanged.
    """
    console = build_snes(mats, SNES_AT)
    if stage is not None:
        stage(mats)
    ctrl = build_snes_controller(mats, SNES_CTRL_AT, math.radians(SNES_CTRL_ROT))
    build_cable(mats, ctrl["cable_anchor"], console["port_2"])
    return ROOT
