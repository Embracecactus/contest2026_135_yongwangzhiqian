# SPDX-License-Identifier: Apache-2.0
"""Build the film's fictional protagonist 周屿 and his close-up hands in Blender.

Original procedural modelling for 《我在》: a stylised, faceless figure whose
glasses carry the expression. No scan, photo or real person's likeness.

  - Body: skin-modifier "clay" shapes for sweater, trousers, neck, mitten hands
    and shoes, subdivided, parented with automatic weights to a named armature
    (root, spine, chest, neck, head, shoulder/upperarm/forearm/hand .L/.R,
    thigh/shin/foot .L/.R). Head, hair, nose, ears and glasses are rigid parts
    weighted to the head bone. Rest pose is a relaxed A-pose.
  - Hands: separate rigid close-up meshes with five fingers in four poses
    (relaxed, grip, point, pinch), each with a sweater cuff, for POV shots.

Units are metres (1 BU = 1 m). Blender is Z-up, the figure faces -Y; the glTF
export converts to three.js Y-up (+Z forward after export).

Usage (Windows Blender from WSL):
  blender.exe -b --factory-startup --python build_character.py -- --out <dir>
Writes zhouyu.glb and hands.glb into <dir>, refusing to overwrite.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else os.getcwd()

COL = {
    "sweater": "#A8894F",   # muted mustard knit
    "trousers": "#2B3140",  # dark navy
    "skin": "#E2BC9C",
    "hair": "#1E1A19",
    "shoes": "#E9E4DA",
    "frame": "#232326",
    "lens": "#9FB6C8",
}


def lin(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c] + [1.0]


def material(name, rough=0.8, metal=0.0, alpha=1.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = lin(COL[name])
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = metal
    if alpha < 1.0:
        p.inputs["Alpha"].default_value = alpha
        m.blend_method = "BLEND" if hasattr(m, "blend_method") else None
    return m


def skin_object(name, verts, edges, radii, mat, subdiv=2, root=0):
    """verts: list of (x,y,z); radii: list of (rx, ry) in metres."""
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, edges, [])
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    mod = obj.modifiers.new("Skin", "SKIN")
    mod.use_smooth_shade = True
    mod.branch_smoothing = 0.6
    for i, r in enumerate(radii):
        sv = mesh.skin_vertices[0].data[i]
        sv.radius = r
    mesh.skin_vertices[0].data[root].use_root = True
    sub = obj.modifiers.new("Sub", "SUBSURF")
    sub.levels = subdiv
    sub.render_levels = subdiv
    bpy.context.view_layer.objects.active = obj
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier="Skin")
    bpy.ops.object.modifier_apply(modifier="Sub")
    bpy.ops.object.shade_smooth()
    obj.data.materials.append(mat)
    return obj


def ellipsoid(name, center, scale, mat, segments=32, rings=20):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, radius=1.0, location=center)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def torus(name, center, major, minor, rot, mat):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=40,
                                     minor_segments=10, location=center, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def cylinder(name, a, b, r, mat):
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=d.length, vertices=12, location=(a + b) / 2)
    o = bpy.context.active_object
    o.name = name
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def join(objs, name):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    return o


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# --------------------------------------------------------------- skeleton
J = {}
J["root"] = Vector((0, 0, 0.96))
J["spine"] = Vector((0, 0.005, 1.10))
J["chest"] = Vector((0, 0.01, 1.30))
J["neck"] = Vector((0, 0.0, 1.44))
J["head"] = Vector((0, -0.005, 1.50))
J["head_end"] = Vector((0, -0.005, 1.75))
for s, sx in (("L", 1), ("R", -1)):
    J["shoulder." + s] = Vector((sx * 0.05, 0.01, 1.41))
    J["upperarm." + s] = Vector((sx * 0.185, 0.015, 1.395))
    ang = math.radians(52)   # A-pose: arm hangs 52 degrees from horizontal
    elbow = J["upperarm." + s] + Vector((sx * 0.28 * math.cos(ang), 0.0, -0.28 * math.sin(ang)))
    J["forearm." + s] = elbow
    wrist = elbow + Vector((sx * 0.25 * math.cos(math.radians(60)), -0.02, -0.25 * math.sin(math.radians(60))))
    J["hand." + s] = wrist
    J["hand_end." + s] = wrist + Vector((sx * 0.085 * math.cos(math.radians(62)), -0.01, -0.085 * math.sin(math.radians(62))))
    J["thigh." + s] = Vector((sx * 0.095, 0.0, 0.93))
    J["shin." + s] = Vector((sx * 0.10, -0.005, 0.505))
    J["foot." + s] = Vector((sx * 0.10, 0.02, 0.085))
    J["toe." + s] = Vector((sx * 0.105, -0.125, 0.03))

BONES = [  # name, head, tail, parent
    ("root", "root", "spine", None),
    ("spine", "spine", "chest", "root"),
    ("chest", "chest", "neck", "spine"),
    ("neck", "neck", "head", "chest"),
    ("head", "head", "head_end", "neck"),
]
for s in ("L", "R"):
    BONES += [
        ("shoulder." + s, "shoulder." + s, "upperarm." + s, "chest"),
        ("upperarm." + s, "upperarm." + s, "forearm." + s, "shoulder." + s),
        ("forearm." + s, "forearm." + s, "hand." + s, "upperarm." + s),
        ("hand." + s, "hand." + s, "hand_end." + s, "forearm." + s),
        ("thigh." + s, "thigh." + s, "shin." + s, "root"),
        ("shin." + s, "shin." + s, "foot." + s, "thigh." + s),
        ("foot." + s, "foot." + s, "toe." + s, "shin." + s),
    ]


def build_body():
    clear()
    m_sw, m_tr, m_sk = material("sweater", 0.92), material("trousers", 0.85), material("skin", 0.62)
    m_hair, m_sh = material("hair", 0.75), material("shoes", 0.7)
    m_fr, m_ln = material("frame", 0.35, 0.4), material("lens", 0.05, 0.0)
    parts = []
    # Sweater: torso + arms, with a soft roll collar and cuffs.
    v, e, r = [], [], []

    def add(p, rad, link=None):
        v.append(tuple(p))
        r.append(rad)
        if link is not None:
            e.append((link, len(v) - 1))
        return len(v) - 1
    hem = add(Vector((0, 0.0, 1.00)), (0.150, 0.115))
    waist = add(J["spine"], (0.140, 0.105), hem)
    chest = add(J["chest"], (0.172, 0.118), waist)
    collar = add(Vector((0, 0.008, 1.43)), (0.088, 0.080), chest)
    add(Vector((0, 0.006, 1.505)), (0.070, 0.066), collar)
    for s in ("L", "R"):
        sh = add(J["upperarm." + s] + Vector((0, 0, -0.005)), (0.074, 0.074), chest)
        el = add(J["forearm." + s], (0.060, 0.060), sh)
        cu = add(J["hand." + s] + (J["forearm." + s] - J["hand." + s]).normalized() * 0.025, (0.046, 0.046), el)
    parts.append(skin_object("sweater", v, e, r, m_sw, subdiv=2, root=chest))
    # Trousers.
    v, e, r = [], [], []
    pel = add(Vector((0, 0.0, 1.02)), (0.142, 0.110))
    for s in ("L", "R"):
        hp = add(J["thigh." + s] + Vector((0, 0, 0.0)), (0.082, 0.085), pel)
        kn = add(J["shin." + s], (0.060, 0.062), hp)
        an = add(J["foot." + s] + Vector((0, 0, 0.03)), (0.050, 0.052), kn)
    parts.append(skin_object("trousers", v, e, r, m_tr, subdiv=2, root=pel))
    # Neck.
    v, e, r = [], [], []
    n0 = add(Vector((0, 0.006, 1.46)), (0.050, 0.048))
    add(Vector((0, 0.0, 1.54)), (0.048, 0.046), n0)
    parts.append(skin_object("neck", v, e, r, m_sk, subdiv=2, root=0))
    # Mitten hands with a thumb.
    for s, sx in (("L", 1), ("R", -1)):
        v, e, r = [], [], []
        w = J["hand." + s]
        d = (J["hand_end." + s] - w).normalized()
        side = Vector((0, -1, 0))
        w0 = add(w + d * -0.01, (0.034, 0.024))
        pa = add(w + d * 0.045, (0.044, 0.020), w0)
        ti = add(w + d * 0.100, (0.036, 0.016), pa)
        th = add(w + d * 0.030 + side * 0.035 + Vector((sx * 0.0, 0, 0)), (0.016, 0.016), w0)
        add(w + d * 0.070 + side * 0.055, (0.012, 0.012), th)
        parts.append(skin_object("hand." + s, v, e, r, m_sk, subdiv=2, root=0))
    # Shoes.
    for s in ("L", "R"):
        v, e, r = [], [], []
        heel = add(J["foot." + s] + Vector((0, 0.035, -0.045)), (0.045, 0.040))
        mid = add(J["foot." + s] + Vector((0, -0.04, -0.05)), (0.050, 0.035), heel)
        add(J["toe." + s] + Vector((0, 0.0, -0.005)), (0.042, 0.028), mid)
        parts.append(skin_object("shoe." + s, v, e, r, m_sh, subdiv=2, root=0))
    # Rigid head parts.
    hc = Vector((0, -0.012, 1.628))
    head = ellipsoid("head", hc, (0.100, 0.110, 0.120), m_sk)
    jaw = ellipsoid("jaw", hc + Vector((0, -0.028, -0.058)), (0.078, 0.078, 0.070), m_sk)
    nose = ellipsoid("nose", hc + Vector((0, -0.112, -0.012)), (0.016, 0.022, 0.024), m_sk, 16, 10)
    earL = ellipsoid("ear.L", hc + Vector((0.100, 0.005, -0.005)), (0.014, 0.026, 0.036), m_sk, 16, 10)
    earR = ellipsoid("ear.R", hc + Vector((-0.100, 0.005, -0.005)), (0.014, 0.026, 0.036), m_sk, 16, 10)
    hair_main = ellipsoid("hair", hc + Vector((0, 0.018, 0.040)), (0.112, 0.122, 0.112), m_hair)
    fringe = ellipsoid("fringe", hc + Vector((0.02, -0.075, 0.072)), (0.090, 0.050, 0.040), m_hair, 24, 14)
    tuft1 = ellipsoid("tuft1", hc + Vector((-0.055, -0.04, 0.095)), (0.045, 0.06, 0.035), m_hair, 20, 12)
    tuft2 = ellipsoid("tuft2", hc + Vector((0.06, 0.02, 0.105)), (0.05, 0.06, 0.03), m_hair, 20, 12)
    back = ellipsoid("hairback", hc + Vector((0, 0.06, -0.02)), (0.100, 0.07, 0.10), m_hair, 24, 14)
    # Glasses: two rings, bridge, temples, lenses.
    gy = hc.y - 0.118
    gz = hc.z + 0.012
    rL = torus("rimL", (0.040, gy, gz), 0.028, 0.0035, (math.radians(90), 0, 0), m_fr)
    rR = torus("rimR", (-0.040, gy, gz), 0.028, 0.0035, (math.radians(90), 0, 0), m_fr)
    br = cylinder("bridge", (0.012, gy, gz + 0.004), (-0.012, gy, gz + 0.004), 0.0028, m_fr)
    tL = cylinder("templeL", (0.068, gy + 0.004, gz), (0.098, gy + 0.10, gz + 0.004), 0.0025, m_fr)
    tR = cylinder("templeR", (-0.068, gy + 0.004, gz), (-0.098, gy + 0.10, gz + 0.004), 0.0025, m_fr)
    bpy.ops.mesh.primitive_circle_add(vertices=32, radius=0.026, fill_type="NGON", location=(0.040, gy - 0.001, gz),
                                      rotation=(math.radians(90), 0, 0))
    lL = bpy.context.active_object
    lL.name = "lensL"
    lL.data.materials.append(m_ln)
    bpy.ops.mesh.primitive_circle_add(vertices=32, radius=0.026, fill_type="NGON", location=(-0.040, gy - 0.001, gz),
                                      rotation=(math.radians(90), 0, 0))
    lR = bpy.context.active_object
    lR.name = "lensR"
    lR.data.materials.append(m_ln)
    rigid = [head, jaw, nose, earL, earR, hair_main, fringe, tuft1, tuft2, back, rL, rR, br, tL, tR, lL, lR]

    # Armature.
    arm_data = bpy.data.armatures.new("rig")
    arm = bpy.data.objects.new("rig", arm_data)
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    for o in bpy.context.selected_objects:
        o.select_set(False)
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones
    for name, h, t, parent in BONES:
        b = eb.new(name)
        b.head = J[h]
        b.tail = J[t]
        if parent:
            b.parent = eb[parent]
            b.use_connect = False
    for s in ("L", "R"):  # roll arms so their local X points forward
        for n in ("upperarm.", "forearm.", "hand."):
            eb[n + s].roll = 0.0
    bpy.ops.object.mode_set(mode="OBJECT")

    # Deforming parts: automatic weights.
    for p in parts:
        for o in bpy.context.selected_objects:
            o.select_set(False)
        p.select_set(True)
        arm.select_set(True)
        bpy.context.view_layer.objects.active = arm
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    # Rigid head parts: everything to the head bone.
    head_join = join(rigid, "headparts")
    vg = head_join.vertex_groups.new(name="head")
    vg.add(list(range(len(head_join.data.vertices))), 1.0, "REPLACE")
    head_join.parent = arm
    mod = head_join.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    body = join([head_join] + parts, "zhouyu")
    return arm, body


def export(path, objs):
    if os.path.exists(path):
        raise SystemExit("refusing to overwrite " + path)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_skins=True,
                              export_animations=False, export_yup=True, export_apply=False,
                              export_materials="EXPORT")


def build_hands():
    clear()
    m_sk, m_sw = material("skin", 0.6), material("sweater", 0.92)
    out = []

    def hand(name, finger_curl, thumb, index_curl=None, pinch=False):
        # Right hand in its own frame: wrist at origin, fingers along -Y
        # (forward), palm facing -Z (down), thumb toward -X.
        v, e, r = [], [], []

        def add(p, rad, link=None):
            v.append(tuple(p))
            r.append(rad)
            if link is not None:
                e.append((link, len(v) - 1))
            return len(v) - 1
        wr = add(Vector((0, 0.0, 0)), (0.030, 0.021))
        palm = add(Vector((0, -0.045, 0.002)), (0.040, 0.015), wr)
        knuck = add(Vector((0, -0.085, 0.004)), (0.042, 0.013), palm)
        fingers = [(-0.027, 0.046, 0.0155), (-0.009, 0.050, 0.0160), (0.009, 0.048, 0.0155), (0.026, 0.040, 0.0140)]
        for k, (fx, length, rad) in enumerate(fingers):
            curl = (index_curl if (index_curl is not None and k == 0) else finger_curl)
            base = add(Vector((fx, -0.088, 0.004)), (rad * 0.62, rad * 0.55), knuck)
            p = Vector((fx, -0.088, 0.004))
            ang = 0.0
            prev = base
            segs = (0.45, 0.32, 0.23)
            for j, sl in enumerate(segs):
                ang += curl * (0.9 if j == 0 else 1.0)
                p = p + Vector((0, -math.cos(ang), -math.sin(ang))) * (length * sl)
                prev = add(p, (rad * (0.56 - 0.07 * j), rad * (0.50 - 0.07 * j)), prev)
        # Thumb from the palm side.
        tb = add(Vector((-0.032, -0.025, -0.006)), (0.016, 0.014), palm)
        tp = Vector((-0.032, -0.025, -0.006))
        tdir = Vector(thumb).normalized()
        prev = tb
        for j, sl in enumerate((0.028, 0.024, 0.018)):
            tp = tp + tdir * sl
            prev = add(tp, (0.0125 - 0.0015 * j, 0.011 - 0.0015 * j), prev)
        o = skin_object(name, v, e, r, m_sk, subdiv=2, root=0)
        # Sweater cuff + forearm stub behind the wrist (+Y).
        cv, ce, cr = [], [], []

        def cadd(p, rad, link=None):
            cv.append(tuple(p))
            cr.append(rad)
            if link is not None:
                ce.append((link, len(cv) - 1))
            return len(cv) - 1
        c0 = cadd(Vector((0, -0.008, 0.0)), (0.038, 0.034))
        c1 = cadd(Vector((0, 0.10, 0.004)), (0.046, 0.044), c0)
        cadd(Vector((0, 0.30, 0.01)), (0.052, 0.050), c1)
        cuff = skin_object(name + "_cuff", cv, ce, cr, m_sw, subdiv=2, root=0)
        j = join([o, cuff], name)
        out.append(j)

    hand("hand_relaxed", math.radians(18), (-0.6, -0.8, -0.2))
    hand("hand_grip", math.radians(52), (-0.2, -1.0, -0.45))
    hand("hand_point", math.radians(68), (-0.3, -0.6, -0.6), index_curl=math.radians(4))
    hand("hand_pinch", math.radians(60), (0.15, -1.0, -0.35), index_curl=math.radians(38))
    for k, o in enumerate(out):
        o.location.x = k * 0.2
    return out


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    arm, body = build_body()
    export(os.path.join(OUT, "zhouyu.glb"), [arm, body])
    hands = build_hands()
    export(os.path.join(OUT, "hands.glb"), hands)
    print("CHARACTER_OK")
