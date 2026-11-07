# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Sega Mega Drive / Genesis Model 1 diorama: console, controller and cable.

The shared CRT stage is passed in by the registry; this module authors only the
Model 1 console, its cartridge, the three-button controller and their cable.
The palette is local to this module, as the registry expects.

Reference: tools/carts/console/reference/megadrive.md.  The shell is the
280 x 212 x 79 mm Model 1 wedge; the lettering is our own extruded type modelled
from the reference photos, not a copied image or logo file.
"""
import math

from mathutils import Matrix, Vector

from ..common import (bake_matrix, bake_place, cylinder, dome, place,
                      ring_solid, rounded_box, rounded_solid, rr_loop, spline,
                      text_mesh, tube)
from ..palette import SCREEN, SCREEN_GLOW

ROOT = "megadrive_diorama"
OUTPUT = "android/app/src/main/assets/carts/consoles/megadrive.glb"
PREVIEW_PREFIX = "megadrive-"
PREVIEW_RES = (1000, 765)
STAGE = "diorama"

# The console is charcoal ABS, not the SNES's light grey.  The roughness values
# are deliberately not one shared value: the body, the dark trim, the matte
# buttons and the satin metal badge catch the studio light differently.
SHELL = (0.022, 0.022, 0.025, 1.0)       # charcoal Model 1 shell / controller
DARK = (0.006, 0.006, 0.008, 1.0)        # black trim, recesses, cartridge
ACCENT = (0.420, 0.018, 0.014, 1.0)      # POWER red / A-B-C buttons
ACCENT_LIGHT = (0.020, 0.090, 0.360, 1.0)  # START / RESET blue
METAL = (0.78, 0.78, 0.79, 1.0)          # badge, pins, jack rings
GLASS = (0.18, 0.012, 0.010, 1.0)        # power LED lens

PALETTE = {
    "shell": (SHELL, 0.46, 0.0, None, 0.0),
    "dark": (DARK, 0.42, 0.0, None, 0.0),
    "accent": (ACCENT, 0.32, 0.0, None, 0.0),
    "accent_light": (ACCENT_LIGHT, 0.34, 0.0, None, 0.0),
    "metal": (METAL, 0.30, 0.35, None, 0.0),
    "glass": (GLASS, 0.06, 0.0, (0.55, 0.012, 0.010, 1.0), 2.2),
    "screen": (SCREEN, 0.06, 0.0, SCREEN_GLOW, 1.0),
}

__all__ = ["build", "build_megadrive", "build_controller", "build_cable",
           "md_top", "MD_W", "MD_D"]

# ---------------------------------------------------------------------------
# Model 1 / controller placement
# ---------------------------------------------------------------------------
MD_W, MD_D = 280.0, 212.0
MD_BASE = 2.0
MD_AT = (-220.0, -160.0, 0.0)
CTRL_AT = (-68.0, -342.0, 0.0)
CTRL_ROT = -8.0
# (front is -Y, rear is +Y; the cartridge slot is at the rear of the top)

PORT_X = 88.0
PORT_Z = 28.0
PORT_Y = -MD_D * 0.5 - 1.0


def md_top(x, y):
    """Model 1's wedge: low at the front, high at the rear, gently crowned."""
    t = max(0.0, min(1.0, (y + MD_D * 0.5) / MD_D))
    s = t * t * (3.0 - 2.0 * t)
    h = 55.5 + 21.0 * s
    h += 0.9 * math.cos(math.pi * x / MD_W) * math.cos(math.pi * y / MD_D) ** 2
    return h


def _oval(w, d, n=64):
    """A closed oval loop in XY, counter-clockwise from +X."""
    return [(0.5 * w * math.cos(2.0 * math.pi * i / n),
             0.5 * d * math.sin(2.0 * math.pi * i / n)) for i in range(n)]


def _text(objs, name, body, size, extrude, bevel, mat, at, rot_x=0.0,
          rot_y=0.0, rot_z=0.0, resolution_u=6):
    t = text_mesh(name, body, size, extrude, bevel, mat,
                  resolution_u=resolution_u, bevel_resolution=2)
    bake_place(t, at, rot_x=rot_x, rot_y=rot_y, rot_z=rot_z)
    objs.append(t)
    return t


# ---------------------------------------------------------------------------
# the console
# ---------------------------------------------------------------------------
def build_megadrive(mats, location):
    objs = []
    shell, dark = mats["shell"], mats["dark"]
    accent, blue = mats["accent"], mats["accent_light"]
    metal, glass = mats["metal"], mats["glass"]

    # The wedge is one closed rounded solid; its top height field is the Model
    # 1's sloping lid.  The front skirt is lower than the rear, as the photos.
    objs.append(rounded_solid(
        "md_body", MD_W, MD_D, 30.0, md_top, MD_BASE, shell,
        top_edge=5.5, bottom_edge=3.0, seg=56, rings=48, edge_rings=8,
        bottom_rings=8, bottom_edge_rings=4))

    # A fine dark seam / side shadow line around the plastic at belt height.
    objs.append(ring_solid(
        "md_seam", rr_loop(MD_W + 0.6, MD_D + 0.6, 31.0, 30),
        rr_loop(MD_W - 0.7, MD_D - 0.7, 30.6, 30), 33.0, 34.0, dark))

    # Feet: four inset rubber pads, visible as the console floats.
    for sx in (-1, 1):
        for sy in (-1, 1):
            objs.append(cylinder("md_foot", 9.0, 3.4, 40, dark,
                                 center=(sx * 112.0, sy * 78.0, 1.5),
                                 bevel=0.9, bevel_seg=2))

    # --- the large oval cartridge door on the top --------------------------
    lid_y = 34.0
    lid_z = md_top(0.0, lid_y)
    objs.append(ring_solid(
        "md_cart_lid_rim", _oval(238.0, 164.0), _oval(218.0, 144.0),
        lid_z - 0.4, lid_z + 2.2, shell, bevel=0.8, bevel_seg=2))
    objs.append(ring_solid(
        "md_cart_lid_inner", _oval(218.0, 144.0), _oval(206.0, 132.0),
        lid_z + 0.2, lid_z + 1.2, dark, bevel=0.35, bevel_seg=2))
    # The Model 1's raised oval lid carries the SEGA / 16-BIT wordmark.  These
    # are our own extruded letterforms, modelled from the reference photos; no
    # logo image or font file is copied into the asset.
    badge_z = lid_z + 2.8
    objs.append(ring_solid(
        "md_lid_badge_rim", _oval(184.0, 108.0), _oval(176.0, 100.0),
        badge_z, badge_z + 1.0, metal, bevel=0.35, bevel_seg=2))
    _text(objs, "md_lid_sega", "SEGA", 21.0, 0.60, 0.18, metal,
          (0.0, -25.0, badge_z + 0.8))
    _text(objs, "md_lid_16bit", "16-BIT", 13.0, 0.45, 0.13, metal,
          (0.0, 6.0, badge_z + 0.8))

    # The rear-top cartridge slot and its dark throat.  The cartridge is a
    # separate inserted slab with moulded ribs and a printed label area.
    slot_y = 69.0
    slot_z = md_top(0.0, slot_y)
    objs.append(rounded_box("md_cart_slot", 154.0, 33.0, 3.2, 3.0, 4, dark,
                            center=(0.0, slot_y, slot_z - 1.1)))
    objs.append(rounded_box("md_cart_slot_lip", 164.0, 37.0, 2.4, 5.0, 4, shell,
                            center=(0.0, slot_y, slot_z - 1.8)))
    for k in range(9):
        objs.append(rounded_box("md_slot_rib", 1.2, 24.0, 1.6, 0.5, 2, metal,
                                center=(-64.0 + 16.0 * k, slot_y, slot_z + 0.8)))

    # Cartridge, local bottom at z=0: body, label recess, 16-BIT label, ribs.
    cart = []
    cart.append(rounded_box("md_cartridge", 152.0, 23.0, 96.0, 4.5, 6, dark,
                            center=(0.0, 0.0, 47.0)))
    cart.append(rounded_box("md_cart_label_recess", 132.0, 1.2, 64.0, 3.0, 4,
                            shell, center=(0.0, -11.9, 49.0)))
    cart.append(rounded_box("md_cart_label", 124.0, 0.8, 56.0, 2.0, 3, dark,
                            center=(0.0, -12.8, 49.0)))
    cart.append(rounded_box("md_cart_label_stripe", 110.0, 0.5, 7.0, 1.0, 2,
                            accent, center=(0.0, -13.3, 27.0)))
    _text(cart, "md_cart_16bit", "16-BIT", 15.0, 0.45, 0.16, metal,
          (0.0, -13.35, 54.0), rot_x=math.radians(90.0))
    _text(cart, "md_cart_sega", "SEGA", 9.0, 0.35, 0.12, metal,
          (0.0, -13.35, 33.0), rot_x=math.radians(90.0))
    # moulded ribs down the back and a pair of edge rails
    for k in range(8):
        cart.append(rounded_box("md_cart_back_rib", 144.0, 1.4, 2.0, 0.7, 2,
                                shell, center=(0.0, 11.1, 16.0 + k * 10.0)))
    for sx in (-1, 1):
        cart.append(rounded_box("md_cart_edge", 2.0, 8.0, 70.0, 0.7, 2, shell,
                                center=(sx * 72.0, 5.0, 45.0)))
    m = Matrix.Translation(Vector((0.0, slot_y + 4.0, slot_z - 2.0))) \
        @ Matrix.Rotation(math.radians(-7.0), 4, 'X')
    for ob in cart:
        bake_matrix(ob, m)
    objs.extend(cart)

    # --- top vent grille, left of the cartridge door ----------------------
    for k in range(13):
        objs.append(rounded_box("md_top_vent", 3.0, 96.0, 2.0, 1.0, 2, dark,
                                center=(-126.0 + k * 6.2, -38.0,
                                        md_top(-126.0 + k * 6.2, -38.0) + 0.8)))
    for sx in (-1, 1):
        for k in range(8):
            objs.append(rounded_box("md_top_vent_side", 42.0, 2.4, 1.8, 0.8, 2,
                                    dark, center=(sx * 119.0, -70.0 + k * 8.0,
                                                  md_top(sx * 119.0, -70.0 + k * 8.0) + 0.7)))
        for k in range(7):
            objs.append(rounded_box("md_side_vent", 1.6, 30.0, 2.2, 0.9, 2, dark,
                                    center=(sx * 141.0, -58.0 + k * 8.0, 36.0)))

    # --- front control panel: POWER, RESET, volume -------------------------
    front_y = -MD_D * 0.5 - 0.6
    objs.append(rounded_box("md_front_panel", 154.0, 2.0, 44.0, 4.0, 4, dark,
                            center=(-83.0, front_y - 0.8, 27.0)))
    # POWER: recessed track and a red slider knob
    objs.append(rounded_box("md_power_track", 17.0, 1.2, 28.0, 2.0, 3, dark,
                            center=(-139.0, front_y - 1.8, 29.0)))
    objs.append(rounded_box("md_power_slider", 11.0, 4.2, 14.0, 2.2, 4, accent,
                            center=(-139.0, front_y - 3.4, 35.0)))
    _text(objs, "md_power_label", "POWER", 5.2, 0.25, 0.06, metal,
          (-139.0, front_y - 2.9, 15.0), rot_x=math.radians(90.0))
    # RESET: blue raised button with a satin ring
    objs.append(ring_solid("md_reset_ring", _oval(22.0, 14.0, 40),
                           _oval(18.0, 10.5, 40), 0.0, 1.0, metal))
    bake_matrix(objs[-1], Matrix.Translation(Vector((-102.0, front_y - 2.2, 28.0)))
                @ Matrix.Rotation(math.radians(90.0), 4, 'X'))
    objs.append(rounded_box("md_reset", 16.0, 3.0, 10.0, 3.0, 4, blue,
                            center=(-102.0, front_y - 3.6, 28.0)))
    _text(objs, "md_reset_label", "RESET", 5.2, 0.25, 0.06, metal,
          (-102.0, front_y - 2.9, 15.0), rot_x=math.radians(90.0))
    # volume slider and headphone jack, as on the Model 1 front
    objs.append(rounded_box("md_volume_track", 13.0, 1.2, 25.0, 2.0, 3, dark,
                            center=(-62.0, front_y - 1.8, 29.0)))
    objs.append(rounded_box("md_volume_slider", 8.0, 4.0, 11.0, 1.8, 4, shell,
                            center=(-62.0, front_y - 3.2, 31.0)))
    _text(objs, "md_volume_label", "VOLUME", 4.4, 0.2, 0.05, metal,
          (-62.0, front_y - 2.9, 15.0), rot_x=math.radians(90.0))
    objs.append(cylinder("md_phones", 5.4, 2.4, 32, metal,
                         center=(-31.0, front_y - 1.2, 28.0), axis='Y',
                         bevel=0.6, bevel_seg=2))
    objs.append(cylinder("md_phones_hole", 3.0, 2.6, 24, dark,
                         center=(-31.0, front_y - 1.4, 28.0), axis='Y'))
    objs.append(cylinder("md_led", 2.7, 1.8, 32, glass,
                         center=(-18.0, front_y - 1.8, 28.0), axis='Y'))
    _text(objs, "md_led_label", "POWER", 3.8, 0.16, 0.04, metal,
          (-18.0, front_y - 2.9, 18.0), rot_x=math.radians(90.0))

    # --- front-right controller port recess and two DB9 sockets -------------
    objs.append(rounded_box("md_port_recess", 106.0, 3.0, 43.0, 4.0, 5, dark,
                            center=(84.0, front_y - 1.2, 26.0)))
    for side, sx in (("1", 61.0), ("2", 107.0)):
        objs.append(rounded_box("md_port_bezel", 36.0, 2.0, 24.0, 5.0, 4, metal,
                                center=(sx, front_y - 3.0, 27.0)))
        objs.append(rounded_box("md_port_well", 29.0, 2.8, 17.0, 4.0, 4, dark,
                                center=(sx, front_y - 3.7, 27.0)))
        # two staggered rows of five/four pins in the DB-9 arrangement
        for row, count in ((0, 5), (1, 4)):
            for k in range(count):
                objs.append(cylinder("md_port_pin", 0.75, 2.0, 12, metal,
                                     center=(sx - 9.10 + 4.55 * k,
                                             front_y - 4.5,
                                             31.0 - row * 5.0),
                                     axis='Y'))
        _text(objs, "md_port_number_" + side, side, 6.0, 0.3, 0.07, metal,
              (sx + (-24.0 if sx < 80 else 24.0), front_y - 3.8, 18.0),
              rot_x=math.radians(90.0))
    _text(objs, "md_port_label", "CONTROLLER", 3.2, 0.18, 0.05, metal,
          (84.0, front_y - 2.8, 43.0), rot_x=math.radians(90.0))

    # --- front wordmarks / 16-BIT badge -----------------------------------
    _text(objs, "md_front_mega", "MEGA DRIVE", 10.0, 0.55, 0.14, metal,
          (-37.0, front_y - 2.1, 47.0), rot_x=math.radians(90.0))
    _text(objs, "md_front_sega", "SEGA", 7.0, 0.4, 0.10, metal,
          (57.0, front_y - 2.1, 47.0), rot_x=math.radians(90.0))
    # the raised lid wordmark above carries the 16-BIT badge

    # --- rear panel: jacks, channel switch and vents ----------------------
    rear_y = MD_D * 0.5 + 0.3
    objs.append(rounded_box("md_rear_panel", 124.0, 2.0, 34.0, 3.5, 4, dark,
                            center=(52.0, rear_y, 34.0)))
    for name, x, z in (("AV OUT", 35.0, 43.0), ("RF OUT", 70.0, 43.0)):
        objs.append(cylinder("md_rear_jack", 5.2, 3.2, 28, metal,
                             center=(x, rear_y + 1.4, z), axis='Y',
                             bevel=0.6, bevel_seg=2))
        _text(objs, "md_rear_" + name.replace(" ", "_"), name, 3.2, 0.16, 0.04,
              metal, (x, rear_y + 1.9, z - 10.0), rot_x=math.radians(90.0))
    objs.append(rounded_box("md_channel_switch", 26.0, 3.2, 9.0, 1.5, 3, metal,
                            center=(106.0, rear_y + 1.2, 35.0)))
    objs.append(rounded_box("md_channel_slot", 12.0, 1.0, 5.0, 0.8, 2, dark,
                            center=(106.0, rear_y + 2.9, 35.0)))
    for k in range(11):
        objs.append(rounded_box("md_rear_vent", 4.0, 1.2, 21.0, 0.7, 2, dark,
                                center=(-122.0 + 7.2 * k, rear_y + 0.5, 32.0)))
    _text(objs, "md_rear_16bit", "16-BIT", 5.2, 0.2, 0.05, metal,
          (-96.0, rear_y + 1.2, 35.0), rot_x=math.radians(90.0))

    place(objs, location)
    world = Matrix.Translation(Vector(location))
    return {
        "objs": objs,
        "port_1": world @ Vector((PORT_X - 27.0, PORT_Y - 4.5, PORT_Z)),
        "port_2": world @ Vector((PORT_X + 19.0, PORT_Y - 4.5, PORT_Z)),
    }


# ---------------------------------------------------------------------------
# the three-button controller
# ---------------------------------------------------------------------------
def build_controller(mats, location, rot_z):
    objs = []
    shell, dark = mats["shell"], mats["dark"]
    accent, blue, metal = mats["accent"], mats["accent_light"], mats["metal"]

    def top(x, y):
        # low wings and a slightly higher centre; the real pad is a soft wedge.
        r2 = (x / 78.0) ** 2 + (y / 49.0) ** 2
        return 24.0 + 2.8 * max(0.0, 1.0 - r2) + 0.8 * max(0.0, 1.0 - y / 48.0)

    objs.append(rounded_solid(
        "md_controller", 154.0, 96.0, 35.0, top, 0.0, shell,
        top_edge=7.0, bottom_edge=2.0, seg=46, rings=34, edge_rings=7,
        bottom_rings=7, bottom_edge_rings=4))
    # dark face inset around the controls
    objs.append(rounded_box("md_ctrl_face", 124.0, 66.0, 2.0, 8.0, 5, dark,
                            center=(0.0, 0.0, 25.4)))
    # SEGA wordmark and the controller's long descriptive line
    _text(objs, "md_ctrl_sega", "SEGA", 13.0, 0.40, 0.11, metal,
          (-2.0, 8.0, 28.1))
    _text(objs, "md_ctrl_line", "COMPUTER VIDEO GAME CONTROL PAD", 3.3, 0.15,
          0.04, metal, (0.0, 34.0, 27.8))
    # D-pad: a shallow circular boss with a ring and four arrow marks, then
    # the cross itself.
    ring = ring_solid("md_dpad_ring", _oval(45.0, 45.0, 48),
                      _oval(39.0, 39.0, 48), 0.0, 1.1, dark)
    bake_place(ring, (-44.0, -4.0, 27.1))
    objs.append(ring)
    for k in range(4):
        a = math.radians(90.0 * k)
        tri = rounded_box("md_dpad_arrow", 3.2, 2.0, 0.8, 0.35, 2, metal,
                          center=(-44.0 + 21.0 * math.cos(a),
                                  -4.0 + 21.0 * math.sin(a), 28.4))
        bake_place(tri, (0.0, 0.0, 0.0), rot_z=a + math.radians(45.0))
        objs.append(tri)
    for name, w, d in (("md_dpad_h", 33.0, 11.5), ("md_dpad_v", 11.5, 33.0)):
        objs.append(rounded_box(name, w, d, 4.4, 2.2, 5, dark,
                                center=(-44.0, -4.0, 28.0)))
    for sx, sy in ((-1, 1), (-1, -1), (1, -1), (1, 1)):
        objs.append(cylinder("md_dpad_dot", 1.0, 0.5, 12, metal,
                             center=(-44.0 + sx * 22.0, -4.0 + sy * 12.0, 30.4)))
    # Start: the blue pill at top centre
    objs.append(rounded_box("md_start", 20.0, 7.0, 3.8, 2.8, 5, blue,
                            center=(2.0, 20.0, 28.8)))
    _text(objs, "md_start_label", "START", 3.2, 0.16, 0.04, metal,
          (2.0, 31.0, 27.6))
    # A, B, C: three red domes in the diagonal row, in a black trigger recess
    objs.append(rounded_box("md_btn_panel", 92.0, 55.0, 1.8, 8.0, 5, dark,
                            center=(51.0, -3.0, 26.0)))
    for name, bx, by in (("A", 38.0, -18.0), ("B", 52.0, -3.0), ("C", 66.0, 12.0)):
        objs.append(rounded_box("md_btn_recess", 19.0, 18.0, 2.2, 4.0, 4, dark,
                                center=(bx, by, 27.4)))
        objs.append(dome("md_btn_" + name.lower(), 7.4, 0.52, 34, 18, accent,
                         center=(bx, by, 27.6)))
        _text(objs, "md_btn_label_" + name, name, 3.8, 0.16, 0.04, metal,
              (bx, by - 12.0, 28.1))
    _text(objs, "md_trigger_label", "TRIGGER", 4.0, 0.18, 0.045, metal,
          (55.0, 24.0, 28.0))
    # body seam and cable strain relief at the top
    objs.append(ring_solid(
        "md_ctrl_seam", rr_loop(155.0, 97.0, 35.4, 28),
        rr_loop(153.6, 95.6, 34.8, 28), 9.0, 10.0, dark))
    m = place(objs, location, rot_z)
    return {"objs": objs,
            "cable_anchor": m @ Vector((0.0, 48.0, 23.0))}


def build_cable(mats, start, end):
    dark, metal = mats["dark"], mats["metal"]
    # A relaxed loop in front of the console, then into port 1.
    pts = [
        start,
        Vector((-56.0, -310.0, 18.0)),
        Vector((-36.0, -324.0, 14.0)),
        Vector((-12.0, -328.0, 12.0)),
        Vector((14.0, -322.0, 12.0)),
        Vector((34.0, -306.0, 14.0)),
        Vector((42.0, -287.0, 16.0)),
        Vector((40.0, -276.0, 18.0)),
        end,
    ]
    path = spline(pts, 84)
    objs = [tube("md_cable", path, 2.2, 14, dark)]
    objs.append(rounded_box("md_cable_strain", 12.0, 15.0, 11.0, 3.2, 5, dark,
                            center=tuple(start + Vector((0.0, 3.0, 0.0)))))
    plug = rounded_box("md_cable_plug", 32.0, 22.0, 22.0, 4.5, 5, dark,
                      center=tuple(end + Vector((0.0, -9.0, 0.0))))
    objs.append(plug)
    objs.append(rounded_box("md_cable_grip", 38.0, 15.0, 18.0, 4.0, 4, dark,
                            center=tuple(end + Vector((0.0, -21.0, 0.0)))))
    for k in range(4):
        objs.append(rounded_box("md_cable_rib", 33.0, 1.3, 1.6, 0.5, 2, metal,
                                center=tuple(end + Vector((0.0, -21.0,
                                                           -6.0 + 4.0 * k)))))
    _text(objs, "md_plug_sega", "SEGA", 5.0, 0.25, 0.06, metal,
          tuple(end + Vector((0.0, -9.0, 12.0))))
    return objs


def build(mats, stage=None):
    """Place the console, the shared CRT, the controller and the cable."""
    console = build_megadrive(mats, MD_AT)
    if stage is not None:
        stage(mats)
    ctrl = build_controller(mats, CTRL_AT, math.radians(CTRL_ROT))
    build_cable(mats, ctrl["cable_anchor"], console["port_1"])
    return ROOT
