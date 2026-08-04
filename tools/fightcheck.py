#!/usr/bin/env python3
"""The flicker detector -- which faces fight for the same pixels?

Two axis-aligned faces z-fight when they lie in the SAME plane, point the
SAME direction, and OVERLAP with real area. This walks every pair of solid
pieces in a level and reports exactly those conditions, named, so a shimmer
the eye can barely localise becomes a pair of addresses.

Born 2026-08-03, the same day as leakcheck.py and from its aftermath: the
seam-sealing pass closed the hull ("now it's tight, it's airlocked") and
bought a lot of z-fighting with the same coin. Sealing and flickering are
both coverage problems; both deserve instruments, not eyes.

  python3 tools/fightcheck.py build/examples/terrain_editor/levels/from_ted.eden
"""
import json, sys, collections

path = sys.argv[1]
level = json.load(open(path))

# Engine convention (PrimitiveMeshBuilder::createCube): x/z centred,
# y grows UP from position -- position.y is the BASE.
pieces = []
for o in level["objects"]:
    if o["buildingType"] not in ("platform_slab", "platform_wall"):
        continue
    if not o.get("aabbCollision", True):
        # Non-colliding dressing still RENDERS -- it can fight. Include it.
        pass
    p, s = o["position"], o["scale"]
    pieces.append((o["name"],
                   (p[0]-s[0]/2, p[1], p[2]-s[2]/2,
                    p[0]+s[0]/2, p[1]+s[1], p[2]+s[2]/2)))

EPS = 1e-4      # same-plane tolerance
MIN_AREA = 0.01  # ignore sliver overlaps below 1 cm^2 -- invisible

def overlap1d(a0, a1, b0, b1):
    return max(0.0, min(a1, b1) - max(a0, b0))

# For each axis: faces at min (normal -axis) and max (normal +axis).
# Same plane + same normal + 2D overlap = a fight.
fights = []
for i in range(len(pieces)):
    ni, bi = pieces[i]
    for j in range(i + 1, len(pieces)):
        nj, bj = pieces[j]
        # quick reject: no AABB proximity at all
        if (bi[3] < bj[0] - EPS or bj[3] < bi[0] - EPS or
            bi[4] < bj[1] - EPS or bj[4] < bi[1] - EPS or
            bi[5] < bj[2] - EPS or bj[5] < bi[2] - EPS):
            continue
        for axis in range(3):
            u, v = (axis + 1) % 3, (axis + 2) % 3
            ou = overlap1d(bi[u], bi[u+3], bj[u], bj[u+3])
            ov = overlap1d(bi[v], bi[v+3], bj[v], bj[v+3])
            if ou * ov < MIN_AREA:
                continue
            for side in (0, 3):     # min face (-normal), max face (+normal)
                if abs(bi[axis+side] - bj[axis+side]) < EPS:
                    fights.append((ou * ov, "xyz"[axis] + ("-" if side == 0 else "+"),
                                   ni, nj))

fights.sort(reverse=True)
print(f"{len(pieces)} pieces, {len(fights)} fighting face pairs")
fam = collections.Counter()
def family(n):
    # strip the ship stem and trailing counters for grouping
    parts = n.split("_")
    keep = [p for p in parts if not p.isdigit()]
    for tag in ("bow", "stern"):
        pass
    return "_".join(keep[-3:]) if len(keep) >= 3 else n
for a, ax, ni, nj in fights:
    fam[(family(ni), family(nj), ax)] += 1
for (fi, fj, ax), c in fam.most_common(12):
    print(f"  {c:4d}x  {ax}  {fi}  <->  {fj}")
if fights:
    print("worst single pairs:")
    for a, ax, ni, nj in fights[:6]:
        print(f"  {a:7.2f} u^2  {ax}  {ni}  <->  {nj}")
else:
    print("  no shimmer -- every face owns its pixels")
