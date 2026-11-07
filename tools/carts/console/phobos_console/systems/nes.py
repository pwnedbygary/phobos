# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The NES (NES-001 front loader) diorama: console, controller and cable.

The shared CRT TV is placed by the stage callable passed to :func:`build`, so
the console is authored here and nowhere else.  This module is the whole
registration: the registry in ``systems/__init__.py`` discovers it, so adding or
retuning a system never edits a shared file.
"""
import math

from mathutils import Matrix, Vector

from ..common import (bake_place, cylinder, dome, place, ring_solid,
                      rounded_box, rounded_solid, rr_loop, spline, text_mesh,
                      tube)
from ..palette import SCREEN, SCREEN_GLOW

ROOT = "nes_diorama"
OUTPUT = "android/app/src/main/assets/carts/consoles/nes.glb"
PREVIEW_PREFIX = "nes-"
PREVIEW_RES = (880, 660)
STAGE = "diorama"

# ---------------------------------------------------------------------------
# palette (linear RGB; the renders go through AgX) — the NES is its own authored
# one: warm light-grey upper shell, a darker warm-grey lower chassis, black trim,
# Nintendo red for the wordmark, POWER and the A/B buttons, a red LED lens
# ---------------------------------------------------------------------------
NES_SHELL = (0.560, 0.552, 0.522, 1.0)
NES_DARK = (0.014, 0.014, 0.017, 1.0)
NES_ACCENT = (0.430, 0.022, 0.018, 1.0)
NES_ACCENT_LIGHT = (0.240, 0.232, 0.216, 1.0)
NES_METAL = (0.42, 0.42, 0.43, 1.0)
NES_GLASS = (0.36, 0.012, 0.012, 1.0)

PALETTE = {
    "shell": (NES_SHELL, 0.44, 0.0, None, 0.0),
    "dark": (NES_DARK, 0.42, 0.0, None, 0.0),
    "accent": (NES_ACCENT, 0.36, 0.0, None, 0.0),
    "accent_light": (NES_ACCENT_LIGHT, 0.48, 0.0, None, 0.0),
    "metal": (NES_METAL, 0.30, 1.0, None, 0.0),
    "glass": (NES_GLASS, 0.06, 0.0, (0.55, 0.015, 0.012, 1.0), 2.2),
    "screen": (SCREEN, 0.06, 0.0, SCREEN_GLOW, 1.0),
}

#: The public builders of the old single-file generator, re-exported by
#: ``make_console`` so ``make_console.build_nes`` and friends keep working.
__all__ = ["build_nes", "build_nes_controller", "build_nes_cable", "nes_top"]

# ---------------------------------------------------------------------------
# NES (NES-001 front loader) + controller + cable
# ---------------------------------------------------------------------------
NES_W, NES_D = 256.0, 202.0
NES_H = 64.0
NES_AT = (-225.0, -168.0, 0.0)
NES_CTRL_AT = (-140.0, -340.0, 0.0)
NES_CTRL_ROT = -8.0


def nes_top(x, y):
    """The NES lid is almost flat: a gentle crown and a slight forward slope."""
    t = max(0.0, min(1.0, (y + NES_D * 0.5) / NES_D))
    s = t * t * (3.0 - 2.0 * t)
    h = NES_H - 2.4 + 2.4 * s
    h += 1.1 * math.cos(math.pi * x / NES_W) * math.cos(math.pi * y / NES_D) ** 2
    return h


def build_nes(mats, location):
    objs = []
    shell, dark = mats["shell"], mats["dark"]
    accent, lower = mats["accent"], mats["accent_light"]
    metal, glass = mats["metal"], mats["glass"]

    # --- two-tone body -----------------------------------------------------
    # Upper housing (light grey) over a slightly narrower lower chassis (darker
    # warm grey), with a dark seam; the NES is a low, wide slab, not a cube.
    objs.append(rounded_solid(
        "nes_body", NES_W, NES_D, 18.0, nes_top, 24.0, shell,
        top_edge=10.0, bottom_edge=2.4, seg=56, rings=46, edge_rings=9,
        bottom_rings=6, bottom_edge_rings=4))
    objs.append(rounded_solid(
        "nes_base", NES_W - 3.0, NES_D - 3.0, 17.5, lambda x, y: 25.6, 4.0, lower,
        top_edge=2.0, bottom_edge=4.0, seg=56, rings=12, edge_rings=4,
        bottom_rings=8, bottom_edge_rings=4))
    objs.append(ring_solid(
        "nes_seam", rr_loop(NES_W + 0.5, NES_D + 0.5, 18.2, 34),
        rr_loop(NES_W - 0.6, NES_D - 0.6, 18.0, 34), 23.8, 24.6, dark))
    for sx in (-1, 1):
        for sy in (-1, 1):
            objs.append(cylinder("nes_foot", 8.0, 4.0, 48, dark,
                                 center=(sx * 100.0, sy * 74.0, 2.0),
                                 bevel=1.3, bevel_seg=3))

    # --- front-loading flap ------------------------------------------------
    # A dark seam plate sits behind the light-grey door; the door stands a couple
    # of millimetres proud and tilts out at the bottom, like the real flap.
    objs.append(rounded_box("nes_flap_recess", 182.0, 1.6, 41.0, 3.0, 4, dark,
                            center=(-32.0, -101.3, 43.5)))
    flap = rounded_box("nes_flap", 176.0, 3.2, 35.0, 3.2, 6, shell)
    bake_place(flap, (-32.0, -101.8, 43.5), rot_x=math.radians(-2.0))
    objs.append(flap)
    objs.append(rounded_box("nes_flap_lip", 52.0, 3.0, 4.0, 1.6, 4, shell,
                            center=(-32.0, -103.4, 27.2)))
    word = text_mesh("nes_wordmark", "Nintendo", 12.0, 0.55, 0.14, accent,
                     resolution_u=8, bevel_resolution=2)
    bake_place(word, (-60.0, -103.45, 49.5), rot_x=math.radians(90.0))
    objs.append(word)
    sub = text_mesh("nes_wordmark_sub", "ENTERTAINMENT SYSTEM", 4.2, 0.3, 0.06,
                    accent, resolution_u=5, bevel_resolution=1)
    bake_place(sub, (-58.0, -103.45, 38.6), rot_x=math.radians(90.0))
    objs.append(sub)

    # --- front-right black band, ports and numerals ------------------------
    objs.append(rounded_box("nes_band_front", 66.0, 8.0, 60.0, 3.0, 6, dark,
                            center=(95.0, -99.0, 31.0)))
    objs.append(rounded_box("nes_band_side", 7.0, 44.0, 57.0, 3.0, 6, dark,
                            center=(126.0, -78.0, 31.0)))
    objs.append(rounded_box("nes_band_top", 66.0, 22.0, 6.0, 3.0, 6, dark,
                            center=(95.0, -92.0, 60.0)))
    for label, zc in (("1", 43.0), ("2", 16.0)):
        # The port is a dark recess behind a raised black rim: keep the plate
        # just proud of the band and the pins inside it, so they read as holes.
        objs.append(rounded_box("nes_port_recess", 23.0, 2.0, 26.0, 5.5, 4, dark,
                                center=(95.0, -102.6, zc)))
        rim = ring_solid("nes_port_rim", rr_loop(27.0, 30.0, 6.0, 18),
                         rr_loop(21.0, 24.0, 4.5, 18), 0.0, 1.6, dark, bevel=0.5,
                         bevel_seg=2)
        bake_place(rim, (95.0, -103.4, zc), rot_x=math.radians(90.0))
        objs.append(rim)
        for dx, dz in ((-3.6, 9.0), (3.6, 9.0), (-3.6, 3.0), (3.6, 3.0),
                       (-3.6, -3.0), (3.6, -3.0), (3.6, -9.0)):
            objs.append(cylinder("nes_port_pin", 0.55, 1.2, 12, metal,
                                 center=(95.0 + dx, -103.9, zc + dz), axis='Y'))
        num = text_mesh("nes_port_number", label, 4.2, 0.35, 0.07, accent,
                        resolution_u=4, bevel_resolution=1)
        bake_place(num, (80.5, -103.2, zc + 15.0), rot_x=math.radians(90.0))
        objs.append(num)

    # --- ribbed vent grille on the top right -------------------------------
    gx, gy = 82.0, 4.0
    objs.append(rounded_box("nes_grille_recess", 64.0, 148.0, 2.4, 5.0, 4, dark,
                            center=(gx, gy, 64.0)))
    for k in range(17):
        objs.append(rounded_box("nes_grille_bar", 52.0, 3.0, 1.8, 1.0, 3, shell,
                                center=(gx, gy - 64.0 + k * 8.0, 65.1)))
    for cy in (gy - 72.0, gy + 72.0):
        objs.append(rounded_box("nes_grille_cap", 64.0, 10.0, 3.0, 2.0, 4, dark,
                                center=(gx, cy, 64.7)))

    # --- lower front button panel -----------------------------------------
    objs.append(rounded_box("nes_btn_panel", 122.0, 2.4, 23.0, 4.0, 5, lower,
                            center=(-62.0, -100.4, 15.2)))
    objs.append(rounded_box("nes_power", 30.0, 3.0, 14.0, 1.8, 5, accent,
                            center=(-84.0, -101.9, 15.2)))
    objs.append(rounded_box("nes_reset", 24.0, 3.0, 14.0, 1.8, 5, shell,
                            center=(-48.0, -101.9, 15.2)))
    objs.append(cylinder("nes_led", 2.2, 2.0, 32, glass,
                         center=(-112.0, -101.8, 15.2), axis='Y'))
    for name, sx, mat, label in (("nes_power_label", -84.0, shell, "POWER"),
                                 ("nes_reset_label", -48.0, dark, "RESET")):
        t = text_mesh(name, label, 4.6, 0.2, 0.04, mat, resolution_u=4,
                      bevel_resolution=1)
        bake_place(t, (sx, -103.6, 15.2), rot_x=math.radians(90.0))
        objs.append(t)

    # --- right-side AV jacks, channel switch and rear vents -----------------
    for k, m in enumerate((metal, shell, accent)):
        objs.append(cylinder("nes_av_jack", 3.4, 3.0, 28, m,
                             center=(128.8, 58.0 + 9.0 * k, 25.0), axis='X',
                             bevel=0.5, bevel_seg=2))
    objs.append(rounded_box("nes_channel", 4.0, 16.0, 10.0, 1.5, 4, dark,
                            center=(128.2, 90.0, 25.0)))
    objs.append(rounded_box("nes_channel_switch", 2.4, 6.0, 7.0, 0.6, 2, metal,
                            center=(129.8, 88.0, 25.0)))
    for k in range(8):
        objs.append(rounded_box("nes_rear_vent", 4.0, 1.2, 24.0, 0.5, 2, dark,
                                center=(-84.0 + 10.0 * k, 101.2, 33.0)))

    place(objs, location)
    world = Matrix.Translation(Vector(location))
    return {
        "objs": objs,
        "port_1": world @ Vector((95.0, -103.0, 43.0)),
        "port_2": world @ Vector((95.0, -103.0, 16.0)),
    }


def build_nes_controller(mats, location, rot_z):
    objs = []
    shell, dark, accent = mats["shell"], mats["dark"], mats["accent"]

    def top(x, y):
        r2 = (x / 72.0) ** 2 + (y / 33.0) ** 2
        return 19.4 + 1.6 * max(0.0, 1.0 - r2)

    objs.append(rounded_solid(
        "nes_controller", 144.0, 66.0, 12.0, top, 0.0, shell,
        top_edge=4.5, bottom_edge=2.0, seg=48, rings=34, edge_rings=7,
        bottom_rings=6, bottom_edge_rings=4))

    # black face plate, proud of the grey body by about a millimetre
    objs.append(rounded_box("nes_ctrl_face", 120.0, 50.0, 5.0, 2.2, 6, dark,
                            center=(0.0, 0.0, 20.4)))
    # the two grey grip slots near the top edge
    for sx in (-24.0, 6.0):
        objs.append(rounded_box("nes_ctrl_slot", 27.0, 6.0, 1.2, 2.0, 3, shell,
                                center=(sx, 22.5, 23.2)))
    # D-pad cross: one bar plus two butting arms, so no coplanar faces fight.
    objs.append(rounded_box("nes_dpad_h", 36.0, 12.0, 5.0, 2.6, 5, dark,
                            center=(-41.0, 1.0, 24.0)))
    objs.append(rounded_box("nes_dpad_up", 12.0, 12.0, 5.0, 2.6, 5, dark,
                            center=(-41.0, 13.0, 24.0)))
    objs.append(rounded_box("nes_dpad_down", 12.0, 12.0, 5.0, 2.6, 5, dark,
                            center=(-41.0, -11.0, 24.0)))
    # A / B red buttons in a grey recess, B left and A right
    objs.append(rounded_box("nes_btn_recess", 46.0, 27.0, 1.6, 5.0, 5, shell,
                            center=(37.0, -9.0, 22.8)))
    for name, bx in (("B", 24.0), ("A", 49.0)):
        objs.append(dome("nes_btn_" + name.lower(), 8.6, 0.55, 40, 20, accent,
                         center=(bx, -9.0, 23.0)))
        t = text_mesh("nes_btn_label_" + name, name, 4.0, 0.2, 0.03, accent,
                      resolution_u=4, bevel_resolution=1)
        bake_place(t, (bx, -23.0, 23.8))
        objs.append(t)
    # Start / Select pills and their red labels
    for name, sx in (("SELECT", -13.0), ("START", 9.0)):
        objs.append(rounded_box("nes_" + name.lower(), 17.0, 7.0, 3.0, 2.0, 4,
                                dark, center=(sx, 9.0, 23.6)))
        t = text_mesh("nes_" + name + "_label", name, 2.8, 0.16, 0.02, accent,
                      resolution_u=4, bevel_resolution=1)
        bake_place(t, (sx, 17.0, 24.0))
        objs.append(t)
    # red Nintendo wordmark near the top-right
    word = text_mesh("nes_ctrl_wordmark", "Nintendo", 8.2, 0.25, 0.05, accent,
                     resolution_u=6, bevel_resolution=2)
    bake_place(word, (39.0, 17.0, 23.8))
    objs.append(word)
    # body seam
    objs.append(ring_solid(
        "nes_ctrl_seam", rr_loop(144.6, 66.6, 12.3, 26),
        rr_loop(143.2, 65.2, 11.8, 26), 7.0, 7.8, dark))

    m = place(objs, location, rot_z)
    return {"objs": objs, "cable_anchor": m @ Vector((19.0, 33.0, 18.0))}


def build_nes_cable(mats, start, end):
    dark = mats["dark"]
    pts = [
        start,
        Vector((-126.0, -326.0, 14.0)),
        Vector((-150.0, -334.0, 10.0)),
        Vector((-176.0, -336.0, 8.0)),
        Vector((-200.0, -330.0, 8.0)),
        Vector((-216.0, -318.0, 10.0)),
        Vector((-220.0, -303.0, 12.0)),
        Vector((-212.0, -288.0, 13.0)),
        Vector((-198.0, -274.0, 14.0)),
        Vector((-182.0, -261.0, 15.0)),
        Vector((-168.0, -268.0, 15.5)),
        Vector((-160.0, -280.0, 16.0)),
        Vector((-156.0, -291.0, 16.0)),
        end,
    ]
    path = spline(pts, 96)
    objs = [tube("nes_cable", path, 2.4, 16, dark)]
    objs.append(rounded_box("nes_cable_strain", 13.0, 16.0, 12.0, 3.6, 5, dark,
                            center=tuple(start + Vector((0.0, 4.0, 0.5)))))
    # The NES plug is a tall, slim keyed connector; ribs are black on black.
    objs.append(rounded_box("nes_cable_plug", 19.0, 13.0, 24.0, 4.0, 6, dark,
                            center=tuple(end + Vector((0.0, -6.5, 0.0)))))
    objs.append(rounded_box("nes_cable_grip", 21.0, 9.0, 20.0, 3.2, 5, dark,
                            center=tuple(end + Vector((0.0, -16.0, 0.0)))))
    for k in range(4):
        objs.append(rounded_box("nes_cable_rib", 22.0, 1.2, 1.6, 0.5, 2, dark,
                                center=tuple(end + Vector((0.0, -16.0, -6.0 + 4.0 * k)))))
    return objs


def build(mats, stage=None):
    """Place the console, the shared stage, the controller and the cable.

    Returns the name of the diorama's root node.  ``stage`` is the callable the
    systems table selected for this system's stage kind (the CRT TV for a
    ``diorama``); it is invoked between the console and the controller so the
    exported node order is unchanged.
    """
    console = build_nes(mats, NES_AT)
    if stage is not None:
        stage(mats)
    ctrl = build_nes_controller(mats, NES_CTRL_AT, math.radians(NES_CTRL_ROT))
    build_nes_cable(mats, ctrl["cable_anchor"], console["port_2"])
    return ROOT
