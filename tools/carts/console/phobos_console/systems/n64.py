# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Nintendo 64 (NUS-001) diorama: console, cartridge, controller and cable.

Reference-driven, from the photos and figures recorded in
``tools/carts/console/reference/n64.md``: a bulbous charcoal shell with a low
front deck and a high rear hump, the black front panel with the NINTENDO 64
wordmark and four-colour N logo (typed and extruded here, not a copied image),
four oval controller ports, the top cartridge slot with a light-grey flap and a
ridged cartridge, the POWER/RESET controls, the top vent grille and the
three-prong controller.

The shared CRT TV is placed by the stage callable passed to :func:`build`; this
module is the whole registration (the registry discovers it by its file name).
"""
import math

import bmesh
from mathutils import Matrix, Vector

from ..common import (bake_place, cylinder, dome, new_obj, place, ring_solid,
                      rounded_box, rounded_solid, rr_loop, spline, text_mesh, tube)
from ..palette import SCREEN, SCREEN_GLOW

ROOT = "n64_diorama"
OUTPUT = "android/app/src/main/assets/carts/consoles/n64.glb"
PREVIEW_PREFIX = "n64-"
PREVIEW_RES = (1000, 765)
STAGE = "diorama"

# ---------------------------------------------------------------------------
# palette (linear RGB; the renders go through AgX) — this console's own.
# The seven-material contract cannot carry the N64's two dark greys and four
# button colours separately: the console shell/panel share `dark`, and the N64
# button sequence maps to accent/accent_light/metal/glass.
# ---------------------------------------------------------------------------
SHELL = (0.430, 0.424, 0.398, 1.0)     # warm light-grey controller/cart plastic
DARK = (0.040, 0.039, 0.037, 1.0)      # warm charcoal console shell and CRT
ACCENT = (0.020, 0.070, 0.520, 1.0)    # N64 blue (A, logo leg)
ACCENT_LIGHT = (0.018, 0.300, 0.060, 1.0)  # N64 green (B, logo diagonal)
METAL = (0.550, 0.280, 0.020, 1.0)     # satin yellow (C cluster; brass TV badge)
GLASS = (0.380, 0.010, 0.008, 1.0)     # red lens (POWER LED, Start, logo segment)

PALETTE = {
    "shell": (SHELL, 0.50, 0.0, None, 0.0),
    "dark": (DARK, 0.45, 0.0, None, 0.0),
    "accent": (ACCENT, 0.30, 0.0, None, 0.0),
    "accent_light": (ACCENT_LIGHT, 0.30, 0.0, None, 0.0),
    "metal": (METAL, 0.38, 0.30, None, 0.0),
    "glass": (GLASS, 0.07, 0.0, (0.60, 0.020, 0.014, 1.0), 1.0),
    "screen": (SCREEN, 0.06, 0.0, SCREEN_GLOW, 1.0),
}

# ---------------------------------------------------------------------------
# local coordinates: width X, up Z, front -Y, feet on Z=0
# ---------------------------------------------------------------------------
N64_W, N64_D = 260.0, 190.0
N64_AT = (-225.0, -200.0, 0.0)
N64_CTRL_AT = (0.0, -330.0, 0.0)
N64_CTRL_ROT = -8.0

CTRL_W, CTRL_D, CTRL_H = 156.0, 119.0, 40.0


# ---------------------------------------------------------------------------
# console shell profile and helpers
# ---------------------------------------------------------------------------
def _clamp(v, lo=0.0, hi=1.0):
    return max(lo, min(hi, v))


def _smooth(t):
    t = _clamp(t)
    return t * t * (3.0 - 2.0 * t)


def n64_top(x, y):
    """The bulbous top: low front deck, high domed hump, falling rear deck."""
    s = _smooth((y + 90.0) / 120.0)
    h = 52.0 + 20.0 * s
    h += 4.0 * (1.0 - (x / 130.0) ** 2) * s
    h -= 7.0 * _smooth((y - 45.0) / 50.0)
    h += 0.3 * math.cos(math.pi * x / 260.0) * math.cos(math.pi * y / 190.0) ** 2
    return h


def _top_slope(x, y):
    return math.atan2(n64_top(x, y + 0.5) - n64_top(x, y - 0.5), 1.0)


def _ellipse_loop(rx, ry, seg):
    return [(rx * math.cos(2.0 * math.pi * i / seg),
             ry * math.sin(2.0 * math.pi * i / seg)) for i in range(seg)]


def _outline_prism(name, pts, z0, z1, mat, bevel=0.0, bevel_seg=3, smooth=True):
    """A closed prism over a top-view outline, optionally edge-bevelled.

    The controller body uses the coarse outline plus one bevel pass: it rounds
    the plan-view corners and the top/bottom rims at once, which is the shape the
    three-prong reference photo has.
    """
    bm = bmesh.new()
    top = [bm.verts.new((x, y, z1)) for x, y in pts]
    bot = [bm.verts.new((x, y, z0)) for x, y in pts]
    face_top = bm.faces.new(top)
    bm.faces.new(list(reversed(bot)))
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    if bevel > 0.0:
        bmesh.ops.bevel(
            bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
            offset=bevel, segments=bevel_seg, affect='EDGES', clamp_overlap=True)
    return new_obj(name, bm, mat, smooth=smooth)


def _scaled_outline(pts, s):
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    return [(cx + (x - cx) * s, cy + (y - cy) * s) for x, y in pts]


def _triangle(name, size, depth, mat, location, rot_z=0.0):
    bm = bmesh.new()
    pts = [(0.0, size), (-size * 0.9, -size * 0.55), (size * 0.9, -size * 0.55)]
    lower = [bm.verts.new((x, y, 0.0)) for x, y in pts]
    upper = [bm.verts.new((x, y, depth)) for x, y in pts]
    bm.faces.new(upper)
    bm.faces.new(list(reversed(lower)))
    for i in range(3):
        j = (i + 1) % 3
        bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    ob = new_obj(name, bm, mat, smooth=False)
    bake_place(ob, location, rot_z=rot_z)
    return ob


# ---------------------------------------------------------------------------
# front panel: wordmark, N64 logo, ports, POWER/RESET, memory cover
# ---------------------------------------------------------------------------
def _build_n64_logo(objs, mats, cx, cy, cz, scale=1.0):
    """The four-colour N, hand-built from four prisms (our reproduction)."""
    accent, green, yellow, red = (mats["accent"], mats["accent_light"],
                                  mats["metal"], mats["glass"])
    leg_w = 3.8 * scale
    leg_h = 15.0 * scale
    depth = 1.4
    # left leg (blue) and the two right-leg segments (yellow over red)
    objs.append(rounded_box("n64_logo_left", leg_w, depth, leg_h, 0.5, 2, accent,
                            center=(cx - 6.5 * scale, cy, cz)))
    objs.append(rounded_box("n64_logo_upper", leg_w, depth, leg_h * 0.52, 0.5, 2,
                            yellow, center=(cx + 6.5 * scale, cy, cz + leg_h * 0.24)))
    objs.append(rounded_box("n64_logo_lower", leg_w, depth, leg_h * 0.52, 0.5, 2,
                            red, center=(cx + 6.5 * scale, cy, cz - leg_h * 0.24)))
    diag = rounded_box("n64_logo_diag", leg_w, depth, 20.0 * scale, 0.5, 2, green)
    bake_place(diag, (cx, cy, cz), rot_y=math.radians(-48.0))
    objs.append(diag)


def _build_front(mats, objs):
    shell, dark = mats["shell"], mats["dark"]

    # Black control panel; its 18 mm height carries the two-line lockup.
    objs.append(rounded_box("n64_front_panel", 190.0, 4.0, 22.0, 4.5, 4, dark,
                            center=(0.0, -96.7, 33.0)))
    word = text_mesh("n64_wordmark", "NINTENDO 64", 6.8, 0.5, 0.12, shell,
                     resolution_u=8, bevel_resolution=2)
    bake_place(word, (0.0, -98.8, 40.5), rot_x=math.radians(90.0))
    objs.append(word)
    _build_n64_logo(objs, mats, 0.0, -98.7, 30.5, scale=0.95)

    # four oval controller ports, two either side of the wordmark
    for idx, px in enumerate((-78.0, -42.0, 42.0, 78.0)):
        objs.append(rounded_box("n64_port_rim", 30.0, 3.0, 14.0, 6.5, 7, shell,
                                center=(px, -96.4, 16.0)))
        objs.append(rounded_box("n64_port_recess", 22.0, 2.2, 9.0, 4.0, 4, dark,
                                center=(px, -97.5, 16.0)))
        for k in (-5.5, 0.0, 5.5):
            objs.append(cylinder("n64_port_pin", 2.0, 1.4, 20, dark,
                                 center=(px + k, -98.2, 16.0), axis='Y'))
        num = text_mesh("n64_port_number", str(idx + 1), 4.2, 0.3, 0.06, shell,
                        resolution_u=4, bevel_resolution=1)
        bake_place(num, (px, -96.8, 5.5), rot_x=math.radians(90.0))
        objs.append(num)

    # red POWER LED lens in a dark recess, below the panel on the left
    objs.append(rounded_box("n64_led_recess", 14.0, 1.5, 5.0, 2.2, 3, dark,
                            center=(-26.0, -96.2, 6.8)))
    objs.append(rounded_box("n64_led", 8.0, 1.3, 3.0, 1.3, 3, mats["glass"],
                            center=(-26.0, -96.8, 6.8)))


def _build_top(mats, objs):
    shell, dark = mats["shell"], mats["dark"]

    # POWER slider and RESET button in their recessed ovals.
    for name, px, well_w, well_d, cap_w, cap_d in (
            ("power", -84.0, 30.0, 18.0, 20.0, 9.0),
            ("reset", 84.0, 24.0, 16.0, 13.0, 9.0)):
        top = n64_top(px, -58.0)
        slope = _top_slope(px, -58.0)
        well = rounded_box("n64_" + name + "_well", well_w, well_d, 2.6, 6.0, 4, dark)
        bake_place(well, (px, -58.0, top + 0.7), rot_x=slope)
        objs.append(well)
        cap = rounded_box("n64_" + name + "_switch", cap_w, cap_d, 5.0, 2.2, 4, dark)
        bake_place(cap, (px, -58.0, top + 2.6), rot_x=slope)
        objs.append(cap)
        label = text_mesh("n64_" + name + "_label", name.upper(), 3.2, 0.35, 0.06,
                          shell, resolution_u=4, bevel_resolution=1)
        bake_place(label, (px, -68.5, n64_top(px, -68.5) + 0.2), rot_x=slope)
        objs.append(label)

    # memory-expansion cover and its little latch, on the front centre deck.
    mx, my = 0.0, -30.0
    mtop = n64_top(mx, my)
    mslope = _top_slope(mx, my)
    cover = rounded_box("n64_memory_cover", 56.0, 44.0, 2.2, 6.0, 4, dark)
    bake_place(cover, (mx, my, mtop + 0.6), rot_x=mslope)
    objs.append(cover)
    lid = rounded_box("n64_memory_lid", 26.0, 18.0, 1.4, 3.0, 3, dark)
    bake_place(lid, (mx, my - 6.0, mtop + 1.6), rot_x=mslope)
    objs.append(lid)
    latch = rounded_box("n64_memory_latch", 9.0, 5.0, 2.0, 1.2, 3, shell)
    bake_place(latch, (mx, my + 17.0, mtop + 1.7), rot_x=mslope)
    objs.append(latch)
    label = text_mesh("n64_memory_label", "MEMORY EXPANSION", 2.5, 0.3, 0.04, shell,
                      resolution_u=4, bevel_resolution=1)
    bake_place(label, (mx, my - 20.0, n64_top(mx, my - 20.0) + 1.5), rot_x=mslope)
    objs.append(label)

    # top vent grille: twenty angled fins on a recessed dark base plate.
    vy = -6.0
    vtop = n64_top(0.0, vy)
    vslope = _top_slope(0.0, vy)
    base = rounded_box("n64_vent_base", 148.0, 40.0, 1.6, 2.0, 3, dark)
    bake_place(base, (0.0, vy, vtop - 0.2), rot_x=vslope)
    objs.append(base)
    for i in range(20):
        vx = -70.0 + 7.35 * i
        z = n64_top(vx, vy) + 0.7
        fin = rounded_box("n64_vent_fin", 3.0, 29.0, 1.4, 0.35, 2, dark)
        bake_place(fin, (vx, vy, z), rot_x=vslope, rot_z=math.radians(9.0))
        objs.append(fin)

    # NINTENDO oval badge on the rear deck, behind the cartridge slot.
    bx, by = 52.0, 86.0
    btop = n64_top(bx, by)
    bslope = _top_slope(bx, by)
    badge = ring_solid("n64_badge_oval", _ellipse_loop(27.0, 8.0, 48),
                       _ellipse_loop(24.0, 5.6, 48), 0.0, 0.8, dark)
    bake_place(badge, (bx, by, btop + 0.15), rot_x=bslope)
    objs.append(badge)
    bword = text_mesh("n64_badge_word", "NINTENDO", 3.2, 0.3, 0.05, dark,
                      resolution_u=4, bevel_resolution=1)
    bake_place(bword, (bx, by, btop + 0.35), rot_x=bslope)
    objs.append(bword)

    # side vents (five slots a side, near the rear)
    for side in (-1, 1):
        for k in range(5):
            objs.append(rounded_box(
                "n64_side_vent", 0.9, 13.0, 3.2, 0.9, 3, dark,
                center=(side * 130.2, 42.0 + k * 9.0, 48.0)))

    # ---- cartridge slot, flap, cartridge ---------------------------------
    sy = 42.0
    stop = n64_top(0.0, sy)
    objs.append(ring_solid(
        "n64_slot_rim", rr_loop(162.0, 34.0, 10.0, 24),
        rr_loop(148.0, 22.0, 6.0, 24), stop - 1.0, stop + 2.2, dark))
    objs.append(rounded_box("n64_slot_liner", 146.0, 30.0, 1.4, 2.5, 3, dark,
                            center=(0.0, sy, stop + 0.6)))
    cart = rounded_box("n64_cart", 116.0, 18.5, 76.6, 5.0, 6, shell,
                       center=(0.0, sy, stop + 6.4))
    objs.append(cart)
    # label recess and printed NINTENDO mark on the exposed front face
    objs.append(rounded_box("n64_cart_label_border", 106.0, 1.0, 46.0, 3.0, 3, dark,
                            center=(0.0, sy - 9.5, stop + 22.0)))
    objs.append(rounded_box("n64_cart_label", 100.0, 1.2, 40.0, 3.0, 3, shell,
                            center=(0.0, sy - 10.1, stop + 22.0)))
    clabel = text_mesh("n64_cart_wordmark", "NINTENDO", 3.2, 0.3, 0.05, dark,
                       resolution_u=4, bevel_resolution=1)
    bake_place(clabel, (0.0, sy - 10.9, stop + 35.0), rot_x=math.radians(90.0))
    objs.append(clabel)
    _build_n64_logo(objs, mats, 0.0, sy - 10.9, stop + 25.0, scale=0.48)
    # moulded ridge and seam lines across the cartridge
    objs.append(rounded_box("n64_cart_ridge", 116.8, 19.3, 2.0, 0.9, 3, shell,
                            center=(0.0, sy, stop + 40.0)))
    objs.append(rounded_box("n64_cart_groove", 100.0, 0.5, 0.9, 0.3, 2, dark,
                            center=(0.0, sy - 9.4, stop + 37.2)))
    # the retracted flap edge behind the cartridge, with its hinge rod
    flap = rounded_box("n64_flap", 144.0, 8.0, 2.0, 1.6, 3, shell)
    bake_place(flap, (0.0, sy + 13.0, stop + 2.0), rot_x=math.radians(24.0))
    objs.append(flap)
    objs.append(cylinder("n64_flap_hinge", 1.6, 140.0, 20, dark,
                         center=(0.0, sy + 17.0, stop + 1.6), axis='X'))


def _build_rear_bottom(mats, objs):
    shell, dark, metal = mats["shell"], mats["dark"], mats["metal"]

    # rear panel: multi-out, power jack, vents, label, screws
    objs.append(rounded_box("n64_rear_panel", 220.0, 4.0, 48.0, 5.0, 4, dark,
                            center=(0.0, 96.6, 32.0)))
    objs.append(rounded_box("n64_multiout", 62.0, 3.0, 16.0, 3.0, 3, dark,
                            center=(-55.0, 98.7, 38.0)))
    objs.append(rounded_box("n64_multiout_pins", 52.0, 1.4, 3.0, 0.6, 2, metal,
                            center=(-55.0, 99.6, 38.0)))
    objs.append(cylinder("n64_dc_jack", 9.0, 6.0, 32, dark,
                         center=(58.0, 98.7, 38.0), axis='Y'))
    objs.append(cylinder("n64_dc_ring", 10.5, 1.4, 32, dark,
                         center=(58.0, 99.6, 38.0), axis='Y'))
    for k in range(6):
        objs.append(rounded_box("n64_rear_vent", 34.0, 1.2, 2.4, 0.7, 2, dark,
                                center=(0.0, 98.9, 16.0 + k * 5.4)))
    objs.append(rounded_box("n64_rear_label", 42.0, 0.8, 17.0, 1.5, 2, shell,
                            center=(-8.0, 98.9, 24.0)))
    for sx in (-1, 1):
        for sz in (12.0, 52.0):
            objs.append(cylinder("n64_rear_screw", 2.2, 1.0, 20, metal,
                                 center=(sx * 100.0, 98.8, sz), axis='Y'))

    # bottom: four recessed feet, vent slots and the extension-connector plate
    for sx in (-1, 1):
        for sy in (-1, 1):
            objs.append(cylinder("n64_foot", 14.0, 5.0, 40, dark,
                                 center=(sx * 100.0, sy * 65.0, 2.5),
                                 bevel=1.4, bevel_seg=3))
    objs.append(rounded_box("n64_bottom_panel", 180.0, 120.0, 1.0, 4.0, 3, dark,
                            center=(0.0, 0.0, 4.4)))
    for k in range(8):
        objs.append(rounded_box("n64_bottom_vent", 9.0, 40.0, 1.0, 0.6, 2, dark,
                                center=(-70.0 + k * 20.0, 36.0, 3.8)))
    objs.append(rounded_box("n64_ext_connector", 64.0, 46.0, 1.2, 3.0, 3, dark,
                            center=(0.0, -28.0, 3.9)))
    objs.append(rounded_box("n64_bottom_label", 44.0, 1.0, 26.0, 2.0, 2, shell,
                            center=(0.0, 10.0, 3.9)))


# ---------------------------------------------------------------------------
# console assembly
# ---------------------------------------------------------------------------
def build_console(mats, location):
    dark = mats["dark"]
    objs = []

    # two shells with a slight step at the moulded part line
    objs.append(rounded_solid(
        "n64_lower_shell", 256.0, 186.0, 35.0, lambda x, y: 20.0, 5.0, dark,
        top_edge=2.5, bottom_edge=5.0, seg=52, rings=10, edge_rings=3,
        bottom_rings=8))
    objs.append(rounded_solid(
        "n64_upper_shell", 260.0, 190.0, 35.0, n64_top, 18.0, dark,
        top_edge=7.0, bottom_edge=2.5, seg=54, rings=42, edge_rings=8,
        bottom_rings=8))

    _build_front(mats, objs)
    _build_top(mats, objs)
    _build_rear_bottom(mats, objs)

    place(objs, location)
    world = Matrix.Translation(Vector(location))
    return {
        "objs": objs,
        "ports": [world @ Vector((x, -95.0, 16.0)) for x in (-78.0, -42.0, 42.0, 78.0)],
    }


# ---------------------------------------------------------------------------
# three-prong controller
# ---------------------------------------------------------------------------
CTRL_OUTLINE = [
    (-58.0, 46.0), (58.0, 46.0),                    # top edge
    (70.0, 42.0), (76.0, 32.0), (78.0, 18.0), (78.0, 2.0), (76.0, -14.0),
    (72.0, -30.0), (66.0, -44.0), (58.0, -55.0), (48.0, -62.0), (40.0, -58.0),
    (44.0, -46.0), (43.0, -18.0),                   # right notch
    (19.0, -24.0), (18.0, -54.0), (14.0, -64.0), (7.0, -69.0), (0.0, -71.0),
    (-7.0, -69.0), (-14.0, -64.0), (-18.0, -54.0), (-19.0, -24.0),
    (-43.0, -18.0), (-44.0, -46.0),                 # left notch
    (-40.0, -58.0), (-48.0, -62.0), (-58.0, -55.0), (-66.0, -44.0),
    (-72.0, -30.0), (-76.0, -14.0), (-78.0, 2.0), (-78.0, 18.0),
    (-76.0, 32.0), (-70.0, 42.0),
]


def _controller_logo(mats, objs):
    # the moulded NINTENDO oval on the top face; our own typography, dark like
    # the recessed lettering on the grey controller
    dark = mats["dark"]
    ring = ring_solid("n64_ctrl_badge_oval", _ellipse_loop(19.0, 5.6, 40),
                      _ellipse_loop(17.0, 3.9, 40), CTRL_H + 0.05,
                      CTRL_H + 0.55, dark)
    objs.append(ring)
    word = text_mesh("n64_ctrl_wordmark", "NINTENDO", 3.0, 0.3, 0.05, dark,
                     resolution_u=5, bevel_resolution=1)
    bake_place(word, (0.0, 32.0, CTRL_H + 0.25))
    objs.append(word)


def build_controller(mats, location, rot_z):
    shell, dark, accent = mats["shell"], mats["dark"], mats["accent"]
    green, yellow, red = (mats["accent_light"], mats["metal"], mats["glass"])
    objs = []

    objs.append(_outline_prism("n64_controller", CTRL_OUTLINE, 0.0, CTRL_H,
                               shell, bevel=1.8, bevel_seg=3))
    # moulded part line: a 0.15 mm-proud dark band around the seam
    objs.append(_outline_prism("n64_ctrl_seam",
                               _scaled_outline(CTRL_OUTLINE, 1.004),
                               16.6, 17.4, dark, bevel=0.0, bevel_seg=1,
                               smooth=False))

    # D-pad well and cross
    objs.append(cylinder("n64_dpad_well", 21.0, 2.2, 64, dark,
                         center=(-48.0, 6.0, CTRL_H - 0.5)))
    objs.append(rounded_box("n64_dpad_h", 30.0, 10.0, 3.6, 2.2, 5, dark,
                            center=(-48.0, 6.0, CTRL_H + 1.4)))
    objs.append(rounded_box("n64_dpad_v", 10.0, 30.0, 3.62, 2.2, 5, dark,
                            center=(-48.0, 6.0, CTRL_H + 1.4)))
    objs.append(cylinder("n64_dpad_dimple", 3.0, 0.7, 24, dark,
                         center=(-48.0, 6.0, CTRL_H + 3.2)))

    # analog stick: dark well, grey shaft and ribbed cap
    objs.append(cylinder("n64_stick_well", 20.0, 2.4, 72, dark,
                         center=(0.0, -10.0, CTRL_H - 0.6)))
    objs.append(cylinder("n64_stick_base", 10.0, 6.0, 48, dark,
                         center=(0.0, -10.0, CTRL_H + 2.2)))
    objs.append(cylinder("n64_stick_shaft", 8.0, 8.0, 48, shell,
                         center=(0.0, -10.0, CTRL_H + 8.0)))
    objs.append(cylinder("n64_stick_cap", 11.0, 3.2, 48, shell,
                         center=(0.0, -10.0, CTRL_H + 13.0), bevel=1.2,
                         bevel_seg=3))
    for r in (8.0, 6.2, 4.4):
        objs.append(cylinder("n64_stick_ring", r, 0.35, 40, dark,
                             center=(0.0, -10.0, CTRL_H + 14.75)))

    # Start (red) and A/B/C face buttons
    def face_button(name, x, y, radius, height, mat, glyph):
        objs.append(cylinder(name + "_bezel", radius + 1.8, 1.0, 48, dark,
                             center=(x, y, CTRL_H + 0.2)))
        objs.append(dome(name, radius, height, 40, 18, mat,
                         center=(x, y, CTRL_H + 0.6)))
        if glyph:
            size = radius * 0.72 if len(glyph) == 1 else 2.5
            t = text_mesh(name + "_glyph", glyph, size, 0.22, 0.03,
                          shell if mat is not red else dark, resolution_u=3,
                          bevel_resolution=1)
            bake_place(t, (x, y, CTRL_H + 0.6 + radius * height + 0.12))
            objs.append(t)

    face_button("n64_start", -4.0, 24.0, 6.0, 0.52, red, "START")
    face_button("n64_b", 16.0, 4.0, 7.0, 0.52, green, "B")
    face_button("n64_a", 32.0, -6.0, 7.0, 0.52, accent, "A")
    c_positions = {
        "up": ((38.0, 32.0), 0.0),
        "left": ((26.0, 20.0), -90.0),
        "right": ((50.0, 20.0), 90.0),
        "down": ((38.0, 8.0), 180.0),
    }
    for name, ((x, y), rot) in c_positions.items():
        objs.append(cylinder("n64_c_" + name + "_bezel", 6.6, 0.9, 40, dark,
                             center=(x, y, CTRL_H + 0.2)))
        objs.append(dome("n64_c_" + name, 5.2, 0.48, 36, 16, yellow,
                         center=(x, y, CTRL_H + 0.6)))
        tri = _triangle("n64_c_" + name + "_glyph", 1.7, 0.25, dark,
                        (x, y, CTRL_H + 3.9), rot_z=math.radians(rot))
        objs.append(tri)

    _controller_logo(mats, objs)

    # shoulder buttons on the top-left/top-right edge
    for sx, name, rot in ((-56.0, "L", 12.0), (56.0, "R", -12.0)):
        objs.append(rounded_box("n64_shoulder_" + name, 28.0, 10.0, 5.0, 2.6, 5,
                                shell, center=(sx, 44.0, CTRL_H - 2.2)))
        # a dark shadow gap where it meets the shell
        objs.append(rounded_box("n64_shoulder_gap_" + name, 26.0, 2.0, 1.0, 0.5,
                                2, dark, center=(sx, 47.0, CTRL_H - 3.6)))

    # Z trigger under the centre prong (visible from the front/below)
    z = rounded_box("n64_z_trigger", 28.0, 20.0, 10.0, 5.0, 5, dark)
    bake_place(z, (0.0, -52.0, 7.0), rot_x=math.radians(-18.0))
    objs.append(z)

    m = place(objs, location, rot_z)
    return {"objs": objs, "cable_anchor": m @ Vector((0.0, 44.0, 27.0))}


def build_cable(mats, start, end):
    shell, dark = mats["shell"], mats["dark"]
    pts = [
        start,
        Vector((-20.0, -290.0, 32.0)),
        Vector((-70.0, -289.0, 30.0)),
        Vector((-130.0, -297.0, 28.0)),
        Vector((-200.0, -298.0, 25.0)),
        Vector((-260.0, -299.0, 22.0)),
        Vector((-290.0, -298.0, 19.0)),
        end + Vector((3.0, -2.0, 2.0)),
        end,
    ]
    path = spline(pts, 88)
    objs = [tube("n64_cable", path, 2.3, 14, dark)]
    objs.append(rounded_box("n64_cable_strain", 11.0, 13.0, 9.0, 3.0, 5, dark,
                            center=tuple(start + Vector((0.0, 4.0, 0.0)))))
    objs.append(rounded_box("n64_plug", 26.0, 20.0, 15.0, 4.0, 5, shell,
                            center=tuple(end + Vector((0.0, -4.0, 0.0)))))
    objs.append(rounded_box("n64_plug_grip", 18.0, 2.0, 8.0, 0.8, 2, dark,
                            center=tuple(end + Vector((0.0, -13.0, 0.0)))))
    for k in range(3):
        objs.append(cylinder("n64_plug_pin", 1.7, 1.0, 16, dark,
                             center=tuple(end + Vector((-6.0 + k * 6.0, 4.5, 0.0))),
                             axis='Y'))
    return objs


# ---------------------------------------------------------------------------
# entry point
# ---------------------------------------------------------------------------
def build(mats, stage):
    """Place the shared CRT, then the console, controller and cable."""
    if stage:
        # The N64's `metal` is the satin yellow of the C-button cluster, so the
        # shared CRT's trim (grille bars, knob caps, badge, vent fins) is given
        # the light-grey shell material instead: a silver/grey set, seven
        # materials in total.
        tv_mats = dict(mats)
        tv_mats["metal"] = mats["shell"]
        stage(tv_mats)
    console = build_console(mats, N64_AT)
    controller = build_controller(mats, N64_CTRL_AT, math.radians(N64_CTRL_ROT))
    build_cable(mats, controller["cable_anchor"], console["ports"][0])
    return ROOT
