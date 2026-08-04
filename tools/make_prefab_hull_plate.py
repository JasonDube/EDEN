#!/usr/bin/env python3
"""Emit assets/models/prefabs/hull_plate.lime -- a riveted hull plate, authored
from a pixel-art reference: two navy panel bands over a dark seam backing, a
worn light strip along the top edge, brass bolts at the band corners, and
ink-dark recesses where the paint gave up.

Same contract as the other make_prefab scripts: this runs ONCE and its output
is the deliverable. Reshape hull_plate.lime in LIME from here on; rerun this
only to get the plain original back.

The reference is a texture. This is not a texture -- every panel is raised
geometry, every bolt head sticks out of the surface, every recess sits proud
or flush by a few millimetres. Vertex colour carries the palette, so it needs
no image file and Painter can still repaint it later.
"""
import math

VERTS, FACES = [], []

def box(centre, half, colour):
    """A box with four vertices per face, so each face keeps its own normal.

    Sharing eight corners would average the normals and smear a hard-edged
    plate into something soft. Twenty-four vertices is the cost of an edge
    reading as an edge."""
    cx, cy, cz = centre
    hx, hy, hz = half
    faces = [
        ((0, 0, 1),  [(-hx,-hy, hz), ( hx,-hy, hz), ( hx, hy, hz), (-hx, hy, hz)]),
        ((0, 0,-1),  [( hx,-hy,-hz), (-hx,-hy,-hz), (-hx, hy,-hz), ( hx, hy,-hz)]),
        ((1, 0, 0),  [( hx,-hy, hz), ( hx,-hy,-hz), ( hx, hy,-hz), ( hx, hy, hz)]),
        ((-1,0, 0),  [(-hx,-hy,-hz), (-hx,-hy, hz), (-hx, hy, hz), (-hx, hy,-hz)]),
        ((0, 1, 0),  [(-hx, hy, hz), ( hx, hy, hz), ( hx, hy,-hz), (-hx, hy,-hz)]),
        ((0,-1, 0),  [(-hx,-hy,-hz), ( hx,-hy,-hz), ( hx,-hy, hz), (-hx,-hy, hz)]),
    ]
    for normal, corners in faces:
        base = len(VERTS)
        for uv, corner in zip([(0,0),(1,0),(1,1),(0,1)], corners):
            x, y, z = corner
            VERTS.append(((cx + x, cy + y, cz + z), normal, uv, colour))
        FACES.append([base, base + 1, base + 2, base + 3])

# ---------------------------------------------------------------------------
# The palette, read off the reference. Navy hull, near-black seams, one worn
# highlight, brass where a bolt catches the light, ink where nothing does.
# ---------------------------------------------------------------------------
SEAM  = (0.075, 0.080, 0.110, 1.0)   # the dark gutter between bands
NAVY  = (0.165, 0.180, 0.240, 1.0)   # the plate itself
LIGHT = (0.340, 0.360, 0.440, 1.0)   # worn strip along the top edge
GOLD  = (0.760, 0.600, 0.220, 1.0)   # brass bolt heads
GRAY  = (0.300, 0.310, 0.380, 1.0)   # steel rivets, duller than brass
INK   = (0.050, 0.055, 0.080, 1.0)   # recesses

# ---------------------------------------------------------------------------
# The plate. Two metres wide, a metre and a half tall, standing on its foot.
# Backing slab in seam-colour; two raised bands in navy; details ride the
# band faces, each a few millimetres prouder than the last so nothing
# z-fights with anything.
# ---------------------------------------------------------------------------
box((0.0, 0.75, 0.0), (1.00, 0.75, 0.035), SEAM)          # backing slab

BANDS = [(0.37, 0.33), (1.13, 0.33)]                       # (centre y, half y)
for cy, hy in BANDS:
    box((0.0, cy, 0.030), (0.97, hy, 0.030), NAVY)         # raised band

box((0.0, 1.42, 0.033), (0.97, 0.04, 0.031), LIGHT)        # worn top strip

def bolt(x, y, colour, r=0.040):
    box((x, y, 0.060), (r, r, 0.012), colour)              # proud of the band

def recess(x, y, r=0.045):
    box((x, y, 0.058), (r, r, 0.004), INK)                 # barely raised ink

# Upper band: brass at the top corners, a recess low on the left,
# a rivet trio at its heart -- steel, brass, steel.
bolt(-0.88, 1.28, GOLD)
bolt( 0.88, 1.28, GOLD)
recess(-0.45, 0.92)
bolt(-0.12, 1.08, GRAY, r=0.030)
bolt( 0.00, 1.08, GOLD, r=0.030)
bolt( 0.12, 1.08, GRAY, r=0.030)

# Lower band: brass at the top corners, ink in the bottom corners,
# one stray recess where a fitting used to be.
bolt(-0.88, 0.60, GOLD)
bolt( 0.88, 0.60, GOLD)
recess(-0.88, 0.14)
recess( 0.88, 0.14)
recess( 0.25, 0.30)

# ---------------------------------------------------------------------------
# Ports: it mounts flat on whatever it is protecting.
# ---------------------------------------------------------------------------
PORTS = [
    ("deck_mount", (0.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)),
]

META = {
    "role":    "",
    "surface_mount": "1",        # walls are where hull plate belongs
    "catalog": "structure",
    "title":   "Riveted Hull Plate",
    "price":   "250",
    "mass":    "150",
    "summary": "Two navy panel bands, brass-bolted. Armor for the eye, not the ledger.",
    "mount":   "deck_mount",
    "prefab":  "1",
}

# ---------------------------------------------------------------------------
# HALF-EDGES. Not optional -- LIME is a half-edge editor, and a file without
# them crashes it. Layout is exactly what EditableMesh::rebuildFromFaces
# produces: a half-edge stores the vertex it points TO, next/prev walk the
# face loop, a vertex points at the first half-edge that leaves it.
# ---------------------------------------------------------------------------
UINT32_MAX = 4294967295
half_edges = []          # (vertexIndex, faceIndex, nextIndex, prevIndex, twinIndex)
face_he = []             # first half-edge of each face
vert_he = [UINT32_MAX] * len(VERTS)

for face_index, verts in enumerate(FACES):
    base = len(half_edges)
    face_he.append(base)
    n = len(verts)
    for i in range(n):
        half_edges.append([verts[(i + 1) % n], face_index,
                           base + (i + 1) % n, base + (i + n - 1) % n, UINT32_MAX])
        if vert_he[verts[i]] == UINT32_MAX:
            vert_he[verts[i]] = base + i

# Twins BY POSITION, the way LIME's own linkTwinsByPosition does it -- the
# twenty-four-vertex boxes keep their hard edges in EDEN yet still weld into
# closed, editable solids in the modeller.
def pos_key(p):
    return tuple(int(round(c * 10000.0)) for c in p)

lookup = {}
for index, he in enumerate(half_edges):
    to_vert = he[0]
    from_vert = half_edges[he[3]][0]
    key = (pos_key(VERTS[from_vert][0]), pos_key(VERTS[to_vert][0]))
    reverse = (key[1], key[0])
    if reverse in lookup:
        twin = lookup.pop(reverse)
        half_edges[index][4] = twin
        half_edges[twin][4] = index
    else:
        lookup[key] = index

twinned = sum(1 for he in half_edges if he[4] != UINT32_MAX)

out = ["# LIME Model Format v2.1",
       "# Half-edge mesh with embedded texture and transform",
       "# Generated by tools/make_prefab_hull_plate.py -- editable in LIME from here on",
       "",
       "# TRANSFORM",
       "transform_pos: 0 0 0",
       "transform_rot: 1 0 0 0",
       "transform_scale: 1 1 1",
       ""]
out.append(f"# VERTICES: {len(VERTS)}")
out.append("# idx: pos.x pos.y pos.z | nrm.x nrm.y nrm.z | uv.u uv.v | "
           "col.r col.g col.b col.a | halfEdgeIdx selected")
for i, ((px, py, pz), (nx, ny, nz), (u, v), (r, g, b, a)) in enumerate(VERTS):
    out.append(f"v {i}: {px:.5f} {py:.5f} {pz:.5f} | {nx:.5f} {ny:.5f} {nz:.5f} | "
               f"{u:.5f} {v:.5f} | {r:.4f} {g:.4f} {b:.4f} {a:.4f} | {vert_he[i]} 0")
out.append("")
out.append(f"# FACES: {len(FACES)}")
out.append("# idx: halfEdgeIdx vertexCount selected | vertex_indices...")
for i, f in enumerate(FACES):
    out.append(f"f {i}: {face_he[i]} {len(f)} 0 | " + " ".join(str(x) for x in f))
out.append("")
out.append(f"# HALF_EDGES: {len(half_edges)}")
out.append("# idx: vertexIndex faceIndex nextIndex prevIndex twinIndex")
for i, he in enumerate(half_edges):
    out.append(f"he {i}: {he[0]} {he[1]} {he[2]} {he[3]} {he[4]}")
out.append("")
for i, (name, p, fwd, up) in enumerate(PORTS):
    out.append(f'port {i}: "{name}" {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} | '
               f"{fwd[0]:.4f} {fwd[1]:.4f} {fwd[2]:.4f} | "
               f"{up[0]:.4f} {up[1]:.4f} {up[2]:.4f}")
out.append("")
out.append("# METADATA")
for k in sorted(META):
    out.append(f"meta {k}: {META[k]}")
out.append("")

# ---------------------------------------------------------------------------
# WALK IT THE WAY LIME WALKS IT, before writing a byte. Reproduces
# EditableMesh::triangulate exactly; refuses to write a file that only looks
# fine in the loader that ignores the hard half.
# ---------------------------------------------------------------------------
for face_index, verts in enumerate(FACES):
    walked, he = [], face_he[face_index]
    for _ in range(len(verts)):
        assert he < len(half_edges), f"face {face_index}: half-edge {he} out of range"
        prev = half_edges[he][3]
        assert prev < len(half_edges), f"face {face_index}: prev {prev} out of range"
        vertex = half_edges[prev][0]
        assert vertex < len(VERTS), f"face {face_index}: vertex {vertex} out of range"
        walked.append(vertex)
        he = half_edges[he][2]
    assert walked == verts, f"face {face_index}: walked {walked}, expected {verts}"
    assert he == face_he[face_index], f"face {face_index}: loop does not close"

for index, he in enumerate(half_edges):
    twin = he[4]
    if twin != UINT32_MAX:
        assert half_edges[twin][4] == index, f"half-edge {index}: twin is not mutual"

path = "assets/models/prefabs/hull_plate.lime"
with open(path, "w") as fh:
    fh.write("\n".join(out))
print(f"{path}: {len(VERTS)} vertices, {len(FACES)} faces, "
      f"{len(half_edges)} half-edges ({twinned} twinned, "
      f"{len(half_edges) - twinned} on the outside), "
      f"{len(PORTS)} ports, {len(META)} metadata keys")
