# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake Phobos's ZX Spectrum cassette-case shell to assets/carts/carts/tape.glb.

This shell is Phobos's own work, not a Cartridge output: Cartridge's pipeline has
cart and disc geometry only, and the Spectrum's games are tapes. It is modeled
here so the tape shell stays reproducible: run it with Blender 5.2 (it needs
nothing but bpy/bmesh):

    blender --background --python tools/carts/tape/make_tape.py -- \
        --output android/app/src/main/assets/carts/carts/tape.glb

Units are millimetres, in the convention the baked carts use: width on X, height
on Z, thickness on Y, the printed face toward -Y. The materials are named for
CartMaterials: `shell` takes the per-game colour and finish, `dark` is the case's
tray and seam, and `label` is the printed inlay card (UV 0..1, upright).
"""
import argparse
import math
import os
import sys

import bpy
import bmesh

W, H, T = 109.0, 70.0, 17.0  # a standard compact-cassette case
FRONT = -T / 2.0


def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat)
    for img in list(bpy.data.images):
        if img.users == 0:
            bpy.data.images.remove(img)


def material(name, rgba, roughness=0.5, metallic=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = rgba
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    return m


def rounded_box(name, sx, sy, sz, bevel=1.4, segments=4):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= sx
        v.co.y *= sy
        v.co.z *= sz
    bmesh.ops.bevel(
        bm,
        geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
        offset=bevel,
        segments=segments,
        affect='EDGES',
        clamp_overlap=True,
    )
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def label_quad(name, sw, sh, y):
    """A flat card in XZ facing -Y, UV 0..1 (u left to right, v bottom to top)."""
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    hw, hh = sw / 2.0, sh / 2.0
    verts = [
        bm.verts.new((-hw, y, -hh)),
        bm.verts.new((hw, y, -hh)),
        bm.verts.new((hw, y, hh)),
        bm.verts.new((-hw, y, hh)),
    ]
    face = bm.faces.new(verts)
    uv = bm.loops.layers.uv.new("UVMap")
    for loop in face.loops:
        x, _y, z = loop.vert.co
        loop[uv].uv = ((x + hw) / sw, (z + hh) / sh)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def build():
    clear_scene()
    scene = bpy.context.scene

    root = bpy.data.objects.new("tape", None)
    scene.collection.objects.link(root)

    shell = rounded_box("shell", W, T, H)
    shell.data.materials.append(material("shell", (0.045, 0.045, 0.05, 1.0), roughness=0.38))

    dark = material("dark", (0.018, 0.018, 0.02, 1.0), roughness=0.58)

    # The two case halves meet in a raised seam around the sides.
    seam = rounded_box("dark", W + 0.6, 2.2, H + 0.6, bevel=0.5, segments=2)
    seam.data.materials.append(dark)

    # The inlay slides behind the case's clear front lip: a thin dark tray, then the printed card.
    tray = rounded_box("dark", W - 2.0, 1.0, H - 2.0, bevel=0.6, segments=2)
    tray.location = (0.0, FRONT + 0.3, 0.0)
    tray.data.materials.append(dark)

    card = label_quad("label", W - 5.0, H - 5.0, FRONT - 0.55)
    card.data.materials.append(material("label", (0.9, 0.9, 0.9, 1.0), roughness=0.5))

    for ob in (shell, seam, tray, card):
        ob.parent = root
        ob.matrix_parent_inverse = root.matrix_world.inverted()

    bpy.context.view_layer.objects.active = shell
    for ob in (root, shell, seam, tray, card):
        ob.select_set(True)

    return root, shell, seam, tray, card


def render_preview(path):
    """Render a front three-quarter preview with a test inlay (upright check)."""
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 48
    scene.render.resolution_x = 900
    scene.render.resolution_y = 620

    world = scene.world or bpy.data.worlds.new("W")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.05, 0.06, 1.0)

    img = bpy.data.images.new("inlay", 256, 160, alpha=False)
    px = [0.0] * (256 * 160 * 4)

    def put(x, y, c):
        i = ((159 - y) * 256 + x) * 4
        px[i:i + 4] = [c[0], c[1], c[2], 1.0]

    for y in range(160):
        for x in range(256):
            put(x, y, (0.82, 0.82, 0.86))
    cols = [(0.9, 0.2, 0.2), (0.95, 0.55, 0.15), (0.95, 0.9, 0.2), (0.25, 0.75, 0.3), (0.2, 0.45, 0.9)]
    for y in range(44):
        for x in range(256):
            if x + (43 - y) * 3 < 150:
                put(x, y, cols[min(4, (43 - y) // 9)])
    for y in range(52, 140):
        for x in range(150, 230):
            put(x, y, (0.06, 0.09, 0.28))
    for y in range(58, 132):
        for x in range(158, 176):
            put(x, y, (0.95, 0.95, 0.95))
    for y in range(58, 76):
        for x in range(158, 220):
            put(x, y, (0.95, 0.95, 0.95))
    for y in range(88, 104):
        for x in range(158, 206):
            put(x, y, (0.95, 0.95, 0.95))
    img.pixels[:] = px
    img.pack()

    mat = bpy.data.materials["label"]
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    nt.links.new(tex.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])

    mesh = bpy.data.meshes.new("floor")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=600.0)
    bm.to_mesh(mesh)
    bm.free()
    floor = bpy.data.objects.new("floor", mesh)
    scene.collection.objects.link(floor)
    floor.location = (0, 0, -40.0)
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.03, 0.032, 0.038, 1.0)
    fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.65
    floor.data.materials.append(fm)

    from mathutils import Vector

    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 70
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = (150.0, -250.0, 95.0)
    cam.rotation_euler = (Vector((0, 0, 4)) - cam.location).normalized().to_track_quat('-Z', 'Y').to_euler()
    scene.camera = cam

    def area(name, loc, energy, size):
        d = bpy.data.lights.new(name, 'AREA')
        d.energy = energy
        d.size = size
        o = bpy.data.objects.new(name, d)
        scene.collection.objects.link(o)
        o.location = loc
        o.rotation_euler = (Vector((0, 0, 0)) - o.location).normalized().to_track_quat('-Z', 'Y').to_euler()

    area("key", (200, -260, 300), 420000, 260)
    area("fill", (-300, -160, 120), 160000, 300)
    area("rim", (0, 260, 220), 180000, 220)

    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default="android/app/src/main/assets/carts/carts/tape.glb")
    ap.add_argument("--preview", default=None)
    args = ap.parse_args(argv)

    root, shell, seam, tray, card = build()
    out = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=out,
        export_format='GLB',
        use_selection=True,
        export_apply=True,
        export_yup=True,
    )
    print("wrote", out)

    if args.preview:
        render_preview(os.path.abspath(args.preview))
        print("wrote", args.preview)


if __name__ == "__main__":
    main()
