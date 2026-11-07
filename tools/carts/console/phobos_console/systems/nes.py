# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The NES (NES-001 front loader) diorama: console, controller and cable.

The shared CRT TV is placed by the stage callable passed to :func:`build`, so
the console is authored here and nowhere else.  This module is the whole
registration: the registry in ``systems/__init__.py`` discovers it.  The shell
follows the 8 photos in ``refs/nes/``: a 256 x 203 x 76 mm slab with a warm
off-white upper housing, a grey/brown-grey lower band, a full-width cream
cartridge lid and the black front-right controller tower.  See
``tools/carts/console/reference/nes.md`` for the measured features and the
stylisations.
"""
import math

import bmesh
from mathutils import Matrix, Vector

from ..common import (bake_matrix, bake_place, cylinder, dome, loft_xz, new_obj,
                      place, ring_solid, rounded_box, rounded_solid, rr_loop,
                      spline, text_mesh, tube)
from ..palette import SCREEN, SCREEN_GLOW

ROOT = "nes_diorama"
OUTPUT = "android/app/src/main/assets/carts/consoles/nes.glb"
PREVIEW_PREFIX = "nes-"
PREVIEW_RES = (880, 660)
STAGE = "diorama"

# ---------------------------------------------------------------------------
# palette (linear RGB; the renders go through AgX) — the NES is its own authored
# one: warm off-white upper shell, a distinctly grey/brown-grey lower band,
# black trim, Nintendo red for the wordmark, POWER and the A/B buttons, and a
# red LED lens.
# ---------------------------------------------------------------------------
NES_SHELL = (0.540, 0.505, 0.425, 1.0)
NES_DARK = (0.012, 0.012, 0.014, 1.0)
NES_ACCENT = (0.330, 0.010, 0.008, 1.0)
NES_ACCENT_LIGHT = (0.240, 0.216, 0.192, 1.0)
NES_METAL = (0.450, 0.410, 0.300, 1.0)
NES_GLASS = (0.360, 0.012, 0.012, 1.0)

PALETTE = {
    "shell": (NES_SHELL, 0.50, 0.0, None, 0.0),
    "dark": (NES_DARK, 0.42, 0.0, None, 0.0),
    "accent": (NES_ACCENT, 0.36, 0.0, None, 0.0),
    "accent_light": (NES_ACCENT_LIGHT, 0.60, 0.0, None, 0.0),
    "metal": (NES_METAL, 0.30, 1.0, None, 0.0),
    "glass": (NES_GLASS, 0.06, 0.0, (0.55, 0.015, 0.012, 1.0), 2.2),
    "screen": (SCREEN, 0.06, 0.0, SCREEN_GLOW, 1.0),
}

#: The public builders of the old single-file generator, re-exported by
#: ``make_console`` so ``make_console.build_nes`` and friends keep working.
__all__ = ["build_nes", "build_nes_controller", "build_nes_cable", "nes_top"]

# The NES-001 slab: 256 wide, 203 deep, 76 high including the feet.  The seam
# between the cream upper housing and the grey lower band sits just below mid
# height (about 38 mm), and the front face is vertical with a small bottom
# chamfer rather than the old wedge.
NES_W, NES_D, NES_H = 256.0, 203.0, 76.0
NES_BASE = 4.0
NES_SEAM = 27.0
NES_AT = (-225.0, -168.0, 0.0)

NES_CTRL_W, NES_CTRL_D, NES_CTRL_H = 120.0, 56.0, 20.0
NES_CTRL_AT = (-140.0, -340.0, 0.0)
NES_CTRL_ROT = -8.0

# front-face details
FRONT_Y = -NES_D * 0.5
LID_X, LID_W = -35.5, 181.0
TOWER_X, TOWER_W = 75.5, 43.0
PORT_X = (64.5, 86.5)
PORT_Z = 23.0


def _face_text(name, body, size, extrude, bevel, mat, x, y, z, face,
               resolution_u=4, bevel_resolution=1):
    """A centred text mesh lying on one of the four vertical faces."""
    rights = {
        "front": (Vector((1.0, 0.0, 0.0)), Vector((0.0, 0.0, 1.0))),
        "rear": (Vector((-1.0, 0.0, 0.0)), Vector((0.0, 0.0, 1.0))),
        "left": (Vector((0.0, -1.0, 0.0)), Vector((0.0, 0.0, 1.0))),
        "right": (Vector((0.0, 1.0, 0.0)), Vector((0.0, 0.0, 1.0))),
    }
    right, up = rights[face]
    normal = right.cross(up)
    t = text_mesh(name, body, size, extrude, bevel, mat,
                  resolution_u=resolution_u,
                  bevel_resolution=bevel_resolution)
    rot = Matrix(((right.x, up.x, normal.x, 0.0),
                  (right.y, up.y, normal.y, 0.0),
                  (right.z, up.z, normal.z, 0.0),
                  (0.0, 0.0, 0.0, 1.0)))
    bake_matrix(t, Matrix.Translation(Vector((x, y, z))) @ rot)
    return t


def _left_text(name, body, size, extrude, bevel, mat, x0, y, z,
               resolution_u=8, bevel_resolution=1, scale_x=1.0):
    """A left-aligned text mesh on a -Y-facing face.

    ``text_mesh`` centres its text; this scales in X and shifts the converted
    mesh so its left edge sits at ``x0``.  Keeping the alignment here avoids a
    shared-helper edit for one console's printed wordmark.
    """
    t = text_mesh(name, body, size, extrude, bevel, mat,
                  resolution_u=resolution_u,
                  bevel_resolution=bevel_resolution)
    if scale_x != 1.0:
        bake_matrix(t, Matrix.Diagonal((scale_x, 1.0, 1.0, 1.0)))
    min_x = min(v.co.x for v in t.data.vertices)
    bake_matrix(t, Matrix.Translation(Vector((x0 - min_x, 0.0, 0.0))))
    bake_place(t, (0.0, y, z), rot_x=math.radians(90.0))
    return t


def nes_top(x, y):
    """A nearly flat lid with a gentle crown: 75.2 mm at the edges, 76 centre."""
    crown = math.cos(math.pi * x / NES_W) * math.cos(math.pi * y / NES_D) ** 2
    return 75.2 + 0.8 * max(0.0, crown)


def _stadium_side(name, y, z, w, h, x0, x1, mat):
    """An extruded vertical stadium (oval) plate on a side face.

    ``w`` is the depth (Y) and ``h`` the height (Z); the plate spans x0..x1.
    Used for the oval case vents, which are recessed ovals in the grey band.
    """
    pts = rr_loop(w, h, min(w, h) * 0.5 - 0.1, 24)
    bm = bmesh.new()
    loops = [[bm.verts.new((x, y + px, z + pz)) for px, pz in pts]
             for x in (x0, x1)]
    n = len(pts)
    for a, b in zip(loops, loops[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    for loop, x in ((loops[0], x0), (loops[-1], x1)):
        cy = sum(v.co.y for v in loop) / n
        cz = sum(v.co.z for v in loop) / n
        c = bm.verts.new((x, cy, cz))
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((loop[i], loop[j], c))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return new_obj(name, bm, mat, smooth=True)


def _port(mats, x, z, label):
    """One tall oval 7-pin controller socket in the black tower."""
    dark, metal, accent = mats["dark"], mats["metal"], mats["accent"]
    lower = mats["accent_light"]
    objs = []
    # dark socket plate, just proud of the tower face; loft_xz authors it
    # around x=0, so move it under its port
    socket = loft_xz(
        "nes_port_socket",
        [(-101.55, 16.6, 24.6, z, 7.9), (-101.15, 16.6, 24.6, z, 7.9)], dark)
    bake_matrix(socket, Matrix.Translation(Vector((x, 0.0, 0.0))))
    objs.append(socket)
    # flat oval rim, only 0.35 mm off the tower face: the bezel is the flat
    # black tower, not a cylinder around each socket
    rim = ring_solid(
        "nes_port_rim",
        rr_loop(19.6, 28.4, 8.4, 30),
        rr_loop(17.9, 26.7, 7.7, 30),
        0.0, 0.35, lower, bevel=0.15, bevel_seg=2)
    bake_place(rim, (x, -101.5, z), rot_x=math.radians(90.0))
    objs.append(rim)
    for dz, dxs in ((5.6, (-4.3, 0.0, 4.3)),
                    (-2.8, (-4.9, -1.6, 1.6, 4.9))):
        for dx in dxs:
            objs.append(cylinder("nes_port_pin", 0.85, 0.5, 14, metal,
                                 center=(x + dx, -101.55, z + dz), axis='Y'))
    num = text_mesh("nes_port_number_" + label, label, 4.4, 0.2, 0.03,
                    accent, resolution_u=4, bevel_resolution=1)
    bake_place(num, (x, -103.5, z + 17.8), rot_x=math.radians(90.0))
    objs.append(num)
    return objs


def build_nes(mats, location):
    objs = []
    shell, dark = mats["shell"], mats["dark"]
    accent, lower = mats["accent"], mats["accent_light"]
    metal, glass = mats["metal"], mats["glass"]

    # --- two-tone slab -----------------------------------------------------
    objs.append(rounded_solid(
        "nes_upper", NES_W, NES_D, 12.0, nes_top, NES_SEAM, shell,
        top_edge=5.0, bottom_edge=1.2, seg=96, rings=42, edge_rings=6,
        bottom_rings=5, bottom_edge_rings=2))
    objs.append(rounded_solid(
        "nes_lower", NES_W, NES_D, 12.0, lambda x, y: NES_SEAM + 0.35,
        NES_BASE, lower, top_edge=1.0, bottom_edge=9.0, seg=96, rings=18,
        edge_rings=3, bottom_rings=12, bottom_edge_rings=9))
    objs.append(ring_solid(
        "nes_seam", rr_loop(NES_W + 0.55, NES_D + 0.55, 12.0, 40),
        rr_loop(NES_W - 0.65, NES_D - 0.65, 11.5, 40),
        NES_SEAM - 0.45, NES_SEAM + 0.55, dark))

    # recessed rubber feet, mostly hidden under the lower band; the dark
    # notches at the bottom corners are the moulded cutouts around them
    for sx in (-1, 1):
        for sy in (-1, 1):
            objs.append(rounded_box("nes_foot", 22.0, 17.0, 3.2, 3.0, 4, dark,
                                    center=(sx * 104.0, sy * 73.0, 1.6)))
            objs.append(rounded_box(
                "nes_foot_cutout", 13.0, 1.0, 5.0, 1.6, 3, dark,
                center=(sx * 116.0, sy * (NES_D * 0.5 - 0.1), 5.6)))
            objs.append(rounded_box(
                "nes_foot_cutout_side", 1.0, 13.0, 5.0, 1.6, 3, dark,
                center=(sx * (NES_W * 0.5 - 0.1), sy * 96.0, 5.6)))

    # --- full-width flip cartridge lid ------------------------------------
    # The lid is the whole upper front panel, not a raised plaque: the only
    # geometry is a thin seam at its lower edge (the rest of the boundary is
    # the body edge on the left, the tower on the right, and the top hinge
    # line on the top surface).  The wordmark is printed straight onto it.
    objs.append(rounded_box("nes_lid_seam_bottom", LID_W, 0.3, 0.7, 0.2, 2,
                            dark, center=(LID_X, FRONT_Y - 0.02, 44.5)))
    objs.append(rounded_box("nes_lid_topseam", LID_W, 0.8, 0.25, 0.12, 2,
                            dark, center=(LID_X, FRONT_Y + 25.0, 75.45)))
    for lx in (LID_X - LID_W * 0.5, LID_X + LID_W * 0.5):
        objs.append(rounded_box("nes_lid_topside", 0.8, 25.0, 0.25, 0.12, 2,
                                dark, center=(lx, FRONT_Y + 12.5, 75.45)))
    objs.append(rounded_box("nes_lid_lip", 42.0, 2.8, 3.2, 1.8, 5, shell,
                            center=(-8.0, FRONT_Y - 0.85, 43.6)))
    objs.append(rounded_box("nes_lid_thumb", 30.0, 0.6, 2.4, 1.0, 4, lower,
                            center=(-8.0, FRONT_Y - 1.35, 44.7)))
    # red two-line wordmark printed on the lid, left aligned near its top;
    # the second line is letter-spaced capitals and noticeably smaller.
    word = _left_text("nes_wordmark", "Nintendo", 15.0, 0.16, 0.03, accent,
                      -112.0, FRONT_Y - 0.20, 67.0, resolution_u=12,
                      bevel_resolution=2, scale_x=1.10)
    objs.append(word)
    word_right = max(v.co.x for v in word.data.vertices)
    reg = text_mesh("nes_wordmark_reg", "®", 4.0, 0.08, 0.015, accent,
                    resolution_u=4, bevel_resolution=1)
    bake_place(reg, (word_right + 1.2, FRONT_Y - 0.20, 70.5),
               rot_x=math.radians(90.0))
    objs.append(reg)
    sub = _left_text(
        "nes_wordmark_sub", "E N T E R T A I N M E N T   S Y S T E M",
        4.6, 0.10, 0.015, accent, -112.0, FRONT_Y - 0.20, 55.2,
        resolution_u=6, bevel_resolution=1, scale_x=1.0)
    objs.append(sub)

    # --- black controller tower on the front right ------------------------
    objs.append(rounded_box("nes_port_tower", TOWER_W, 45.0, 72.0, 2.2, 5,
                            dark, center=(TOWER_X, FRONT_Y + 22.5, 40.0)))
    # the tower's upper blank panel reads as a slightly different black plate
    objs.append(rounded_box("nes_tower_panel", TOWER_W - 4.0, 1.2, 19.0, 1.8,
                            4, dark, center=(TOWER_X, FRONT_Y - 0.9, 59.0)))
    for label, px in zip(("1", "2"), PORT_X):
        objs.extend(_port(mats, px, PORT_Z, label))

    # --- top-right vent grille, front-to-back black caps ------------------
    objs.append(rounded_box("nes_grille_recess", TOWER_W - 4.0, 132.0, 1.4,
                            3.0, 4, lower, center=(TOWER_X, 7.0, 75.1)))
    for k in range(14):
        objs.append(rounded_box(
            "nes_grille_bar", TOWER_W - 6.0, 3.0, 1.5, 1.0, 3, shell,
            center=(TOWER_X, -46.0 + 8.9 * k, 75.75)))
    objs.append(rounded_box("nes_grille_rear", TOWER_W, 22.0, 3.6, 2.4, 4,
                            dark, center=(TOWER_X, 86.0, 75.35)))

    # --- lower front panel: LED, POWER and RESET --------------------------
    groove = ring_solid(
        "nes_btn_panel_groove",
        rr_loop(121.0, 20.0, 3.4, 20),
        rr_loop(119.4, 18.4, 3.0, 20), 0.0, 0.4, dark)
    bake_place(groove, (-67.0, FRONT_Y - 0.02, 17.5),
               rot_x=math.radians(90.0))
    objs.append(groove)
    objs.append(rounded_box("nes_led_bezel", 7.0, 0.4, 7.0, 1.4, 3, dark,
                            center=(-112.0, FRONT_Y - 0.02, 17.5)))
    objs.append(rounded_box("nes_led", 4.0, 0.3, 4.0, 0.6, 2, glass,
                            center=(-112.0, FRONT_Y - 0.12, 17.5)))
    for name, bx, bw, bh, label in (("power", -84.0, 31.0, 14.0, "POWER"),
                                    ("reset", -48.0, 25.0, 12.0, "RESET")):
        objs.append(rounded_box("nes_" + name + "_bezel", bw + 2.5, 0.4,
                                bh + 2.5, 1.0, 3, dark,
                                center=(bx, FRONT_Y - 0.02, 17.5)))
        objs.append(rounded_box("nes_" + name, bw, 0.3, bh, 1.0, 3, lower,
                                center=(bx, FRONT_Y - 0.12, 17.5)))
        t = text_mesh("nes_" + name + "_label", label, 4.3, 0.08, 0.01,
                      accent, resolution_u=6, bevel_resolution=1)
        bake_place(t, (bx, FRONT_Y - 0.35, 17.5), rot_x=math.radians(90.0))
        objs.append(t)

    # --- right-side AV panel and both rear oval vents ---------------------
    # The owner's brief puts the AUDIO/VIDEO outputs on the left side; the
    # gallery photos are ambiguous about which side carries them (its caption
    # calls the plain side the left one), so the side is also chosen to stay
    # visible in the 120-degree rear-left preview: the CRT stands to the
    # console's right and would hide it there.
    side_x = -NES_W * 0.5
    objs.append(rounded_box("nes_av_recess", 2.6, 60.0, 24.0, 3.0, 4, dark,
                            center=(side_x + 0.8, 38.0, 17.0)))
    objs.append(rounded_box("nes_av_panel", 1.6, 56.0, 20.0, 2.6, 4, lower,
                            center=(side_x - 0.2, 38.0, 17.0)))
    for name, y, mat, label in (("audio", 30.0, accent, "AUDIO"),
                                ("video", 46.0, metal, "VIDEO")):
        objs.append(cylinder("nes_rca_ring_" + name, 5.2, 1.2, 28, dark,
                             center=(side_x - 0.9, y, 18.0), axis='X'))
        objs.append(cylinder("nes_rca_" + name, 3.6, 3.2, 28, mat,
                             center=(side_x - 2.0, y, 18.0), axis='X',
                             bevel=0.5, bevel_seg=2))
        t = _face_text("nes_rca_label_" + name, label, 2.6, 0.08, 0.01,
                       accent, side_x - 1.2, y, 9.0, "left")
        objs.append(t)
    for side in (-1, 1):
        objs.append(_stadium_side(
            "nes_vent_rim", 82.0, 20.0, 16.0, 36.0,
            side * (NES_W * 0.5 + 0.2), side * (NES_W * 0.5 + 0.7), lower))
        objs.append(_stadium_side(
            "nes_vent", 82.0, 20.0, 11.0, 29.0,
            side * (NES_W * 0.5 + 0.6), side * (NES_W * 0.5 + 1.0), dark))

    # --- rear face: label recess, RF switch, antenna and power ------------
    objs.append(rounded_box("nes_rear_label_recess", 102.0, 1.0, 20.0, 3.0, 4,
                            dark, center=(-8.0, NES_D * 0.5 + 0.4, 16.5)))
    objs.append(rounded_box("nes_rear_label_plate", 96.0, 0.7, 16.0, 2.0, 4,
                            lower, center=(-8.0, NES_D * 0.5 + 0.75, 16.5)))
    objs.append(rounded_box("nes_rf_switch", 20.0, 1.8, 11.0, 1.6, 3, dark,
                            center=(46.0, NES_D * 0.5 + 1.0, 17.0)))
    objs.append(rounded_box("nes_rf_slider", 7.0, 1.2, 4.5, 0.8, 2, metal,
                            center=(46.0, NES_D * 0.5 + 2.0, 17.0)))
    objs.append(cylinder("nes_antenna_ring", 6.2, 1.4, 32, dark,
                         center=(74.0, NES_D * 0.5 + 1.0, 17.0), axis='Y'))
    objs.append(cylinder("nes_antenna", 4.6, 3.0, 32, metal,
                         center=(74.0, NES_D * 0.5 + 2.1, 17.0), axis='Y',
                         bevel=0.5, bevel_seg=2))
    objs.append(rounded_box("nes_power_jack", 15.0, 2.0, 11.0, 2.0, 3, dark,
                            center=(96.0, NES_D * 0.5 + 1.0, 17.0)))
    objs.append(cylinder("nes_power_pin", 2.2, 2.0, 20, metal,
                         center=(96.0, NES_D * 0.5 + 2.2, 17.0), axis='Y'))
    for label, rx, rz in (("POWER", 96.0, 9.0), ("CHANNEL", 46.0, 9.0),
                          ("ANTENNA", 74.0, 9.0)):
        t = _face_text("nes_rear_label_" + label, label, 2.5, 0.08, 0.01,
                       accent, rx, NES_D * 0.5 + 2.2, 8.0, "rear")
        objs.append(t)

    place(objs, location)
    world = Matrix.Translation(Vector(location))
    return {
        "objs": objs,
        "port_1": world @ Vector((PORT_X[0], -101.5, PORT_Z)),
        "port_2": world @ Vector((PORT_X[1], -101.5, PORT_Z)),
    }


def build_nes_controller(mats, location, rot_z):
    objs = []
    shell, dark, accent = mats["shell"], mats["dark"], mats["accent"]

    def top(x, y):
        r2 = (x / (NES_CTRL_W * 0.5)) ** 2 + (y / (NES_CTRL_D * 0.5)) ** 2
        return NES_CTRL_H - 1.2 + 1.3 * max(0.0, 1.0 - r2)

    objs.append(rounded_solid(
        "nes_controller", NES_CTRL_W, NES_CTRL_D, 10.0, top, 0.0, shell,
        top_edge=4.0, bottom_edge=2.0, seg=64, rings=32, edge_rings=6,
        bottom_rings=7, bottom_edge_rings=4))

    # dark-grey D-pad on the left
    z = top(-35.0, 0.0) + 1.0
    objs.append(rounded_box("nes_dpad_h", 26.0, 9.0, 4.2, 2.0, 5, dark,
                            center=(-35.0, 0.0, z)))
    objs.append(rounded_box("nes_dpad_v", 9.0, 26.0, 4.24, 2.0, 5, dark,
                            center=(-35.0, 0.0, z)))
    # Start / Select pills in the centre
    for name, sx in (("select", -9.0), ("start", 9.0)):
        objs.append(rounded_box("nes_" + name, 13.0, 5.6, 2.8, 2.0, 4, dark,
                                center=(sx, 13.0, top(sx, 13.0) + 0.8)))
        t = text_mesh("nes_" + name + "_label", name.upper(), 2.0, 0.08, 0.01,
                      accent, resolution_u=4, bevel_resolution=1)
        bake_place(t, (sx, 13.0, top(sx, 13.0) + 2.35))
        objs.append(t)
    # dark-red A/B, B lower-left and A upper-right
    for name, bx, by in (("B", 26.0, -8.0), ("A", 44.0, 6.0)):
        objs.append(dome("nes_btn_" + name.lower(), 5.8, 0.60, 44, 22, accent,
                         center=(bx, by, top(bx, by) - 0.6)))
        t = text_mesh("nes_btn_label_" + name, name, 2.8, 0.08, 0.01, accent,
                      resolution_u=4, bevel_resolution=1)
        bake_place(t, (bx, by - 9.0, top(bx, by - 9.0) + 0.15))
        objs.append(t)
    # body seam
    objs.append(ring_solid(
        "nes_ctrl_seam", rr_loop(NES_CTRL_W + 0.4, NES_CTRL_D + 0.4, 10.2, 24),
        rr_loop(NES_CTRL_W - 0.6, NES_CTRL_D - 0.6, 9.8, 24), 4.4, 5.2, dark))
    # black cable strain relief at the top centre
    objs.append(rounded_box("nes_ctrl_strain", 11.0, 13.0, 8.0, 2.5, 4, dark,
                            center=(0.0, 28.5, 15.5)))

    m = place(objs, location, rot_z)
    return {"objs": objs, "cable_anchor": m @ Vector((0.0, 29.5, 16.0))}


def build_nes_cable(mats, start, end):
    dark, metal = mats["dark"], mats["metal"]
    pts = [
        start,
        Vector((-152.0, -326.0, 13.0)),
        Vector((-172.0, -336.0, 8.0)),
        Vector((-196.0, -340.0, 6.0)),
        Vector((-216.0, -333.0, 6.0)),
        Vector((-228.0, -318.0, 7.0)),
        Vector((-230.0, -301.0, 9.0)),
        Vector((-222.0, -285.0, 11.0)),
        Vector((-208.0, -274.0, 13.0)),
        Vector((-192.0, -268.0, 14.0)),
        Vector((-176.0, -268.0, 15.0)),
        Vector((-162.0, -274.0, 16.0)),
        Vector((-152.0, -284.0, 17.0)),
        Vector((-146.0, -295.0, 18.0)),
        end,
    ]
    path = spline(pts, 104)
    objs = [tube("nes_cable", path, 2.4, 16, dark)]
    objs.append(rounded_box("nes_cable_strain", 13.0, 16.0, 12.0, 3.6, 5, dark,
                            center=tuple(start + Vector((0.0, 4.0, 0.5)))))
    # tall keyed connector in port 2
    objs.append(rounded_box("nes_cable_plug", 19.0, 13.0, 24.0, 4.0, 6, dark,
                            center=tuple(end + Vector((0.0, -6.5, 0.0)))))
    objs.append(rounded_box("nes_cable_grip", 21.0, 9.0, 20.0, 3.2, 5, dark,
                            center=tuple(end + Vector((0.0, -16.0, 0.0)))))
    for k in range(4):
        objs.append(rounded_box("nes_cable_rib", 22.0, 1.2, 1.6, 0.5, 2, metal,
                                center=tuple(end + Vector((0.0, -16.0,
                                                           -6.0 + 4.0 * k)))))
    return objs


def build(mats, stage=None):
    """Place the console, the shared stage, the controller and the cable.

    Returns the name of the diorama's root node.  The stage callable is invoked
    between the console and the controller so the exported node order is
    unchanged.
    """
    console = build_nes(mats, NES_AT)
    if stage is not None:
        stage(mats)
    ctrl = build_nes_controller(mats, NES_CTRL_AT, math.radians(NES_CTRL_ROT))
    build_nes_cable(mats, ctrl["cable_anchor"], console["port_2"])
    return ROOT
