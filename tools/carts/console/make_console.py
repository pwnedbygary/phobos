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
identity transform under the single `snes_diorama` root node.  Nothing is
randomised and Blender's bundled font supplies the embossed lettering: the same
Blender build rebuilds the GLB byte for byte.
"""
import argparse
import hashlib
import math
import os
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

# ---------------------------------------------------------------------------
# palette (linear RGB; the renders go through AgX)
# ---------------------------------------------------------------------------
SHELL = (0.50, 0.49, 0.455, 1.0)      # warm light grey SNES plastic
DARK = (0.022, 0.022, 0.026, 1.0)     # charcoal CRT cabinet and dark plastic
ACCENT = (0.16, 0.10, 0.44, 1.0)      # SNES purple (switches, A/B)
METAL = (0.44, 0.44, 0.45, 1.0)       # satin metal (pins, knob caps, badge)
ACCENT_LIGHT = (0.34, 0.31, 0.47, 1.0)  # SNES lavender X/Y buttons
GLASS = (0.02, 0.10, 0.05, 1.0)       # green LED lens
SCREEN = (0.006, 0.008, 0.014, 1.0)   # CRT face; the app stamps art over it

# The NES palette is its own authored one (the shells are not tintable): warm
# light-grey upper shell, a darker warm-grey lower chassis, black trim, Nintendo
# red for the wordmark, POWER and the A/B buttons, and a red LED lens.
NES_SHELL = (0.560, 0.552, 0.522, 1.0)
NES_DARK = (0.014, 0.014, 0.017, 1.0)
NES_ACCENT = (0.430, 0.022, 0.018, 1.0)
NES_ACCENT_LIGHT = (0.240, 0.232, 0.216, 1.0)
NES_METAL = (0.42, 0.42, 0.43, 1.0)
NES_GLASS = (0.36, 0.012, 0.012, 1.0)
NES_SCREEN = (0.006, 0.008, 0.014, 1.0)

SNES_PALETTE = {
    "shell": (SHELL, 0.42, 0.0, None, 0.0),
    "dark": (DARK, 0.44, 0.0, None, 0.0),
    "accent": (ACCENT, 0.34, 0.0, None, 0.0),
    "accent_light": (ACCENT_LIGHT, 0.38, 0.0, None, 0.0),
    "metal": (METAL, 0.28, 1.0, None, 0.0),
    "glass": (GLASS, 0.06, 0.0, (0.02, 0.35, 0.08, 1.0), 2.0),
    "screen": (SCREEN, 0.06, 0.0, (0.015, 0.03, 0.07, 1.0), 1.0),
}

NES_PALETTE = {
    "shell": (NES_SHELL, 0.44, 0.0, None, 0.0),
    "dark": (NES_DARK, 0.42, 0.0, None, 0.0),
    "accent": (NES_ACCENT, 0.36, 0.0, None, 0.0),
    "accent_light": (NES_ACCENT_LIGHT, 0.48, 0.0, None, 0.0),
    "metal": (NES_METAL, 0.30, 1.0, None, 0.0),
    "glass": (NES_GLASS, 0.06, 0.0, (0.55, 0.015, 0.012, 1.0), 2.2),
    "screen": (NES_SCREEN, 0.06, 0.0, (0.015, 0.03, 0.07, 1.0), 1.0),
}


# ---------------------------------------------------------------------------
# scene and materials
# ---------------------------------------------------------------------------
def clear_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat)
    for img in list(bpy.data.images):
        if img.users == 0:
            bpy.data.images.remove(img)
    for cu in list(bpy.data.curves):
        bpy.data.curves.remove(cu)


def make_material(name, color, roughness, metallic=0.0, emission=None,
                  emission_strength=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = emission
        bsdf.inputs["Emission Strength"].default_value = emission_strength
    mat.diffuse_color = color
    # Every part is a closed solid except the screen sheet, so backface culling is
    # safe and the glTF export can mark the materials single-sided.
    mat.use_backface_culling = True
    return mat


def make_materials(palette):
    mats = {}
    for name in ("shell", "dark", "accent", "accent_light", "metal", "glass", "screen"):
        color, roughness, metallic, emission, strength = palette[name]
        mats[name] = make_material(name, color, roughness, metallic=metallic,
                                   emission=emission, emission_strength=strength)
    return mats


# ---------------------------------------------------------------------------
# geometry helpers.  Every helper bakes its placement into the mesh; objects
# live at identity so the export is a flat, predictable node tree.
# ---------------------------------------------------------------------------
def new_obj(name, bm, mat, smooth=True):
    for f in bm.faces:
        f.smooth = smooth
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if mat is not None:
        ob.data.materials.append(mat)
    return ob


def bake_matrix(ob, matrix):
    ob.data.transform(matrix)
    return ob


def bake_place(ob, location, rot_x=0.0, rot_y=0.0, rot_z=0.0):
    m = Matrix.Translation(Vector(location)) @ Matrix.Rotation(rot_z, 4, 'Z') \
        @ Matrix.Rotation(rot_y, 4, 'Y') @ Matrix.Rotation(rot_x, 4, 'X')
    return bake_matrix(ob, m)


def place(objs, location, rot_z=0.0):
    m = Matrix.Translation(Vector(location)) @ Matrix.Rotation(rot_z, 4, 'Z')
    for ob in objs:
        bake_matrix(ob, m)
    return m


def rr_loop(w, dep, r, seg):
    """A rounded-rectangle loop in XY, CCW seen from +Z, `seg` points per corner."""
    r = max(0.05, min(r, w * 0.5 - 0.08, dep * 0.5 - 0.08))
    half_w, half_d = w * 0.5, dep * 0.5
    corners = [
        (half_w - r, half_d - r, 0.0),
        (-half_w + r, half_d - r, 90.0),
        (-half_w + r, -half_d + r, 180.0),
        (half_w - r, -half_d + r, 270.0),
    ]
    pts = []
    for cx, cy, a0 in corners:
        for i in range(seg):
            a = math.radians(a0 + 90.0 * i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def round_drop(d, radius):
    """How far a rounded edge of `radius` falls at inward distance `d`."""
    if radius <= 0.0 or d >= radius:
        return 0.0
    if d < 0.0:
        d = 0.0
    return radius - math.sqrt(max(0.0, radius * radius - (radius - d) ** 2))


def _ring_distances(dmax, edge, edge_rings, total):
    dists = [edge * i / edge_rings for i in range(edge_rings)]
    remaining = max(1, total - edge_rings)
    for i in range(1, remaining + 1):
        dists.append(edge + (dmax - edge) * i / remaining)
    return dists[:total]


def rounded_solid(name, w, dep, r, top_fn, bottom_z, mat,
                  top_edge=8.0, bottom_edge=3.0, seg=160, rings=30,
                  edge_rings=6, bottom_rings=7, bottom_edge_rings=4):
    """A closed rounded-rect solid whose top is a height field.

    Concentric rounded rectangles fill the footprint; the top surface follows
    `top_fn(x, y)`, falls away over `top_edge` at the rim so the top rounds into
    the vertical side, and the bottom mirrors that over `bottom_edge`.  This is
    how the SNES shell gets its curved top and rounded corners without booleans.
    """
    bm = bmesh.new()
    dmax = min(w, dep) * 0.5 * 0.965

    top_loops = []
    for d in _ring_distances(dmax, top_edge, edge_rings, rings):
        ww = max(w - 2.0 * d, 0.2)
        dd = max(dep - 2.0 * d, 0.2)
        drop = round_drop(d, top_edge)
        loop = [bm.verts.new((x, y, top_fn(x, y) - drop))
                for x, y in rr_loop(ww, dd, r - d, seg)]
        top_loops.append(loop)

    n = len(top_loops[0])
    for k in range(len(top_loops) - 1):
        a, b = top_loops[k], top_loops[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))

    cx = sum(v.co.x for v in top_loops[-1]) / n
    cy = sum(v.co.y for v in top_loops[-1]) / n
    cz = sum(v.co.z for v in top_loops[-1]) / n
    cap_top = bm.verts.new((cx, cy, cz))
    for i in range(n):
        bm.faces.new((top_loops[-1][i], top_loops[-1][(i + 1) % n], cap_top))

    bottom_loops = []
    for d in _ring_distances(dmax, bottom_edge, bottom_edge_rings, bottom_rings):
        ww = max(w - 2.0 * d, 0.2)
        dd = max(dep - 2.0 * d, 0.2)
        z = bottom_z + round_drop(d, bottom_edge)
        bottom_loops.append(
            [bm.verts.new((x, y, z)) for x, y in rr_loop(ww, dd, r - d, seg)])

    for k in range(len(bottom_loops) - 1):
        a, b = bottom_loops[k], bottom_loops[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))

    cx = sum(v.co.x for v in bottom_loops[-1]) / n
    cy = sum(v.co.y for v in bottom_loops[-1]) / n
    cap_bot = bm.verts.new((cx, cy, bottom_z))
    for i in range(n):
        bm.faces.new((bottom_loops[-1][i], bottom_loops[-1][(i + 1) % n], cap_bot))

    outer_top, outer_bot = top_loops[0], bottom_loops[0]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((outer_top[i], outer_top[j], outer_bot[j], outer_bot[i]))

    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return new_obj(name, bm, mat, smooth=True)


def rounded_box(name, w, dep, h, bevel, segments, mat, center=(0.0, 0.0, 0.0),
                smooth=True):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= w
        v.co.y *= dep
        v.co.z *= h
    bmesh.ops.bevel(
        bm,
        geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
        offset=bevel,
        segments=segments,
        affect='EDGES',
        clamp_overlap=True,
    )
    bmesh.ops.translate(bm, verts=list(bm.verts), vec=Vector(center))
    return new_obj(name, bm, mat, smooth=smooth)


def cylinder(name, radius, depth, seg, mat, center=(0.0, 0.0, 0.0),
             axis='Z', bevel=0.0, bevel_seg=3, smooth=True):
    bm = bmesh.new()
    bmesh.ops.create_cone(
        bm, cap_ends=True, cap_tris=False, segments=seg,
        radius1=radius, radius2=radius, depth=depth)
    if bevel > 0.0:
        rim = [e for e in bm.edges
               if len(e.link_faces) == 2 and any(len(f.verts) > 4 for f in e.link_faces)]
        bmesh.ops.bevel(
            bm, geom=list(bm.verts) + rim, offset=bevel, segments=bevel_seg,
            affect='EDGES', clamp_overlap=True)
    if axis == 'Y':
        bmesh.ops.rotate(bm, verts=list(bm.verts), cent=(0.0, 0.0, 0.0),
                         matrix=Matrix.Rotation(math.pi * 0.5, 3, 'X'))
    elif axis == 'X':
        bmesh.ops.rotate(bm, verts=list(bm.verts), cent=(0.0, 0.0, 0.0),
                         matrix=Matrix.Rotation(math.pi * 0.5, 3, 'Y'))
    bmesh.ops.translate(bm, verts=list(bm.verts), vec=Vector(center))
    return new_obj(name, bm, mat, smooth=smooth)


def loft_xz(name, sections, mat, smooth=True):
    """A lofted solid along Y; each section is (y, w, h, z_center, radius)."""
    bm = bmesh.new()
    loops = []
    for y, w, h, zc, r in sections:
        loops.append([bm.verts.new((x, y, zc + z)) for x, z in rr_loop(w, h, r, 20)])
    n = len(loops[0])
    for k in range(len(loops) - 1):
        a, b = loops[k], loops[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    for loop, y in ((loops[0], sections[0][0]), (loops[-1], sections[-1][0])):
        cx = sum(v.co.x for v in loop) / n
        cz = sum(v.co.z for v in loop) / n
        c = bm.verts.new((cx, y, cz))
        for i in range(n):
            bm.faces.new((loop[i], loop[(i + 1) % n], c))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return new_obj(name, bm, mat, smooth=smooth)


def loft_smooth(name, controls, samples, seg, mat, bottom_z=0.0, smooth=True):
    """A smooth loft along Y from (y, width, height, corner) control sections.

    The bottom stays on `bottom_z` while the height tapers, which is the CRT
    cabinet's side profile: flat base, rounded top falling away toward the tube.
    """
    dense = spline([Vector((c[0], c[1], c[2], c[3])) for c in controls], samples)
    bm = bmesh.new()
    loops = []
    for v in dense:
        y, w, h, r = v.x, v.y, v.z, v.w
        r = max(0.2, min(r, w * 0.5 - 0.1, h * 0.5 - 0.1))
        zc = bottom_z + h * 0.5
        loops.append([bm.verts.new((x, y, zc + z)) for x, z in rr_loop(w, h, r, seg)])
    n = len(loops[0])
    for k in range(len(loops) - 1):
        a, b = loops[k], loops[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    for loop, y in ((loops[0], dense[0].x), (loops[-1], dense[-1].x)):
        cx = sum(v.co.x for v in loop) / n
        cz = sum(v.co.z for v in loop) / n
        c = bm.verts.new((cx, y, cz))
        for i in range(n):
            bm.faces.new((loop[i], loop[(i + 1) % n], c))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return new_obj(name, bm, mat, smooth=smooth)


def ring_solid(name, outer_loop, inner_loop, z0, z1, mat, bevel=0.0,
               bevel_seg=3, smooth=True):
    """A picture-frame solid from two matching XY loops, extruded from z0 to z1."""
    bm = bmesh.new()
    n = len(outer_loop)
    assert n == len(inner_loop)
    ob0 = [bm.verts.new((x, y, z0)) for x, y in outer_loop]
    ob1 = [bm.verts.new((x, y, z1)) for x, y in outer_loop]
    ib0 = [bm.verts.new((x, y, z0)) for x, y in inner_loop]
    ib1 = [bm.verts.new((x, y, z1)) for x, y in inner_loop]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((ob1[i], ob1[j], ib1[j], ib1[i]))   # front face
        bm.faces.new((ob0[i], ob0[j], ib0[j], ib0[i]))   # back face
        bm.faces.new((ob0[i], ob0[j], ob1[j], ob1[i]))   # outer wall
        bm.faces.new((ib0[i], ib0[j], ib1[j], ib1[i]))   # inner wall
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    if bevel > 0.0:
        bmesh.ops.bevel(
            bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
            offset=bevel, segments=bevel_seg, affect='EDGES', clamp_overlap=True)
    return new_obj(name, bm, mat, smooth=smooth)


def dome(name, radius, zscale, seg, rings, mat, center, smooth=True):
    """A deterministic flattened hemisphere (top pole down to the equator).

    `bmesh.ops.create_uvsphere` orders its faces through a hash in this Blender
    build, which reorders the glTF index buffer from run to run; this hand-built
    dome keeps the mesh, and therefore the GLB, byte-identical.
    """
    bm = bmesh.new()
    top = bm.verts.new((0.0, 0.0, radius * zscale))
    loops = []
    for i in range(1, rings + 1):
        theta = (i / rings) * (math.pi * 0.5)
        z = math.cos(theta) * radius * zscale
        r = math.sin(theta) * radius
        loops.append([bm.verts.new((r * math.cos(2.0 * math.pi * k / seg),
                                    r * math.sin(2.0 * math.pi * k / seg), z))
                      for k in range(seg)])
    first = loops[0]
    for k in range(seg):
        j = (k + 1) % seg
        bm.faces.new((top, first[k], first[j]))
    for i in range(len(loops) - 1):
        a, b = loops[i], loops[i + 1]
        for k in range(seg):
            j = (k + 1) % seg
            bm.faces.new((a[k], b[k], b[j], a[j]))
    cap = bm.verts.new((0.0, 0.0, 0.0))
    last = loops[-1]
    for k in range(seg):
        j = (k + 1) % seg
        bm.faces.new((last[j], last[k], cap))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bmesh.ops.translate(bm, verts=list(bm.verts), vec=Vector(center))
    return new_obj(name, bm, mat, smooth=smooth)


def text_mesh(name, body, size, extrude, bevel, mat, resolution_u=6,
              bevel_resolution=3, smooth=False):
    curve = bpy.data.curves.new(name, type='FONT')
    curve.body = body
    curve.size = size
    curve.extrude = extrude
    curve.bevel_depth = bevel
    curve.bevel_resolution = bevel_resolution
    curve.resolution_u = resolution_u
    curve.align_x = 'CENTER'
    curve.align_y = 'CENTER'
    ob = bpy.data.objects.new(name, curve)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.convert(target='MESH')
    ob = bpy.context.view_layer.objects.active
    ob.data.materials.clear()
    ob.data.materials.append(mat)
    for f in ob.data.polygons:
        f.use_smooth = smooth
    # Blender's text conversion adds a font UV layer.  The diorama's only UV
    # target is the TV screen, so strip it here: the GLB then carries TEXCOORD_0
    # on tv_screen alone and the text meshes stop paying for unused UV seams.
    while ob.data.uv_layers:
        ob.data.uv_layers.remove(ob.data.uv_layers[0])
    ob.select_set(False)
    return ob


def tube(name, points, radius, seg, mat, smooth=True):
    """A round tube through world-space points."""
    bm = bmesh.new()
    loops = []
    up_ref = Vector((0.0, 0.0, 1.0))
    for i, p in enumerate(points):
        if i == 0:
            t = (points[1] - points[0])
        elif i == len(points) - 1:
            t = (points[-1] - points[-2])
        else:
            t = (points[i + 1] - points[i - 1])
        t.normalize()
        side = t.cross(up_ref)
        if side.length < 1e-4:
            side = t.cross(Vector((0.0, 1.0, 0.0)))
        side.normalize()
        up = side.cross(t).normalized()
        loop = [bm.verts.new(p + radius * (math.cos(a) * side + math.sin(a) * up))
                for a in (2.0 * math.pi * k / seg for k in range(seg))]
        loops.append(loop)
    for k in range(len(loops) - 1):
        a, b = loops[k], loops[k + 1]
        for i in range(seg):
            j = (i + 1) % seg
            bm.faces.new((a[i], a[j], b[j], b[i]))
    for loop, p, flip in ((loops[0], points[0], True), (loops[-1], points[-1], False)):
        c = bm.verts.new(p)
        for i in range(seg):
            j = (i + 1) % seg
            if flip:
                bm.faces.new((loop[j], loop[i], c))
            else:
                bm.faces.new((loop[i], loop[j], c))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return new_obj(name, bm, mat, smooth=smooth)


def spline(points, samples):
    """Catmull-Rom through `points`, resampled to `samples` positions."""
    pts = [Vector(p) for p in points]
    ext = [pts[0]] + pts + [pts[-1]]
    out = []
    for s in range(samples):
        u = s / (samples - 1) * (len(pts) - 1)
        i = min(int(u), len(pts) - 2)
        t = u - i
        p0, p1, p2, p3 = ext[i], ext[i + 1], ext[i + 2], ext[i + 3]
        t2, t3 = t * t, t * t * t
        out.append(0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                          (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                          (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3))
    return out


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
    dark, metal = mats["dark"], mats["metal"]
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


# ---------------------------------------------------------------------------
# CRT TV
# ---------------------------------------------------------------------------
TV_AT = (105.0, 30.0, 0.0)


def build_tv(mats, location):
    objs = []
    dark, shell, metal = mats["dark"], mats["shell"], mats["metal"]
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


# ---------------------------------------------------------------------------
# assembly and export
# ---------------------------------------------------------------------------
def recenter():
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    xs = [v.co.x for ob in meshes for v in ob.data.vertices]
    ys = [v.co.y for ob in meshes for v in ob.data.vertices]
    zs = [v.co.z for ob in meshes for v in ob.data.vertices]
    shift = Vector((-(min(xs) + max(xs)) * 0.5,
                    -(min(ys) + max(ys)) * 0.5, -min(zs)))
    for ob in meshes:
        for v in ob.data.vertices:
            v.co += shift
    return shift


def model_bounds():
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    xs = [v.co.x for ob in meshes for v in ob.data.vertices]
    ys = [v.co.y for ob in meshes for v in ob.data.vertices]
    zs = [v.co.z for ob in meshes for v in ob.data.vertices]
    return (min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs))


def parent_to_root(name):
    root = bpy.data.objects.new(name, None)
    root.empty_display_size = 40.0
    bpy.context.scene.collection.objects.link(root)
    for ob in [o for o in bpy.data.objects if o.type == 'MESH']:
        ob.parent = root
        ob.matrix_parent_inverse = Matrix.Identity(4)
    return root


def model_stats():
    tris, verts = 0, 0
    for ob in bpy.data.objects:
        if ob.type != 'MESH':
            continue
        me = ob.data
        verts += len(me.vertices)
        tris += sum(max(1, len(p.vertices) - 2) for p in me.polygons)
    return verts, tris


def export_glb(path, root_name):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    for ob in bpy.data.objects:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = bpy.data.objects.get(root_name)
    bpy.ops.export_scene.gltf(
        filepath=os.path.abspath(path),
        export_format='GLB',
        use_selection=True,
        export_apply=True,
        export_yup=True,
        export_extras=False,
        export_normals=True,
        export_texcoords=True,
        export_materials='EXPORT',
    )
    data = open(path, 'rb').read()
    digest = hashlib.sha256(data).hexdigest()
    print(f"wrote {path} ({len(data)} bytes) sha256 {digest}")
    return digest, len(data)


# ---------------------------------------------------------------------------
# preview render (the screen artwork is added only after the GLB is written)
# ---------------------------------------------------------------------------
def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def screen_image(w=512, h=384):
    """A deterministic, generic 16-bit platformer frame for the preview only."""
    px = [0.0] * (w * h * 4)
    horizon = int(h * 0.36)
    ground_top = int(h * 0.24)

    def put(x, y, r, g, b):
        if 0 <= x < w and 0 <= y < h:
            i = (y * w + x) * 4
            px[i] = srgb_to_linear(r)
            px[i + 1] = srgb_to_linear(g)
            px[i + 2] = srgb_to_linear(b)
            px[i + 3] = 1.0

    def rect(x0, y0, x1, y1, c):
        for yy in range(max(0, y0), min(h, y1)):
            for xx in range(max(0, x0), min(w, x1)):
                put(xx, yy, *c)

    def disc(cx, cy, rad, c):
        rr = rad * rad
        for yy in range(max(0, cy - rad), min(h, cy + rad + 1)):
            for xx in range(max(0, cx - rad), min(w, cx + rad + 1)):
                if (xx - cx) ** 2 + (yy - cy) ** 2 <= rr:
                    put(xx, yy, *c)

    # sky: deep blue at the top, warm haze at the horizon
    for yy in range(h):
        t = yy / (h - 1.0)
        r = 0.05 + 0.55 * (1.0 - t) ** 2
        g = 0.10 + 0.55 * (1.0 - t) ** 2
        b = 0.38 + 0.44 * (1.0 - t) ** 1.1
        for xx in range(w):
            put(xx, yy, r, g, b)
    disc(int(w * 0.76), int(h * 0.55), 26, (1.0, 0.90, 0.52))
    disc(int(w * 0.76), int(h * 0.55), 19, (1.0, 0.96, 0.70))
    for cx, cy, sc in ((int(w * 0.18), int(h * 0.72), 1.0),
                       (int(w * 0.52), int(h * 0.80), 0.78),
                       (int(w * 0.84), int(h * 0.83), 0.6)):
        for dx, dy, rr in ((0, 0, 20), (18, 4, 16), (-20, 2, 14),
                           (7, 11, 14), (-7, 11, 14)):
            disc(int(cx + dx * sc), int(cy + dy * sc), int(rr * sc),
                 (0.94, 0.97, 1.0))
    # far mountains, then the near hills
    for xx in range(w):
        hh = horizon + int(42 + 30 * math.sin(xx * 0.011) + 16 * math.sin(xx * 0.034 + 1.2))
        rect(xx, hh, xx + 1, int(h * 0.46), (0.22, 0.29, 0.50))
    for xx in range(w):
        hh = int(h * 0.34 + 26 * math.sin(xx * 0.017 + 2.0) + 12 * math.sin(xx * 0.051))
        rect(xx, hh, xx + 1, int(h * 0.38), (0.17, 0.42, 0.28))
    # ground: grass lip over a dirt band
    rect(0, 0, w, ground_top, (0.36, 0.22, 0.12))
    for yy in range(ground_top, int(h * 0.32)):
        for xx in range(w):
            if (xx // 8 + yy // 4) % 2 == 0:
                put(xx, yy, 0.30, 0.17, 0.09)
    rect(0, int(h * 0.24), w, int(h * 0.28), (0.30, 0.66, 0.30))
    for xx in range(w):
        if (xx // 4) % 3 == 0:
            rect(xx, int(h * 0.28), xx + 1, int(h * 0.28) + 4, (0.40, 0.78, 0.38))
    # brick platform and bushes for foreground interest
    for bx in range(int(w * 0.34), int(w * 0.47), 22):
        rect(bx, int(h * 0.34), bx + 20, int(h * 0.40), (0.56, 0.30, 0.16))
        rect(bx, int(h * 0.40), bx + 20, int(h * 0.42), (0.74, 0.44, 0.24))
    for bx, by in ((int(w * 0.08), int(h * 0.26)), (int(w * 0.74), int(h * 0.26))):
        for dx, dy, rr in ((0, 0, 13), (10, 2, 11), (-10, 2, 10)):
            disc(bx + dx, by + dy, rr, (0.20, 0.50, 0.24))
    # floating blocks and coins
    for bx in (int(w * 0.55), int(w * 0.63)):
        by = int(h * 0.40)
        rect(bx, by, bx + 28, by + 28, (0.78, 0.46, 0.18))
        rect(bx, by + 22, bx + 28, by + 28, (0.94, 0.66, 0.30))
        rect(bx + 4, by + 4, bx + 8, by + 8, (0.55, 0.30, 0.10))
        rect(bx + 4, by + 16, bx + 12, by + 20, (0.62, 0.36, 0.12))
    for cx in (int(w * 0.57), int(w * 0.63), int(w * 0.69)):
        disc(cx, int(h * 0.56), 9, (0.98, 0.80, 0.18))
        disc(cx, int(h * 0.56), 5, (1.0, 0.93, 0.45))
    # hero sprite, 12 x 16 at 6x, standing on the grass
    hero = [
        "....rrrr....", "...rrrrrr...", "...ssssss...", "..sesssses..",
        "..ssssssss..", "...ssssss...", "..bbbbbbbb..", ".bbbbbbbbbb.",
        "sbbbbbbbbbbs", "sbbbbbbbbbbs", "..bbbbbbbb..", "..bbbbbbbb..",
        "..bb....bb..", "..bb....bb..", ".yyyy..yyyy.", ".yyyy..yyyy.",
    ]
    pal = {"r": (0.86, 0.16, 0.16), "s": (0.98, 0.76, 0.58),
           "e": (0.08, 0.08, 0.12), "b": (0.16, 0.30, 0.86),
           "y": (0.36, 0.22, 0.10)}
    scale, hx0, hy0 = 5, int(w * 0.19), int(h * 0.24)
    for row, line in enumerate(hero):
        for col, ch in enumerate(line):
            if ch in pal:
                rect(hx0 + col * scale, hy0 + (15 - row) * scale,
                     hx0 + (col + 1) * scale, hy0 + (16 - row) * scale, pal[ch])

    # HUD strip
    rect(0, h - 30, w, h, (0.02, 0.05, 0.12))
    disc(26, h - 15, 8, (0.98, 0.80, 0.18))
    digits = {"3": ("111", "001", "111", "001", "111"),
              "4": ("101", "101", "111", "001", "001"),
              "7": ("111", "001", "011", "010", "010")}
    for n, ch in enumerate("347"):
        for ry, row in enumerate(digits[ch]):
            for rx, cell in enumerate(row):
                if cell == "1":
                    rect(44 + n * 16 + rx * 3, h - 24 + (4 - ry) * 3 + 1,
                         44 + n * 16 + (rx + 1) * 3, h - 24 + (5 - ry) * 3 + 1,
                         (0.92, 0.96, 1.0))
    # a small life heart
    for dx, dy in ((0, 1), (1, 1), (2, 1), (1, 0), (1, 2)):
        rect(w - 44 + dx * 5, h - 22 + dy * 5, w - 44 + (dx + 1) * 5,
             h - 22 + (dy + 1) * 5, (0.90, 0.20, 0.24))

    # scanlines and vignette
    for yy in range(h):
        mul = 0.82 if yy % 2 == 0 else 1.0
        for xx in range(w):
            i = (yy * w + xx) * 4
            nx = (xx - w * 0.5) / (w * 0.5)
            ny = (yy - h * 0.5) / (h * 0.5)
            vig = 1.0 - 0.32 * min(1.0, (nx * nx + ny * ny) * 0.8)
            px[i] *= mul * vig
            px[i + 1] *= mul * vig
            px[i + 2] *= mul * vig

    img = bpy.data.images.new("screen_art", w, h, alpha=True, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'
    img.pixels[:] = px
    img.pack()
    return img


def wire_screen_art(mats):
    img = screen_image()
    mat = mats["screen"]
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = 'Closest'
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = 1.4
    return img


def render_previews(directory, samples=48, res=(1000, 765), prefix="",
                    target=(0.0, 0.0, 178.0),
                    cameras=(("front", (0.0, -1490.0, 430.0)),
                             ("three-quarter", (1010.0, -1060.0, 535.0)),
                             ("angle-120", (-1330.0, 790.0, 690.0)))):
    os.makedirs(directory, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 70

    world = scene.world or bpy.data.worlds.new("W")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.018, 0.021, 0.030, 1.0)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.8

    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=2400.0)
    me = bpy.data.meshes.new("floor")
    bm.to_mesh(me)
    bm.free()
    floor = bpy.data.objects.new("floor", me)
    scene.collection.objects.link(floor)
    fm = make_material("preview_floor", (0.042, 0.044, 0.052, 1.0), 0.55)
    floor.data.materials.append(fm)

    def area(name, loc, energy, size, color=(1.0, 1.0, 1.0)):
        d = bpy.data.lights.new(name, 'AREA')
        d.energy = energy
        d.size = size
        d.color = color
        o = bpy.data.objects.new(name, d)
        scene.collection.objects.link(o)
        o.location = loc
        o.rotation_euler = (Vector((0.0, 0.0, 170.0)) - o.location).normalized() \
            .to_track_quat('-Z', 'Y').to_euler()

    area("key", (-820.0, -950.0, 1250.0), 1.5e7, 620.0, (1.0, 0.96, 0.90))
    area("fill", (1150.0, -650.0, 480.0), 6.5e6, 900.0, (0.80, 0.88, 1.0))
    area("rim", (250.0, 1250.0, 900.0), 9.0e6, 700.0, (0.85, 0.90, 1.0))
    area("top", (0.0, 100.0, 1650.0), 4.0e6, 800.0)
    area("front", (250.0, -1450.0, 220.0), 4.5e6, 900.0, (1.0, 0.97, 0.92))

    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 65.0
    cam_data.clip_start = 10.0
    cam_data.clip_end = 100000.0
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam

    scene.view_settings.exposure = 0.55
    look_at = Vector(target)
    for name, loc in cameras:
        loc = Vector(loc)
        cam.location = loc
        cam.rotation_euler = (look_at - loc).normalized() \
            .to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = os.path.join(os.path.abspath(directory),
                                             prefix + name + ".png")
        bpy.ops.render.render(write_still=True)
        print("wrote", scene.render.filepath)


# ---------------------------------------------------------------------------
# systems
# ---------------------------------------------------------------------------
# The table keeps the shared diorama (CRT TV, materials, lighting, preview rig)
# and the per-system console/controller/cable in one place.
SYSTEMS = {
    "snes": {
        "root": "snes_diorama",
        "output": "android/app/src/main/assets/carts/consoles/snes.glb",
        "palette": SNES_PALETTE,
        "console": build_snes,
        "console_at": SNES_AT,
        "controller": build_snes_controller,
        "controller_at": (-80.0, -345.0, 0.0),
        "controller_rot": -8.0,
        "cable": build_cable,
        "preview_prefix": "",
        "preview_res": (1000, 765),
    },
    "nes": {
        "root": "nes_diorama",
        "output": "android/app/src/main/assets/carts/consoles/nes.glb",
        "palette": NES_PALETTE,
        "console": build_nes,
        "console_at": NES_AT,
        "controller": build_nes_controller,
        "controller_at": NES_CTRL_AT,
        "controller_rot": NES_CTRL_ROT,
        "cable": build_nes_cable,
        "preview_prefix": "nes-",
        "preview_res": (880, 660),
    },
}


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
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

    console = cfg["console"](mats, cfg["console_at"])
    build_tv(mats, TV_AT)
    ctrl = cfg["controller"](mats, cfg["controller_at"],
                             math.radians(cfg["controller_rot"]))
    cfg["cable"](mats, ctrl["cable_anchor"], console["port_2"])

    shift = recenter()
    parent_to_root(cfg["root"])
    verts, tris = model_stats()
    print(f"recentre shift {tuple(round(v, 3) for v in shift)}")
    (x0, x1), (y0, y1), (z0, z1) = model_bounds()
    print(f"STATS verts={verts} tris={tris}")
    print(f"BOUNDS x {x0:.1f}..{x1:.1f} (w {x1 - x0:.1f}), y {y0:.1f}..{y1:.1f} (d {y1 - y0:.1f}), "
          f"z {z0:.1f}..{z1:.1f} (h {z1 - z0:.1f})")
    print("MATERIALS " + ", ".join(sorted(m.name for m in bpy.data.materials)))
    digest, size = export_glb(args.output or cfg["output"], cfg["root"])

    if args.previews:
        wire_screen_art(mats)
        render_previews(args.previews, samples=args.samples,
                        prefix=cfg["preview_prefix"], res=cfg["preview_res"])
    print(f"DONE sha256={digest} bytes={size} verts={verts} tris={tris}")


if __name__ == "__main__":
    main()
