import trimesh
import math
from shapely.geometry import Polygon

OUT = r"F:\workspace\Callender"
T = 2.5
RX, RY, RZ = 66.0, 64.0, 36.0
FZ = RZ / 2.0
R = 8.0

PILLARS = [(22, 22), (-22, 22), (22, -22), (-22, -22)]

def remesh(mesh):
    mesh = mesh.copy()
    mesh.update_faces(mesh.nondegenerate_faces())
    mesh.remove_unreferenced_vertices()
    mesh.fix_normals()
    return mesh

def box(ext, center=(0, 0, 0)):
    m = trimesh.primitives.Box(extents=ext)
    m.apply_translation(center)
    return m

def _cyl_axis(r, h, center, axis, sections=48):
    m = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    if axis == (1, 0, 0):
        m.apply_transform(trimesh.transformations.rotation_matrix(
            math.pi / 2, (0, 1, 0)))
    elif axis == (0, 1, 0):
        m.apply_transform(trimesh.transformations.rotation_matrix(
            math.pi / 2, (1, 0, 0)))
    m.apply_translation(center)
    return m

def cyl(r, h, center=(0, 0, 0), sections=48, axis=(0, 0, 1)):
    """Cylinder with axis along +Z (default) or rotated to X / Y."""
    m = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    if axis == (1, 0, 0):
        m.apply_transform(trimesh.transformations.rotation_matrix(
            math.pi / 2, (0, 1, 0)))
    elif axis == (0, 1, 0):
        m.apply_transform(trimesh.transformations.rotation_matrix(
            math.pi / 2, (1, 0, 0)))
    m.apply_translation(center)
    return m

def cyl_y(r, h, center=(0, 0, 0), sections=48):
    """Cylinder with its axis along Y (for floor standoffs)."""
    m = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    m.apply_transform(trimesh.transformations.rotation_matrix(
        math.pi / 2, [1, 0, 0]))
    m.apply_translation(center)
    return m

def sphere(r, center=(0, 0, 0)):
    m = trimesh.primitives.Sphere(radius=r)
    m.apply_translation(center)
    return m

def rounded_box(ext, r, center=(0, 0, 0)):
    """Box with seamless rounded edges and corners (outer size = ext)."""
    cx, cy, cz = center
    hx, hy, hz = ext[0] / 2 - r, ext[1] / 2 - r, ext[2] / 2 - r
    parts = [box([ext[0] - 2 * r, ext[1] - 2 * r, ext[2] - 2 * r], center)]
    for sy in (-1, 1):
        for sz in (-1, 1):
            parts.append(cyl(r, ext[0] - 2 * r, (cx, cy + sy * hy, cz + sz * hz), axis=(1, 0, 0)))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(cyl(r, ext[1] - 2 * r, (cx + sx * hx, cy, cz + sz * hz), axis=(0, 1, 0)))
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append(cyl(r, ext[2] - 2 * r, (cx + sx * hx, cy + sy * hy, cz)))
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                parts.append(sphere(r, (cx + sx * hx, cy + sy * hy, cz + sz * hz)))
    return remesh(trimesh.boolean.union(parts, engine="manifold"))

def rounded_rect_poly(w, h, r, n=32):
    """2D rounded-rect outline as a shapely polygon (r <= min(w,h)/2)."""
    cx, cy = w / 2 - r, h / 2 - r
    pts = []
    for sx, sy, a0 in ((1, 1, 0.0), (-1, 1, math.pi / 2),
                       (-1, -1, math.pi), (1, -1, 1.5 * math.pi)):
        for i in range(n + 1):
            a = a0 + (i / n) * (math.pi / 2)
            pts.append((sx * cx + r * math.cos(a),
                        sy * cy + r * math.sin(a)))
    return Polygon(pts)

def union(shapes):
    if len(shapes) == 1:
        return remesh(shapes[0])
    return remesh(trimesh.boolean.union(shapes, engine="manifold"))

def subtract(a, bs):
    if not bs:
        return remesh(a)
    return remesh(trimesh.boolean.difference([a] + bs, engine="manifold"))

### BACK TRAY (rounded box) ---------------------------------------------------
outer = rounded_box((RX, RY, RZ), R)
inner = rounded_box((RX - 2 * T, RY - 2 * T, RZ - 2 * T), R - T)
tray = subtract(outer, [inner])

# The front (z+) face is open: the front PANEL forms that wall. Cut away
# everything at z > 15.5 within the rounded-rect silhouette (66x64 r8), so
# the sides, top, bottom and back remain solid walls of the tray.
front_cut = trimesh.creation.extrude_polygon(
    rounded_rect_poly(RX, RY, R), height=50)
front_cut.apply_translation((0, 0, FZ - T))  # extrude starts at z=0; spans 15.5 .. 65.5
tray = subtract(tray, [front_cut])

# ESP32 board stands VERTICALLY against the BACK (interior) wall, long axis
# along X, so its USB port on the RIGHT edge faces the RIGHT side wall and
# the cable exits a slot in that wall. Board plane z ~ -8.5, clear of the
# speaker ring (which occupies z < -9.5) and of the corner pillars.
stands, holes = [], []
for bx, by in ((-21, -12), (-21, 12), (21, -12), (21, 12)):
    stands.append(cyl(3.2, 7.0, (bx, by, -12.0)))
    holes.append(cyl(1.7, 9.0, (bx, by, -12.0)))
stand_group = union(stands)
hole_group = union(holes)
tray = union([tray, stand_group])
tray = subtract(tray, [hole_group])

# Cable slot through the RIGHT wall at the USB port (board right edge).
cable = box((12, 14, 16), (31.0, 0, -7.5))
tray = subtract(tray, [cable])

# Mounting posts with M3 insert pocket toward the front.
posts, holes = [], []
for px, py in PILLARS:
    posts.append(cyl(8.0, RZ, (px, py, 0)))
    holes.append(cyl(3.4, RZ, (px, py, 0)))
    holes.append(cyl(4.0, 10.0, (px, py, 7.0)))
    holes.append(cyl(6.0, 3.0, (px, py, FZ - 1.5)))
tray = union([tray, union(posts)])
tray = subtract(tray, holes)

# Speaker opening: full circular perforated grille (any shape worked, chose
# concentric ring-of-holes speaker look) + 40 mm seat ring on the back wall.
gr_cy = 10
gx0, gy0 = 0, gr_cy
gr_holes = []
for rr in (0.0, 6.5, 11.0, 15.0):
    if rr == 0.0:
        gr_holes.append(cyl(3.0, 8.0, (gx0, gy0, -15.5)))
        continue
    n = max(4, int(2 * math.pi * rr / 10)) if rr < 14 else int(rr / 5)
    for k in range(n):
        a = 2 * math.pi * k / n
        gr_holes.append(cyl(3.0, 8.0,
                            (gx0 + rr * math.cos(a), gy0 + rr * math.sin(a), -15.5)))
tray = subtract(tray, gr_holes)

ring_outer = cyl(20.0, 3.2, (0, gr_cy, -11.6))
ring_inner = cyl(13.5, 5.0, (0, gr_cy, -12.0))
tray = union([tray, ring_outer])
tray = subtract(tray, [ring_inner])

### FRONT PLATE (flat panel, rounded-rect outline) -----------------------------
pc = (0, 0, FZ - T / 2)
plate = trimesh.creation.extrude_polygon(
    rounded_rect_poly(RX, RY, R), height=T)
plate.apply_translation((0, 0, pc[2]))

cut = []
oled = box((28, 18, T + 6), (0, 2, pc[2]))
cut.append(oled)
tof = box((14, 10, T + 6), (0, 20, pc[2]))
cut.append(tof)
for px, py in PILLARS:
    cut.append(cyl(3.4, T + 6, (px, py, pc[2])))
    cut.append(cyl(6.0, 1.6, (px, py, FZ - 0.9)))
plate = subtract(plate, cut)

file_tray = OUT + r"\deskpet_housing_back.stl"
file_plate = OUT + r"\deskpet_housing_front.stl"

tray = remesh(tray)
plate = remesh(plate)
print("tray  watertight:", tray.is_watertight, "vol:", round(tray.volume, 1))
print("plate watertight:", plate.is_watertight, "vol:", round(plate.volume, 1))
print("tray ext :", [round(float(v), 1) for v in tray.extents])
print("plate ext:", [round(float(v), 1) for v in plate.extents])

tray.export(file_tray)
plate.export(file_plate)
print("Back :", file_tray)
print("Front:", file_plate)