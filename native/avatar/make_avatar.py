# make_avatar.py — 3D-модель аватара мини-ИИ и рендер кадров для CursorPad.
#
# Запуск (Blender 4.2+ / 5.x):
#   blender -b --factory-startup --python-exit-code 1 -P native/avatar/make_avatar.py -- --out <папка кадров>
# Необязательно: --states idle,thinking  --frames 0,6  --no-blend
#   --cpu [--samples 24] — Cycles на процессоре (видеокарта занята обучением; Eevee рисует на ней)
#
# Персонаж: парящий робот-компаньон. Тело и голова — суперэллипсоиды из
# перламутрового пластика, лицо — тёмный стеклянный экран. Глаза и рот на
# экране программа рисует сама поверх кадра (моргание, взгляд за курсором),
# поэтому здесь для них только точки привязки: face.json.
# Дальше native/avatar/pack_avatar.py собирает атлас и ai_avatar_face.h.
import bpy, bmesh, math, os, sys, json
from mathutils import Vector, Euler
from bpy_extras.object_utils import world_to_camera_view

HERE = os.path.dirname(os.path.abspath(__file__))
FRAME = 192      # размер кадра в атласе
SS = 2           # рендер вдвое крупнее, pack_avatar.py уменьшает
COLS = 24        # кадров на состояние: плавность при 25–30 к/с
STATES = ["idle", "listening", "thinking", "files", "wave", "success", "error", "talking", "sleep",
          "tell", "laugh", "wake"]
ONE_SHOT = {"success", "error", "wake"}

# цвет «настроения» на каждое состояние: антенна, ядро, кольца, глаза в программе
STATE_COLOR = {
    "idle": "38E1FF", "listening": "A98BFF", "thinking": "FFC857", "files": "38E1FF", "wave": "38E1FF",
    "success": "5CFFA8", "error": "FF5C7A", "talking": "38E1FF", "sleep": "2A9FB8",
    "tell": "FFC857", "laugh": "5CFFA8", "wake": "38E1FF",
}

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default
OUT = os.path.abspath(arg("--out", os.path.join(HERE, "frames")))
ONLY_STATES = arg("--states").split(",") if arg("--states") else STATES
ONLY_FRAMES = [int(f) for f in arg("--frames").split(",")] if arg("--frames") else list(range(COLS))
SAVE_BLEND = "--no-blend" not in argv
CYCLES = "--cpu" in argv


def lin(hexcolor):
    """sRGB hex → линейный RGB для Blender."""
    out = []
    for i in range(3):
        c = int(hexcolor[2 * i:2 * i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return out


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def ease_out(t):
    t = max(0.0, min(1.0, t))
    return 1 - (1 - t) ** 3


# ---------------------------------------------------------------- сцена

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
for engine in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
    try:
        scene.render.engine = engine
        break
    except TypeError:
        pass
scene.eevee.taa_render_samples = 64
for attr, value in (("use_raytracing", True), ("use_shadows", True)):
    if hasattr(scene.eevee, attr):
        setattr(scene.eevee, attr, value)
if CYCLES:
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = int(arg("--samples", "24"))
    scene.cycles.use_adaptive_sampling = True
    scene.cycles.use_denoising = True
    for attr, value in (("denoiser", "OPENIMAGEDENOISE"), ("max_bounces", 6), ("transparent_max_bounces", 12),
                        ("glossy_bounces", 3), ("diffuse_bounces", 2)):
        try:
            setattr(scene.cycles, attr, value)
        except (AttributeError, TypeError):
            pass
scene.render.resolution_x = scene.render.resolution_y = FRAME * SS
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.render.image_settings.color_depth = "8"
scene.view_settings.view_transform = "Standard"
scene.view_settings.look = "None"
scene.view_settings.exposure = 0.0


def node_tree(idblock):
    try:
        idblock.use_nodes = True
    except (AttributeError, TypeError):
        pass
    return idblock.node_tree


world = bpy.data.worlds.new("World")
scene.world = world
wt = node_tree(world)
bg = next((n for n in wt.nodes if n.type == "BACKGROUND"), None)
if bg is None:
    wt.nodes.clear()
    bg = wt.nodes.new("ShaderNodeBackground")
    wo = wt.nodes.new("ShaderNodeOutputWorld")
    wt.links.new(bg.outputs[0], wo.inputs["Surface"])
bg.inputs[0].default_value = (*lin("1A2033"), 1)
bg.inputs[1].default_value = 0.9


# ---------------------------------------------------------------- материалы

def fresh_material(name):
    m = bpy.data.materials.new(name)
    nt = node_tree(m)
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    return m, nt, out


def set_input(node, names, value):
    for name in names if isinstance(names, (list, tuple)) else [names]:
        if name in node.inputs:
            node.inputs[name].default_value = value
            return


def plastic(name, color, rough=0.35, coat=0.0, metal=0.0):
    m, nt, out = fresh_material(name)
    p = nt.nodes.new("ShaderNodeBsdfPrincipled")
    set_input(p, "Base Color", (*lin(color), 1))
    set_input(p, "Roughness", rough)
    set_input(p, "Metallic", metal)
    set_input(p, ["Coat Weight", "Clearcoat"], coat)
    set_input(p, ["Coat Roughness", "Clearcoat Roughness"], 0.06)
    set_input(p, ["Specular IOR Level", "Specular"], 0.55)
    nt.links.new(p.outputs[0], out.inputs["Surface"])
    m.diffuse_color = (*lin(color), 1)
    return m


GLOWS = []  # (emission node, коэффициент яркости) — цвет меняется по состоянию


def glow(name, color, strength, alpha=1.0, follows_state=True):
    m, nt, out = fresh_material(name)
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (*lin(color), 1)
    em.inputs["Strength"].default_value = strength
    if alpha < 1.0:
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        mix.inputs[0].default_value = alpha
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(em.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], out.inputs["Surface"])
        for attr, value in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
            try:
                setattr(m, attr, value)
            except (AttributeError, TypeError):
                pass
    else:
        nt.links.new(em.outputs[0], out.inputs["Surface"])
    if follows_state:
        GLOWS.append([em, strength])
    return m, em


def ground_glow_material():
    m, nt, out = fresh_material("GroundGlow")
    coord = nt.nodes.new("ShaderNodeTexCoord")
    grad = nt.nodes.new("ShaderNodeTexGradient")
    grad.gradient_type = "SPHERICAL"
    power = nt.nodes.new("ShaderNodeMath")
    power.operation = "POWER"
    power.inputs[1].default_value = 2.2
    scale = nt.nodes.new("ShaderNodeMath")
    scale.operation = "MULTIPLY"
    scale.inputs[1].default_value = 0.55
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (*lin("38E1FF"), 1)
    em.inputs["Strength"].default_value = 1.0
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(coord.outputs["Object"], grad.inputs["Vector"])
    nt.links.new(grad.outputs["Fac"], power.inputs[0])
    nt.links.new(power.outputs[0], scale.inputs[0])
    nt.links.new(scale.outputs[0], mix.inputs[0])
    nt.links.new(tr.outputs[0], mix.inputs[1])
    nt.links.new(em.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    for attr, value in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
        try:
            setattr(m, attr, value)
        except (AttributeError, TypeError):
            pass
    GLOWS.append([em, 1.0])
    return m, scale


M_PEARL = plastic("Pearl", "EEF1F7", rough=0.32, coat=0.55)
M_VISOR = plastic("Visor", "070A16", rough=0.06, coat=1.0)
M_VIOLET = plastic("Violet", "7B5CFF", rough=0.3, coat=0.4)
M_JOINT = plastic("Joint", "232842", rough=0.4)
M_PUFF = plastic("Puff", "9AA0B8", rough=0.95)
M_ORB, E_ORB = glow("Orb", "38E1FF", 1.6)
M_CORE, E_CORE = glow("Core", "38E1FF", 1.5)
M_RING, E_RING = glow("Ring", "38E1FF", 1.3)
M_RIM, E_RIM = glow("VisorRim", "38E1FF", 1.15)
M_HOLO, _ = glow("Holo", "38E1FF", 0.9, alpha=0.24, follows_state=False)
M_HOLO_LINE, _ = glow("HoloLine", "8FF0FF", 1.4, alpha=0.85, follows_state=False)
M_HOLO_SCAN, _ = glow("HoloScan", "E8FDFF", 1.6, alpha=0.9, follows_state=False)
M_SPARK, _ = glow("Spark", "FFE27A", 1.7, follows_state=False)
M_SPARK2, _ = glow("Spark2", "5CFFA8", 1.6, follows_state=False)
M_ZZZ, _ = glow("Zzz", "9FE9FF", 1.2, follows_state=False)
M_GROUND, GROUND_ALPHA = ground_glow_material()


# ---------------------------------------------------------------- геометрия

def link(name, data, parent=None, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), mat=None):
    ob = bpy.data.objects.new(name, data)
    scene.collection.objects.link(ob)
    ob.parent = parent
    ob.location = loc
    ob.rotation_euler = rot
    ob.scale = scale
    if mat is not None and data is not None:
        data.materials.append(mat)
    return ob


def empty(name, parent=None, loc=(0, 0, 0), rot=(0, 0, 0)):
    ob = link(name, None, parent, loc, rot)
    ob.empty_display_size = 0.1
    return ob


def bm_object(name, bm, parent=None, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), mat=None, smooth_all=True):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = smooth_all or abs(poly.normal.z) < 0.5
    return link(name, me, parent, loc, rot, scale, mat)


def sgnpow(v, e):
    return math.copysign(abs(v) ** e, v)


def superellipsoid(A, B, C, p, seg=72, rings=40, taper=None):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    for v in bm.verts:
        nx, ny, nz = v.co
        x, y, z = A * sgnpow(nx, 2 / p), B * sgnpow(ny, 2 / p), C * sgnpow(nz, 2 / p)
        if taper:
            k = taper(z)
            x, y = x * k, y * k
        v.co = (x, y, z)
    return bm


def torus(R, r, seg=56, sides=14):
    bm = bmesh.new()
    rows = []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        row = []
        for j in range(sides):
            b = 2 * math.pi * j / sides
            d = R + r * math.cos(b)
            row.append(bm.verts.new((d * math.cos(a), d * math.sin(a), r * math.sin(b))))
        rows.append(row)
    for i in range(seg):
        for j in range(sides):
            a, b = rows[i][j], rows[(i + 1) % seg][j]
            c, d = rows[(i + 1) % seg][(j + 1) % sides], rows[i][(j + 1) % sides]
            bm.faces.new((a, b, c, d))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def cylinder(r, depth, seg=48):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg, radius1=r, radius2=r, depth=depth)
    return bm


def sphere(r, seg=40, rings=24):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=r)
    return bm


def star(points=4, outer=1.0, inner=0.36, depth=0.12):
    bm = bmesh.new()
    ring = []
    for i in range(points * 2):
        a = math.pi * i / points + math.pi / 2
        rr = outer if i % 2 == 0 else inner
        ring.append(bm.verts.new((rr * math.cos(a), 0.0, rr * math.sin(a))))
    front = bm.faces.new(ring)
    ext = bmesh.ops.extrude_face_region(bm, geom=[front])
    moved = [g for g in ext["geom"] if isinstance(g, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=(0, depth, 0), verts=moved)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def add_bevel(ob, width, segments=4):
    mod = ob.modifiers.new("Bevel", "BEVEL")
    mod.width = width
    mod.segments = segments
    mod.limit_method = "ANGLE"
    return mod


# --- пропорции (локальные координаты HeadPivot: центр головы на HEAD_Z)
HEAD_A, HEAD_B, HEAD_C, HEAD_P = 0.80, 0.64, 0.66, 2.7
HEAD_Z = 0.70
VISOR_A, VISOR_B, VISOR_N, VISOR_DZ = 0.60, 0.42, 4.0, -0.03
VISOR_LIFT = 0.016


def head_front_y(x, dz, lift=VISOR_LIFT):
    """y передней поверхности головы (смотрит на −Y) в точке x, dz от центра."""
    s = 1 - abs(x / HEAD_A) ** HEAD_P - abs(dz / HEAD_C) ** HEAD_P
    return -(HEAD_B * max(s, 0.0) ** (1 / HEAD_P)) - lift


def visor_mesh(rings=26, seg=144):
    bm = bmesh.new()
    center = bm.verts.new((0, head_front_y(0, VISOR_DZ), HEAD_Z + VISOR_DZ))
    grid = []
    for k in range(1, rings + 1):
        rr = k / rings
        row = []
        for j in range(seg):
            a = 2 * math.pi * j / seg
            x = rr * VISOR_A * sgnpow(math.cos(a), 2 / VISOR_N)
            dz = VISOR_DZ + rr * VISOR_B * sgnpow(math.sin(a), 2 / VISOR_N)
            row.append(bm.verts.new((x, head_front_y(x, dz), HEAD_Z + dz)))
        grid.append(row)
    for j in range(seg):
        bm.faces.new((center, grid[0][j], grid[0][(j + 1) % seg]))
    for k in range(rings - 1):
        for j in range(seg):
            bm.faces.new((grid[k][j], grid[k + 1][j], grid[k + 1][(j + 1) % seg], grid[k][(j + 1) % seg]))
    bm.normal_update()
    if sum(f.normal.y for f in bm.faces) > 0:  # лицевая сторона смотрит в камеру (−Y)
        bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
    border = [tuple(v.co) for v in grid[-1]]
    return bm, border


def rim_curve(name, points, parent, mat, depth=0.014):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = depth
    cu.bevel_resolution = 3
    sp = cu.splines.new("POLY")
    sp.points.add(len(points) - 1)
    for p, co in zip(sp.points, points):
        p.co = (co[0], co[1] - 0.004, co[2], 1)
    sp.use_cyclic_u = True
    return link(name, cu, parent, mat=mat)


def zed(name, parent, mat):
    cu = bpy.data.curves.new(name, "FONT")
    cu.body = "Z"
    cu.size = 0.34
    cu.extrude = 0.025
    cu.bevel_depth = 0.006
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    return link(name, cu, parent, rot=(math.radians(90), 0, 0), mat=mat)


root = empty("Root")
ground = bm_object("GroundGlow", bmesh.new(), loc=(0, 0, -1.28))
bmesh_disc = bmesh.new()
bmesh.ops.create_circle(bmesh_disc, cap_ends=True, cap_tris=False, segments=64, radius=1.0)
bmesh_disc.to_mesh(ground.data)
bmesh_disc.free()
ground.data.materials.append(M_GROUND)
ground.scale = (1.15, 1.15, 1)

# тело: яйцо, сужается книзу
body = bm_object("Body", superellipsoid(0.52, 0.46, 0.60, 2.3,
                                         taper=lambda z: 1 - 0.30 * max(0.0, -z / 0.60) ** 1.4),
                 root, loc=(0, 0, -0.30), mat=M_PEARL)
core_y = -0.452
bm_object("Core", sphere(0.11), root, loc=(0, core_y, -0.20), scale=(1, 0.32, 1), mat=M_CORE)
bm_object("CoreRing", torus(0.145, 0.026), root, loc=(0, core_y + 0.02, -0.20), rot=(math.radians(90), 0, 0), mat=M_VIOLET)
bm_object("HoverRing", torus(0.25, 0.03), root, loc=(0, 0, -0.97), mat=M_RING)
bm_object("NeckRing", torus(0.22, 0.034), root, loc=(0, 0, 0.36), mat=M_JOINT)
bm_object("NeckGlow", torus(0.22, 0.012, sides=8), root, loc=(0, -0.008, 0.36), scale=(1.06, 1.06, 1), mat=M_RING)

# голова
head_pivot = empty("HeadPivot", root, loc=(0, 0, 0.36))
bm_object("Head", superellipsoid(HEAD_A, HEAD_B, HEAD_C, HEAD_P), head_pivot, loc=(0, 0, HEAD_Z), mat=M_PEARL)
visor_bm, visor_border = visor_mesh()
visor = bm_object("Visor", visor_bm, head_pivot, mat=M_VISOR)
solid = visor.modifiers.new("Solidify", "SOLIDIFY")
solid.thickness = 0.03
solid.offset = 1.0
rim_curve("VisorRim", visor_border, head_pivot, M_RIM)
for side in (-1, 1):
    pod = bm_object("Pod" + ("L" if side < 0 else "R"), cylinder(0.19, 0.16), head_pivot,
                    loc=(side * (HEAD_A - 0.02), 0, HEAD_Z - 0.02), rot=(0, math.radians(90), 0), mat=M_VIOLET,
                    smooth_all=False)
    add_bevel(pod, 0.04)
    bm_object("PodGlow" + ("L" if side < 0 else "R"), torus(0.11, 0.022), head_pivot,
              loc=(side * (HEAD_A + 0.065), 0, HEAD_Z - 0.02), rot=(0, math.radians(90), 0), mat=M_RING)
antenna = empty("AntennaPivot", head_pivot, loc=(0.20, 0, HEAD_Z + 0.61), rot=(0, math.radians(12), 0))
bm_object("AntennaStem", cylinder(0.022, 0.30, seg=16), antenna, loc=(0, 0, 0.15), mat=M_JOINT)
bm_object("AntennaOrb", sphere(0.085), antenna, loc=(0, 0, 0.33), mat=M_ORB)

# точки привязки лица: глаза и рот рисует программа
FACE_POINTS = {
    "eyeL": (-0.235, HEAD_Z + 0.05),
    "eyeR": (0.235, HEAD_Z + 0.05),
    "mouth": (0.0, HEAD_Z - 0.20),
}
face_empties = {}
for key, (x, z) in FACE_POINTS.items():
    face_empties[key] = empty("Face_" + key, head_pivot, loc=(x, head_front_y(x, z - HEAD_Z) - 0.02, z))

# руки: парящие перчатки — купол ладони над фиолетовым браслетом и большой палец
# к телу (раньше браслет сидел на самом верху шара, и кисть читалась пустым стаканчиком)
HAND_REST = {-1: Vector((-0.80, -0.06, -0.30)), 1: Vector((0.80, -0.06, -0.30))}
hands = {}
for side in (-1, 1):
    tag = "L" if side < 0 else "R"
    pivot = empty("Hand" + tag, root, loc=HAND_REST[side])
    bm_object("Mitt" + tag, sphere(1.0), pivot, loc=(0, 0, -0.01), scale=(0.155, 0.125, 0.175), mat=M_PEARL)
    bm_object("Thumb" + tag, sphere(1.0, 24, 14), pivot, loc=(-side * 0.115, -0.045, 0.01), rot=(0, side * 0.5, 0),
              scale=(0.055, 0.05, 0.075), mat=M_PEARL)
    bm_object("Cuff" + tag, torus(0.135, 0.03), pivot, loc=(0, 0, 0.085), scale=(1, 0.82, 1), mat=M_VIOLET)
    bm_object("CuffGlow" + tag, torus(0.135, 0.009, sides=8), pivot, loc=(0, -0.005, 0.085), scale=(1.05, 0.86, 1),
              mat=M_RING)
    hands[side] = pivot

# эффекты состояний
orbit = empty("Orbit", head_pivot, loc=(0, 0, HEAD_Z), rot=(math.radians(16), 0, 0))
orbit_dots = [bm_object("OrbitDot%d" % k, sphere(0.06), orbit, mat=M_ORB) for k in range(3)]

doc = empty("Doc", root, loc=(0, -1.0, -0.06), rot=(math.radians(-8), 0, 0))
plane = bmesh.new()
quad = [plane.verts.new(co) for co in ((-0.36, 0, -0.46), (0.36, 0, -0.46), (0.36, 0, 0.46), (-0.36, 0, 0.46))]
plane.faces.new(quad)
bm_object("DocPlane", plane, doc, mat=M_HOLO)
doc_lines = []
for i, (w, z) in enumerate(((0.46, 0.33), (0.52, 0.19), (0.40, 0.07), (0.50, -0.05), (0.30, -0.17), (0.46, -0.29))):
    bar = bmesh.new()
    bmesh.ops.create_cube(bar, size=1.0)
    doc_lines.append(bm_object("DocLine%d" % i, bar, doc, loc=(-0.26 + w / 2, -0.01, z),
                               scale=(w, 0.004, 0.032 if i else 0.05), mat=M_HOLO_LINE))
scan = bmesh.new()
bmesh.ops.create_cube(scan, size=1.0)
doc_scan = bm_object("DocScan", scan, doc, loc=(0, -0.015, 0), scale=(0.70, 0.004, 0.018), mat=M_HOLO_SCAN)

sparks = [bm_object("Spark%d" % k, star(), root, mat=M_SPARK if k % 2 == 0 else M_SPARK2) for k in range(8)]
puffs = [bm_object("Puff%d" % k, sphere(1.0, 24, 14), root, mat=M_PUFF) for k in range(3)]
zeds = [zed("Zed%d" % k, root, M_ZZZ) for k in range(3)]

EFFECTS = {
    "thinking": orbit_dots,
    "files": [ob for ob in bpy.data.objects if ob.name.startswith("Doc") and ob.type == "MESH"],
    "success": sparks,
    "error": puffs,
    "sleep": zeds,
}

# ---------------------------------------------------------------- свет и камера

def aim(ob, target):
    ob.rotation_euler = (Vector(target) - ob.location).to_track_quat("-Z", "Y").to_euler()


def area(name, loc, size, energy, color):
    data = bpy.data.lights.new(name, "AREA")
    data.shape = "DISK"
    data.size = size
    data.energy = energy
    data.color = color
    ob = link(name, data, loc=loc)
    aim(ob, (0, 0, 0.5))
    return ob


area("Key", (-3.6, -5.0, 5.2), 4.0, 1250, (1.0, 0.97, 0.93))
area("Fill", (4.6, -4.0, 1.2), 4.0, 600, (0.86, 0.92, 1.0))
area("Rim", (2.6, 4.6, 3.8), 3.0, 1600, (0.70, 0.86, 1.0))
area("Top", (0.0, -2.4, 6.0), 5.0, 450, (1.0, 1.0, 1.0))

TARGET = Vector((0, 0, 0.56))
cam_data = bpy.data.cameras.new("Camera")
cam_data.lens = 65
cam_data.sensor_width = 36
camera = link("Camera", cam_data)
az, el, dist = math.radians(14), math.radians(9), 7.35
camera.location = TARGET + dist * Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
aim(camera, TARGET)
scene.camera = camera

# ---------------------------------------------------------------- позы


def rest():
    root.location = (0, 0, 0)
    root.rotation_euler = (0, 0, 0)
    head_pivot.rotation_euler = (0, 0, 0)
    antenna.rotation_euler = (0, math.radians(12), 0)
    for side, pivot in hands.items():
        pivot.location = HAND_REST[side].copy()
        pivot.rotation_euler = (0, 0, 0)
    for group in EFFECTS.values():
        for ob in group:
            ob.hide_render = True
    ground.scale = (1.15, 1.15, 1)
    GROUND_ALPHA.inputs[1].default_value = 0.55


def deg(*xyz):
    return Euler([math.radians(v) for v in xyz])


def set_hand(side, x, y, z, rot=(0, 0, 0)):
    hands[side].location = (x, y, z)
    hands[side].rotation_euler = deg(*rot)


def mood(state, pulse):
    color = lin(STATE_COLOR[state])
    for node, base in GLOWS:
        node.inputs["Color"].default_value = (*color, 1)
        node.inputs["Strength"].default_value = base * pulse


def pose(state, i):
    rest()
    t = i / COLS                      # петля
    u = i / (COLS - 1)                # однократная анимация 0..1
    w = 2 * math.pi * t
    pulse = 1.0
    bob = 0.05 * math.sin(w)
    if state == "idle":
        root.location.z = bob
        root.rotation_euler = deg(0, 0, 3 * math.sin(w + 0.6))
        head_pivot.rotation_euler = deg(2 * math.sin(2 * w), 3 * math.sin(w + 1.0), 0)
        for side in (-1, 1):
            r = HAND_REST[side]
            set_hand(side, r.x + side * 0.02 * math.sin(w), r.y, r.z + 0.05 * math.sin(w + 0.9 + side * 0.4), (0, side * 6, 0))
        antenna.rotation_euler = deg(0, 12 + 6 * math.sin(w + 1.6), 0)
        pulse = 0.85 + 0.25 * (0.5 + 0.5 * math.sin(w))
    elif state == "listening":
        root.location.z = 0.03 * math.sin(w)
        root.rotation_euler = deg(6, 0, -4)
        head_pivot.rotation_euler = deg(4, 11 + 2 * math.sin(w), -5)
        set_hand(1, 1.02, -0.10, 0.92 + 0.03 * math.sin(2 * w), (0, -25, 0))
        set_hand(-1, -0.78, -0.10, -0.25 + 0.03 * math.sin(w), (0, -6, 0))
        antenna.rotation_euler = deg(0, 4 + 4 * math.sin(2 * w), 0)
        pulse = 0.8 + 0.5 * (0.5 + 0.5 * math.sin(2 * w))
    elif state == "thinking":
        root.location.z = 0.035 * math.sin(w)
        head_pivot.rotation_euler = deg(-9, -6, 9 * math.sin(w))
        set_hand(1, 0.30, -0.62, 0.38 + 0.02 * math.sin(2 * w), (20, 0, 15))
        set_hand(-1, -0.42, -0.50, -0.18, (0, 0, -20))
        for k, dot in enumerate(orbit_dots):
            a = w + k * 2 * math.pi / 3
            dot.hide_render = False
            dot.location = (0.88 * math.cos(a), 0.80 * math.sin(a), 0.86)
            dot.scale = [0.8 + 0.35 * (0.5 + 0.5 * math.sin(a * 2))] * 3
        antenna.rotation_euler = deg(0, 12 + 3 * math.sin(2 * w), 0)
        pulse = 0.7 + 0.8 * (0.5 + 0.5 * math.sin(2 * w))
    elif state == "files":
        root.location.z = 0.02 * math.sin(w)
        head_pivot.rotation_euler = deg(12, 0, 6 * math.sin(w))
        for side in (-1, 1):
            tap = 0.035 * max(0.0, math.sin(2 * w + (0 if side < 0 else math.pi)))
            set_hand(side, side * 0.46, -0.92, -0.10 + tap, (0, side * 20, 0))
        for ob in EFFECTS["files"]:
            ob.hide_render = False
        doc_scan.location.z = 0.42 - 0.84 * t
        doc.location.z = -0.06 + 0.02 * math.sin(w)
        pulse = 1.0 + 0.2 * math.sin(2 * w)
    elif state == "wave":
        root.location.z = bob
        head_pivot.rotation_euler = deg(-3, 7, -4)
        swing = math.sin(2 * w)
        set_hand(1, 0.98 + 0.08 * swing, -0.16, 1.12, (0, -22 * swing, 0))
        set_hand(-1, -0.80, -0.06, -0.28 + 0.03 * math.sin(w), (0, -6, 0))
        antenna.rotation_euler = deg(0, 12 - 8 * swing, 0)
        pulse = 1.1
    elif state == "success":
        hop = 0.26 * math.sin(math.pi * min(1.0, u * 1.55))
        root.location.z = hop
        lift = ease_out(u * 2.2)
        for side in (-1, 1):
            r = HAND_REST[side]
            set_hand(side, r.x + side * 0.22 * lift, r.y - 0.08, r.z + 1.40 * lift, (0, side * -30 * lift, 0))
        head_pivot.rotation_euler = deg(-10 * lift, 0, 0)
        antenna.rotation_euler = deg(0, 12 - 14 * math.sin(math.pi * u), 0)
        for k, sp in enumerate(sparks):
            a = 2 * math.pi * k / len(sparks) + 0.3
            radius = 0.55 + 0.52 * ease_out(u)
            sp.hide_render = False
            sp.location = (radius * math.cos(a) * 1.1, -0.75, 1.0 + hop + radius * math.sin(a) * 0.8)
            s = 0.13 * math.sin(math.pi * min(1.0, u * 1.08)) * (1.0 if k % 2 else 0.75)
            sp.scale = (max(s, 0.001),) * 3
            sp.rotation_euler = deg(0, 90 * u + k * 20, 0)
        pulse = 1.35
    elif state == "error":
        shake = 0.07 * math.sin(2 * math.pi * 4 * u) * (1 - u)
        slump = smooth(u * 1.4)
        root.location = (shake, 0, -0.06 * slump)
        head_pivot.rotation_euler = deg(13 * slump, -8 * slump, 14 * math.sin(2 * math.pi * 4 * u) * (1 - u))
        for side in (-1, 1):
            r = HAND_REST[side]
            set_hand(side, r.x - side * 0.08 * slump, r.y, r.z - 0.26 * slump, (0, side * 10, 0))
        antenna.rotation_euler = deg(0, 12 + 30 * slump, 0)
        for k, pf in enumerate(puffs):
            p = max(0.0, min(1.0, u * 1.25 - k * 0.12))
            pf.hide_render = p <= 0
            pf.location = (0.28 + 0.16 * k, -0.15, 1.78 + 0.55 * p + 0.06 * k)
            pf.scale = (max(0.001, 0.15 * math.sin(math.pi * p) * (1 + 0.25 * k)),) * 3
        pulse = 1.0 + 0.4 * abs(math.sin(2 * math.pi * 3 * u))
    elif state == "talking":
        root.location.z = 0.04 * math.sin(w)
        head_pivot.rotation_euler = deg(3 * math.sin(2 * w), 4 * math.sin(w), 3 * math.sin(w + 1))
        for side in (-1, 1):
            g = max(0.0, math.sin(w + (0 if side > 0 else math.pi)))
            r = HAND_REST[side]
            set_hand(side, r.x + side * 0.06 * g, -0.30 * g - 0.06, r.z + 0.30 * g, (0, side * -25 * g, 0))
        antenna.rotation_euler = deg(0, 12 + 5 * math.sin(2 * w), 0)
        pulse = 0.95 + 0.25 * abs(math.sin(2 * w))
    elif state == "sleep":
        root.location.z = -0.12 + 0.025 * math.sin(w)
        root.rotation_euler = deg(4, 0, 0)
        head_pivot.rotation_euler = deg(15 + 2 * math.sin(w), 8, 0)
        for side in (-1, 1):
            r = HAND_REST[side]
            set_hand(side, r.x - side * 0.05, r.y, r.z - 0.16 + 0.02 * math.sin(w), (0, side * 12, 0))
        antenna.rotation_euler = deg(0, 38, 0)
        for k, z in enumerate(zeds):
            p = (t + k / 3) % 1.0
            z.hide_render = False
            z.location = (0.55 + 0.55 * p, -0.45, 1.55 + 0.75 * p)
            z.scale = (max(0.001, math.sin(math.pi * p) * (0.6 + 0.6 * p)),) * 3
            z.rotation_euler = deg(90, 0, -12 + 18 * p)
        ground.scale = (1.0, 1.0, 1)
        pulse = 0.45 + 0.15 * math.sin(w)
    elif state == "tell":
        # «Кстати!» — поднял ладонь у головы, покачивает ей, антенна светится ярче
        root.location.z = 0.04 * math.sin(w)
        root.rotation_euler = deg(0, 0, -5)
        head_pivot.rotation_euler = deg(-4 + 2 * math.sin(2 * w), -7, 5 * math.sin(w))
        wag = math.sin(2 * w)
        set_hand(1, 0.86, -0.32, 0.95 + 0.04 * wag, (0, -18 + 10 * wag, 0))
        set_hand(-1, -0.72, -0.20, -0.18 + 0.04 * math.sin(w + 1), (0, -14, 0))
        antenna.rotation_euler = deg(0, 12 + 9 * wag, 0)
        pulse = 1.25 + 0.25 * (0.5 + 0.5 * math.sin(2 * w))
    elif state == "laugh":
        # смеётся: подпрыгивает мелко, голова запрокинута, руки у живота
        shake = abs(math.sin(3 * w))
        root.location.z = 0.03 + 0.07 * shake
        root.rotation_euler = deg(0, 4 * math.sin(3 * w), 0)
        head_pivot.rotation_euler = deg(-14 - 4 * shake, 0, 6 * math.sin(w))
        for side in (-1, 1):
            set_hand(side, side * 0.50, -0.62, -0.42 + 0.05 * shake, (25, side * 30, 0))
        antenna.rotation_euler = deg(0, 12 + 16 * math.sin(3 * w), 0)
        pulse = 1.1 + 0.3 * shake
    elif state == "wake":
        # потягивается после сна: из позы сна вверх, руки над головой, и обратно
        stretch = math.sin(math.pi * min(1.0, u * 1.15))
        settle = smooth(u * 1.6)
        root.location.z = -0.12 * (1 - settle) + 0.10 * stretch
        root.rotation_euler = deg(4 * (1 - settle), 0, 0)
        head_pivot.rotation_euler = deg(15 * (1 - settle) - 12 * stretch, 8 * (1 - settle), 0)
        for side in (-1, 1):
            r = HAND_REST[side]
            set_hand(side, r.x * (1 + 0.12 * stretch), r.y - 0.30 * stretch, r.z + 2.0 * stretch,
                     (0, side * -25 * stretch, 0))
        antenna.rotation_euler = deg(0, 38 * (1 - settle) + 12 * settle - 10 * stretch, 0)
        pulse = 0.45 + 0.75 * settle
    # тень-свечение под ним тускнеет, когда он выше
    GROUND_ALPHA.inputs[1].default_value = 0.55 * (1 - 0.9 * max(0.0, root.location.z))
    mood(state, pulse)


def face_anchor():
    bpy.context.view_layer.update()
    out = {}
    for key, ob in face_empties.items():
        p = world_to_camera_view(scene, camera, ob.matrix_world.translation)
        out[key] = [round(p.x * FRAME, 3), round((1 - p.y) * FRAME, 3)]
    return out


# ---------------------------------------------------------------- рендер

os.makedirs(OUT, exist_ok=True)
face_path = os.path.join(OUT, "face.json")
faces = {}
if os.path.exists(face_path):
    with open(face_path, encoding="utf-8") as f:
        faces = json.load(f)
for row, state in enumerate(STATES):
    if state not in ONLY_STATES:
        continue
    faces.setdefault(state, [None] * COLS)
    for i in ONLY_FRAMES:
        pose(state, i)
        faces[state][i] = face_anchor()
        scene.render.filepath = os.path.join(OUT, "%s_%02d.png" % (state, i))
        bpy.ops.render.render(write_still=True)
        print("avatar frame", state, i, flush=True)
with open(face_path, "w", encoding="utf-8") as f:
    json.dump({"frame": FRAME, "cols": COLS, "states": STATES, "colors": STATE_COLOR, **faces}, f, ensure_ascii=False, indent=1)

if SAVE_BLEND:
    pose("idle", 0)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "mini_ai_avatar.blend"), compress=True)
print("avatar done", OUT, flush=True)
