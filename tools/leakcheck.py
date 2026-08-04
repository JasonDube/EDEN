#!/usr/bin/env python3
"""The seam detector -- does light get into the hull?

Stands sample points inside a built ship and fires rays in random directions.
A ray that escapes without striking any solid piece is A HOLE, reported with
where it stood, where it went, and the named pieces it squeezed between --
so the leak can be traced to the loop that poured the gap.

Born 2026-08-03 from the third field report in one day: bands at gauge
thickness still leaked on steep revolves ("every gradation had gaps, though
the blocks were nice and chunky"). Eyes disagree about thin slits; rays do
not.

  python3 tools/leakcheck.py build/examples/terrain_editor/levels/from_ted.eden

KNOWN SANCTIONED ESCAPES: a hull with door rows reports its own doorways --
the boarding arch is an absence the shell respects, and a ray walking out
the front door is not a defect. Check the escape's z against the plan's D
rows before convicting anything.
"""
import json, math, random, sys, collections

path = sys.argv[1]
RAYS_PER_POINT = int(sys.argv[2]) if len(sys.argv) > 2 else 400
level = json.load(open(path))

# Solid geometry only: colliding structural pieces. Glass counts as hull --
# a window is not a hole. Sockets are paint; lamps and greebles do not seal.
boxes, names = [], []
for o in level["objects"]:
    if o["buildingType"] not in ("platform_slab", "platform_wall"):
        continue
    if not o.get("aabbCollision", True):
        continue
    p, s = o["position"], o["scale"]
    # The engine's primitive convention (PrimitiveMeshBuilder::createCube):
    # x/z centred on position, y grows UP from position -- position.y is the
    # BASE. The first draft of this file centred y and manufactured phantom
    # gaps half a box-height below every real piece.
    boxes.append((p[0]-s[0]/2, p[1], p[2]-s[2]/2,
                  p[0]+s[0]/2, p[1]+s[1], p[2]+s[2]/2))
    names.append(o["name"])

if not boxes:
    sys.exit("no solid pieces in this level")

minx = min(b[0] for b in boxes); maxx = max(b[3] for b in boxes)
miny = min(b[1] for b in boxes); maxy = max(b[4] for b in boxes)
minz = min(b[2] for b in boxes); maxz = max(b[5] for b in boxes)
cx = (minx + maxx) / 2.0
FAR = max(maxx-minx, maxy-miny, maxz-minz) + 10.0

def hit(ox, oy, oz, dx, dy, dz):
    """Nearest slab-test hit distance against all boxes, or None."""
    best = None
    for (x0, y0, z0, x1, y1, z1) in boxes:
        tmin, tmax = 0.0, FAR
        ok = True
        for o, d, lo, hi in ((ox,dx,x0,x1),(oy,dy,y0,y1),(oz,dz,z0,z1)):
            if abs(d) < 1e-9:
                if o < lo or o > hi: ok = False; break
                continue
            t0, t1 = (lo-o)/d, (hi-o)/d
            if t0 > t1: t0, t1 = t1, t0
            tmin, tmax = max(tmin, t0), min(tmax, t1)
            if tmin > tmax: ok = False; break
        if ok and (best is None or tmin < best):
            best = tmin
    return best

def inside(px, py, pz):
    """Interior heuristic: not inside a box, and sealed on all six axes."""
    for (x0, y0, z0, x1, y1, z1) in boxes:
        if x0 <= px <= x1 and y0 <= py <= y1 and z0 <= pz <= z1:
            return False
    for d in ((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)):
        if hit(px, py, pz, *d) is None:
            return False
    return True

# Sample stations along the spine: above deck (rooms) and below (the belly,
# when there is one). Off-centre lanes catch leaks the spine cannot see.
points = []
z = minz + 1.0
while z < maxz - 1.0:
    # Station heights avoid the LAYER grid planes (multiples of 2.0 from the
    # 0.6 deck) -- a station ON the contact plane between two abutting boxes
    # sends rays sliding along a zero-thickness seam and cries wolf.
    for (px, py) in ((cx, 1.53), (cx, -1.33), (cx - 2.5, 1.53), (cx + 2.5, 1.53),
                     (cx - 2.5, -1.33), (cx + 2.5, -1.33)):
        if miny < py < maxy and inside(px, py, z):
            points.append((px, py, z))
    z += 2.0

if not points:
    sys.exit("no interior sample points found -- is this a built ship level?")

random.seed(7)   # the same sky every run: a detector must be repeatable
leaks = []
for (px, py, pz) in points:
    for _ in range(RAYS_PER_POINT):
        th = random.uniform(0.0, 2*math.pi)
        u = random.uniform(-1.0, 1.0)
        r = math.sqrt(1.0 - u*u)
        dx, dy, dz = r*math.cos(th), u, r*math.sin(th)
        if hit(px, py, pz, dx, dy, dz) is None:
            leaks.append((px, py, pz, dx, dy, dz))

def near_names(px, py, pz, dx, dy, dz):
    """The pieces the escaping ray squeezed between: min distance from the
    ray segment to each box, closest few reported."""
    scored = []
    for i, (x0, y0, z0, x1, y1, z1) in enumerate(boxes):
        best = 1e9
        for t in [j * 0.5 for j in range(0, 60)]:
            qx, qy, qz = px+dx*t, py+dy*t, pz+dz*t
            ddx = max(x0-qx, 0.0, qx-x1)
            ddy = max(y0-qy, 0.0, qy-y1)
            ddz = max(z0-qz, 0.0, qz-z1)
            d = math.sqrt(ddx*ddx + ddy*ddy + ddz*ddz)
            if d < best: best = d
        scored.append((best, names[i]))
    scored.sort()
    return [f"{n} ({d:.2f})" for d, n in scored[:3]]

print(f"{len(points)} interior stations x {RAYS_PER_POINT} rays: "
      f"{len(leaks)} escaped")
by_area = collections.Counter()
shown = 0
for (px, py, pz, dx, dy, dz) in leaks:
    nn = near_names(px, py, pz, dx, dy, dz)
    key = tuple(n.split(" (")[0] for n in nn[:2])
    by_area[key] += 1
    if by_area[key] == 1 and shown < 12:
        shown += 1
        print(f"  LEAK from ({px:.1f},{py:.1f},{pz:.1f}) "
              f"dir ({dx:+.2f},{dy:+.2f},{dz:+.2f})  between: {', '.join(nn)}")
if len(by_area) > shown:
    print(f"  ... {len(by_area) - shown} more distinct gap sites")
if not leaks:
    print("  she is tight -- no sky through the seams")
