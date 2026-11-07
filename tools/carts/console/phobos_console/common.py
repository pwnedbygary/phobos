# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared modelling and export helpers for the Phobos system dioramas.

Everything here is system-agnostic: scene/material construction, the bmesh
primitives, recentring and parenting, the glTF export, and the preview rig.
A system module only authors its own console, controller and cable.
"""
import hashlib
import math
import os

import bpy
import bmesh
from mathutils import Matrix, Vector

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
