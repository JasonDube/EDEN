#!/usr/bin/env python3
"""Emit assets/models/prefabs/helm.lime -- the first developer-authored prefab.

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
# The console. Scaled for a body 1.7 m tall: the desk edge lands at about hip
# height, the screen at chest, so a pilot standing at pilot_station is looking
# INTO it rather than down at it.
# ---------------------------------------------------------------------------
HULL   = (0.38, 0.40, 0.44, 1.0)   # dark plating
TRIM   = (0.62, 0.55, 0.30, 1.0)   # brass edging, so the form reads at distance
SCREEN = (0.16, 0.72, 0.68, 1.0)   # lit teal
DARK   = (0.20, 0.21, 0.24, 1.0)
LEVER  = (0.75, 0.28, 0.22, 1.0)   # red throttle -- the one thing you must find fast

TILT = math.radians(-22.0)         # desk face leaning toward the pilot (-Z)

box((0.0, 0.06, 0.0),  (0.62, 0.06, 0.40), DARK)          # floor plate
box((0.0, 0.34, 0.06), (0.46, 0.28, 0.26), HULL)          # plinth
box((0.0, 0.62, 0.02), (0.56, 0.05, 0.34), TRIM)          # waist band
box((0.0, 0.80, 0.00), (0.58, 0.16, 0.32), HULL)          # body
box((0.0, 0.99, -0.10), (0.54, 0.04, 0.30), HULL, pitch=TILT)   # desk
box((0.0, 1.02, -0.12), (0.40, 0.01, 0.20), SCREEN, pitch=TILT) # screen
box((0.0, 1.22, 0.16), (0.50, 0.18, 0.03), DARK)          # upright display back
box((0.0, 1.22, 0.14), (0.44, 0.13, 0.01), SCREEN)        # upright display face
box((0.0, 1.42, 0.16), (0.52, 0.03, 0.05), TRIM)          # display hood

for side in (-1.0, 1.0):                                   # throttle levers
    box((side * 0.44, 1.06, -0.16), (0.03, 0.10, 0.03), LEVER, pitch=TILT)
    box((side * 0.44, 1.16, -0.20), (0.05, 0.03, 0.05), TRIM, pitch=TILT)

# ---------------------------------------------------------------------------
# Ports: how it attaches, and where a body goes to use it.
# ---------------------------------------------------------------------------
PORTS = [
    # Out of the bottom, so it seats on a deck the way a frame seats in a wall.
    ("deck_mount",    (0.0, 0.0, 0.0),   (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)),
    # Where the pilot stands: a pace in front, facing back INTO the console.
    ("pilot_station", (0.0, 0.0, -0.95), (0.0, 0.0,  1.0), (0.0, 1.0, 0.0)),
]

META = {
    "role":        "helm",           # what the game does with it
    "catalog":     "controls",       # which shelf it appears on
    "title":       "Helm Console",
    "price":       "2500",
    "mass":        "180",
    "summary":     "Steers a vessel. Needs an engine on the same hull.",
    "station":     "pilot_station",  # which port a body stands at
    "mount":       "deck_mount",     # which port seats it
    "prefab":      "1",              # this file is a catalogue item, not scenery
}

out = ["# LIME Model Format v2.0",
       "# Generated by tools/make_prefab_helm.py -- editable in LIME from here on",
       "",
       "transform_pos: 0 0 0",
       "transform_rot: 1 0 0 0",
       "transform_scale: 1 1 1",
       ""]
for k in sorted(META):
    out.append(f"meta {k}: {META[k]}")
out.append("")
for i, ((px, py, pz), (nx, ny, nz), (u, v), (r, g, b, a)) in enumerate(VERTS):
    out.append(f"v {i}: {px:.5f} {py:.5f} {pz:.5f} | {nx:.5f} {ny:.5f} {nz:.5f} | "
               f"{u:.5f} {v:.5f} | {r:.4f} {g:.4f} {b:.4f} {a:.4f} | 0 0")
out.append("")
for i, f in enumerate(FACES):
    out.append(f"f {i}: 0 {len(f)} 0 | " + " ".join(str(x) for x in f))
out.append("")
for i, (name, p, fwd, up) in enumerate(PORTS):
    out.append(f'port {i}: "{name}" {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} | '
               f"{fwd[0]:.4f} {fwd[1]:.4f} {fwd[2]:.4f} | "
               f"{up[0]:.4f} {up[1]:.4f} {up[2]:.4f}")
out.append("")

path = "assets/models/prefabs/helm.lime"
with open(path, "w") as fh:
    fh.write("\n".join(out))
print(f"{path}: {len(VERTS)} vertices, {len(FACES)} faces, "
      f"{len(PORTS)} ports, {len(META)} metadata keys")
