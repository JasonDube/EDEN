#!/usr/bin/env python3
"""Emit assets/models/prefabs/robot_station.lime -- the first developer-authored prefab.

WHY A GENERATOR AND NOT C++
The ship in TESSARA:AXIOM is 1324 lines of C++ that have to RUN for the thing to
exist. A prefab must be the opposite: a file that exists on its own, that LIME can
open, that an artist can reshape without touching a compiler. So this script runs
ONCE and its output is the deliverable. Edit helm.lime in LIME from here on; if you
ever want the plain original back, run this again.

WHAT MAKES IT A PREFAB RATHER THAN A MESH
.lime already carries more than geometry, which is why we do not need a new format:

  meta <key>: <value>   arbitrary strings -- role, price, catalogue, mass
  port <i>: "name" p | forward | up    named frames in space, free position

So the helm declares what it IS (role: helm), what it costs, where it MOUNTS
(deck_mount, pointing up out of its base) and where a pilot STANDS to work it
(pilot_station, in front of it, facing back at the console). That last one is the
hardcoded Ship::helmStation() becoming authored data.

Control points were the other candidate for those two, but a cp references a
VERTEX INDEX -- it can only mark a place the mesh already has geometry. A station
is empty air in front of the console. Ports carry a free position and an
orientation, so they are the right tool.
"""
import math, struct

VERTS, FACES = [], []

def box(centre, half, colour, yaw=0.0, pitch=0.0):
    """A box with four vertices per face, so each face keeps its own normal.

    Sharing eight corners would average the normals and smear a hard-edged
    console into something soft. Twenty-four vertices is the cost of an edge
    reading as an edge."""
    cx, cy, cz = centre
    hx, hy, hz = half
    cp, sp = math.cos(pitch), math.sin(pitch)
    cyw, syw = math.cos(yaw), math.sin(yaw)

    def place(p):
        x, y, z = p
        y, z = y * cp - z * sp, y * sp + z * cp      # pitch about X
        x, z = x * cyw + z * syw, -x * syw + z * cyw # yaw about Y
        return (cx + x, cy + y, cz + z)

    def direction(n):
        x, y, z = n
        y, z = y * cp - z * sp, y * sp + z * cp
        x, z = x * cyw + z * syw, -x * syw + z * cyw
        return (x, y, z)

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
        n = direction(normal)
        for uv, corner in zip([(0,0),(1,0),(1,1),(0,1)], corners):
            VERTS.append((place(corner), n, uv, colour))
        FACES.append([base, base + 1, base + 2, base + 3])

# ---------------------------------------------------------------------------
# The robot charging station. A dock with open arms: base pad the robot
# stands on, back pillar carrying the charge coupler, live strip that will
# glow when the juice era gives glow to consumers. Draws 15 kW when online;
# robots do not charge around the clock, so the switch matters.
# ---------------------------------------------------------------------------
STEEL = (0.36, 0.38, 0.42, 1.0)
DARK  = (0.20, 0.21, 0.24, 1.0)
BRASS = (0.62, 0.55, 0.30, 1.0)
LIVE  = (0.55, 0.85, 0.55, 1.0)
VIOLET= (0.55, 0.35, 0.75, 1.0)   # robot-hall violet, same family as the pads

box((0.0, 0.05, 0.1), (0.55, 0.05, 0.65), DARK)      # stand pad
box((0.0, 0.12, 0.1), (0.45, 0.02, 0.55), VIOLET)    # pad inlay
box((0.0, 0.75, -0.62), (0.50, 0.70, 0.10), STEEL)   # back pillar
box((0.0, 1.50, -0.60), (0.42, 0.06, 0.08), BRASS)   # crown rail
box((0.0, 1.05, -0.50), (0.30, 0.16, 0.04), DARK)    # coupler housing
box((0.0, 1.05, -0.45), (0.20, 0.10, 0.02), LIVE)    # coupler face
for side in (-1.0, 1.0):                             # charge arms
    box((side * 0.46, 0.95, -0.30), (0.05, 0.05, 0.28), STEEL)
    box((side * 0.46, 0.95, -0.02), (0.06, 0.07, 0.06), BRASS)

# ---------------------------------------------------------------------------
# Ports: how it attaches, and where a body goes to use it.
# ---------------------------------------------------------------------------
PORTS = [
    ("deck_mount", (0.0, 0.0, 0.0),    (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)),
    # The electrical terminal on the pillar flank.
    ("power_in",   (0.52, 0.75, -0.62), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
    # Where the robot stands to drink.
    ("dock",       (0.0, 0.12, 0.1),   (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)),
]

META = {
    "power_in":  "15",           # kilowatts it draws while charging
    "power_off": "1",            # ships OFFLINE -- charge time is a choice
    "role":    "robot",          # seats on a robot socket, and only there
    "catalog": "robotics",
    "title":   "Robot Charging Station",
    "price":   "1800",
    "mass":    "260",
    "summary": "Recharges a robot. Draws 15 kW online. Mounts on a robot socket.",
    "mount":   "deck_mount",
    "prefab":  "1",
}

# ---------------------------------------------------------------------------
# HALF-EDGES. Not optional, and the reason the first attempt crashed LIME.
#
# EDEN's loader ignores topology -- it fan-triangulates the vertex list on each
# face and never looks at a half-edge -- so a file with none read back perfectly
# and I believed it was correct. LIME IS a half-edge editor. It logged "Face N
# has invalid halfEdge 0" seventy-eight times and then failed to allocate.
#
# Two loaders, one format, and only one of them exercised. Reading a file back
# through the parser that ignores the hard half proves nothing about the parser
# that needs it.
#
# The layout below is exactly what EditableMesh::rebuildFromFaces produces, which
# is the authority: a half-edge stores the vertex it points TO, next and prev walk
# the face loop, and a vertex points at the first half-edge that leaves it.
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

# Twins BY POSITION, the way LIME's own linkTwinsByPosition does it -- so the
# twenty-four-vertex boxes (which keep their hard edges in EDEN, because a shared
# corner would average the normals and soften the console into a pillow) still
# weld into closed, editable solids in the modeller.
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
       "# Generated by tools/make_prefab_engine.py -- editable in LIME from here on",
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
# WALK IT THE WAY LIME WALKS IT, before writing a byte.
#
# The first version of this file was verified by loading it back through EDEN's
# LimeLoader -- which fan-triangulates the `f` lines and never touches a
# half-edge. It passed, and it crashed the modeller. Verifying against the reader
# that ignores the hard half is not verifying.
#
# So this reproduces EditableMesh::triangulate exactly: start at the face's
# half-edge, take each vertex from the PREVIOUS half-edge's destination, follow
# next, and require the recovered polygon to equal the one on the `f` line. If
# the topology is wrong this refuses to write rather than handing over something
# that only looks fine in one of the two programs that read it.
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

path = "assets/models/prefabs/robot_station.lime"
with open(path, "w") as fh:
    fh.write("\n".join(out))
print(f"{path}: {len(VERTS)} vertices, {len(FACES)} faces, "
      f"{len(half_edges)} half-edges ({twinned} twinned, "
      f"{len(half_edges) - twinned} on the outside), "
      f"{len(PORTS)} ports, {len(META)} metadata keys")
