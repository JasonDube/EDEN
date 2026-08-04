#!/usr/bin/env python3
"""THE JOURNEYMAN -- the second ship laid down by the shipwright's
apprentice, and the first built the way players build: no revolve, no loft,
just the storey era's own tools. A pixel-art reference of a tiered spire
station: wide ring decks cantilevered over a narrow core (the anchor law's
showcase), a lift running the entire column, masts on the crown, twin
drop-boosters riding outriggers, and a keel cone socketed into the pad.

Two stages, one command, same contract as make_ship_apprentice.py:
  1. Generate the deck stack as a plan (deck2:..deck8:) and run the yard --
     rooms, sockets, lift shaft, roofs, overhang plates, masts, every law.
  2. Dress what the plan cannot say: boosters with flames, the keel cone,
     ring galleries, the dome crown, night paint.
"""
import json, subprocess, sys

W = H = 48
CX = CY = 24
PLAN = "tools/plans/journeyman_01.plan"
LEVEL = "build/examples/terrain_editor/levels/journeyman_01.eden"

# ---------------------------------------------------------------------------
# The deck stack. Radii in cells; the octagon is the round the grid can hold.
# Wide rings over narrow cores: the pagoda. Letters name the machinery.
# ---------------------------------------------------------------------------
def octa(r):
    cells = set()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if abs(dx) + abs(dy) <= int(r * 1.45):
                cells.add((CX + dx, CY + dy))
    return cells

def rim(cells):
    return {(x, y) for (x, y) in cells
            if not all((x+dx, y+dy) in cells for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)))}

DECKS = [8, 4, 7, 4, 6, 4, 3]      # d1..d7: disc, core, ring, core, ring, dome base, crown

def deck_grid(d, r):
    cells = octa(r)
    edge = rim(cells)
    g = [['_'] * W for _ in range(H)]
    for (x, y) in cells:
        g[y][x] = '#' if (x, y) in edge else '.'
    def put(dx, dy, c):
        g[CY + dy][CX + dx] = c
    # the lift column: one L on deck 1 serves every open floor above
    if d == 0:
        put(0, 0, 'L')
        # engine room south: three separate X grids in the rim, engines inward
        for off in (-3, 0, 3):
            xx, yy = CX + off, CY + r
            while g[yy][xx] != '#':
                yy -= 1
            g[yy][xx] = 'X'
            g[yy - 1][xx] = 'E'
        # reactor north with fins in the rim
        for off in (-1, 0):
            xx, yy = CX + off, CY - r
            while g[yy][xx] != '#':
                yy += 1
            g[yy][xx] = 'F'
            g[yy + 1][xx] = 'P'
            g[yy + 2][xx] = 'P'
        # cargo west, boarding door east
        put(-r + 2, 0, 'C'); put(-r + 3, 0, 'C')
        xx, yy = CX + r, CY
        while g[yy][xx] != '#':
            xx -= 1
        g[yy][xx] = 'D'
    if d == 2:            # gallery ring: windows around the rim, robot hall
        for (x, y) in sorted(edge):
            if (x + y) % 3 == 0:
                g[y][x] = 'W'
        put(-3, 0, 'R'); put(-2, 0, 'R')
    if d == 4:            # upper ring: windows sparser
        for (x, y) in sorted(edge):
            if (x + y) % 4 == 0:
                g[y][x] = 'W'
    if d == 5:            # dome base: the bridge
        put(0, -1, 'B'); put(1, -1, 'B')
    return "\n".join("".join(row) for row in g)

plan = deck_grid(0, DECKS[0])
for d in range(1, len(DECKS)):
    plan += f"\ndeck{d+1}:\n" + deck_grid(d, DECKS[d])
# the crown's masts: a layer of A over the top deck
mast = [['_'] * W for _ in range(H)]
mast[CY][CX] = 'A'
mast[CY][CX - 2] = 'A'
mast[CY][CX + 2] = 'A'
plan += f"\ndeck{len(DECKS)+1}:\n" + "\n".join("".join(r) for r in mast) + "\n"
open(PLAN, "w").write(plan)

r = subprocess.run([sys.executable, "tools/make_ship_from_plan.py", PLAN,
                    "--material", "4"])
if r.returncode != 0:
    sys.exit(r.returncode)

# ---------------------------------------------------------------------------
# The dressing.
# ---------------------------------------------------------------------------
level = json.load(open(LEVEL))
objs = level["objects"]

OX, OZ = 170.0, 130.0
CELL = 2.0
STOREY = 3.4
TOP_Y = 0.2 + len(DECKS) * STOREY + 0.4      # top roof upper face

INK   = (0.055, 0.062, 0.085, 1.0)
AMBER = (1.00, 0.63, 0.16, 1.0)
FLAME = (1.00, 0.55, 0.10, 1.0)
FLAME2= (1.00, 0.85, 0.35, 1.0)
STEEL = (0.30, 0.31, 0.36, 1.0)
HULL  = (0.34, 0.36, 0.42, 1.0)

n = 0
def piece(kind, px, py, pz, sx, sy, sz, color, collide=True, bright=1.0):
    global n
    n += 1
    objs.append({"name": f"journeyman_01_{kind}_{n}", "buildingType": "platform_wall",
                 "modelPath": "", "position": [px, py - sy / 2.0, pz],
                 "rotation": [0.0, 0.0, 0.0],
                 "scale": [sx, sy, sz], "primitiveType": 1, "primitiveSize": 1.0,
                 "primitiveColor": list(color), "primitiveHeight": 1.0,
                 "primitiveRadius": 0.5, "primitiveSegments": 16,
                 "aabbCollision": collide, "polygonCollision": False,
                 "bulletCollisionType": 0, "beingType": 0, "visible": True,
                 "isSkinned": False, "kinematicPlatform": False, "behaviors": [],
                 "brightness": bright, "hueShift": 0.0, "saturation": 1.0,
                 "dailySchedule": False, "patrolSpeed": 5.0,
                 "metadata": {"material": "nanocomposite", "density": "1.1",
                              "armor": "6"}})

# night paint -- milder than the Apprentice's; the reference is working grey
EXEMPT = ("_thruster_", "_radiator_", "_lamp", "_vent_", "_mast_", "_lift_")
for o in objs:
    if o["buildingType"] not in ("platform_slab", "platform_wall"):
        continue
    if any(t in o["name"] for t in EXEMPT):
        continue
    c = o["primitiveColor"]
    if c[3] < 0.9:
        continue
    o["primitiveColor"] = [c[0] * 0.70, c[1] * 0.71, c[2] * 0.78, c[3]]

def lathe(kind, px, pz, y0, y1, radius, layers, taper_top=0.0):
    """A vertical stack of octagon-ish ring boxes: cylinder, or cone when
    taper_top shrinks the last rings."""
    hstep = (y1 - y0) / layers
    for i in range(layers):
        t = i / max(1, layers - 1)
        r = radius * (1.0 - taper_top * t)
        yc = y0 + (i + 0.5) * hstep
        # alternating hair-widths off the shared planes -- the plane ledger
        # applies to dressing too (fightcheck found the nose cones shimmering)
        ins = 0.03 if i % 2 else 0.0
        if r - ins < 1.2:
            # A tip this small is ONE box -- three clamped segments overlap
            # each other and shimmer (fightcheck read their names out).
            piece(kind, px, yc + hstep / 2.0 - ins, pz,
                  max(0.6, 2 * (r - ins)), hstep - ins,
                  max(0.6, 2 * (r - ins)), HULL)
            continue
        for f0, f1 in ((-1.0, -0.5), (-0.5, 0.5), (0.5, 1.0)):
            mid = (f0 + f1) / 2.0
            cw = (1.0 - mid * mid * 0.8) ** 0.5
            piece(kind, px, yc + hstep / 2.0 - ins, pz + mid * (r - ins),
                  max(0.6, 2 * (r - ins) * cw), hstep - ins,
                  max(0.6, (f1 - f0) * (r - ins)), HULL)

# ---- the twin drop-boosters ------------------------------------------------
BX = (DECKS[0] + 5.0) * CELL     # clear of the widest ring
for side in (-1.0, 1.0):
    px = OX + side * BX
    lathe("booster", px, OZ, 3.0, 17.0, 4.2, 6)              # the barrel
    lathe("booster_nose", px, OZ, 17.0, 23.0, 4.0, 4, taper_top=0.85)
    piece("booster_tip", px, 24.0, OZ, 0.5, 1.6, 0.5, STEEL, collide=False)
    piece("booster_lamp", px, 24.6, OZ, 0.3, 0.3, 0.3, AMBER, collide=False, bright=2.2)
    # fins: four thin plates
    for fx, fz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        piece("booster_fin", px + fx * 4.4, 5.4, OZ + fz * 4.4,
              0.3 if fx else 2.6, 4.8, 0.3 if fz else 2.6, STEEL)
    # the flame: descending bright cones
    for i, (fy, fr, col, b) in enumerate(((2.2, 3.2, FLAME, 2.0), (0.2, 2.4, FLAME, 2.3),
                                          (-1.8, 1.6, FLAME2, 2.6), (-3.6, 0.8, FLAME2, 3.0))):
        piece("booster_flame", px, fy, OZ, fr * 2, 2.0, fr * 2, col,
              collide=False, bright=b)
    # outrigger arms to the core at two ring heights


# ---- outrigger arms: ONE through-beam per height, booster to booster --------
# (two half-beams met at the core sharing every plane -- one beam has no
# seam to fight; its end faces bury inside the booster barrels)
for ay in (0.2 + 3 * STOREY, 0.2 + 5 * STOREY):
    piece("arm", OX, ay - 0.4, OZ, 2 * BX, 0.9, 1.6, HULL)
    piece("arm_brace", OX, ay + 0.8, OZ, 2 * BX, 0.4, 0.7, STEEL)

# ---- small tender pods, low on the flanks ----------------------------------
for side in (-1.0, 1.0):
    px = OX + side * (DECKS[0] + 1.5) * CELL
    pz = OZ + 6.0
    lathe("pod", px, pz, 2.0, 7.0, 1.6, 3, taper_top=0.5)
    piece("pod_flame", px, 1.2, pz, 2.0, 1.6, 2.0, FLAME, collide=False, bright=2.2)
    lathe("pod", px, pz - 12.0, 4.0, 9.0, 1.4, 3, taper_top=0.5)

# ---- the keel cone: socketed into the pad ----------------------------------
lathe("keel", OX, OZ, 0.2, -7.0, 5.5, 5, taper_top=0.85)
piece("keel_band", OX, -1.4, OZ, 7.4, 0.5, 7.4, INK, collide=False)

# ---- the dome crown ---------------------------------------------------------
lathe("dome", OX, OZ, TOP_Y, TOP_Y + 3.4, 5.0, 4, taper_top=0.75)
for i in range(8):
    ang = i / 8.0 * 6.28318
    import math
    piece("dome_lamp", OX + math.cos(ang) * 4.2, TOP_Y + 1.2,
          OZ + math.sin(ang) * 4.2, 0.32, 0.32, 0.32, AMBER,
          collide=False, bright=2.2)

# ---- ring galleries: amber porthole runs on every wide deck rim -------------
import math
for d, r in enumerate(DECKS):
    if r < 5:
        continue
    ry = 0.2 + d * STOREY + 0.4 + 1.6
    for i in range(max(10, r * 3)):
        ang = i / max(10, r * 3) * 6.28318
        gx = OX + math.cos(ang) * (r * CELL + 0.15)
        gz = OZ + math.sin(ang) * (r * CELL + 0.15)
        piece("gallery_lamp", gx, ry, gz, 0.28, 0.42, 0.28, AMBER,
              collide=False, bright=1.9)

# Spawn on open ground SOUTHEAST of the hull -- clear of the disc, the
# booster barrel, and the arms -- a short walk from the boarding door.
level["settings"]["spawnPosition"] = [OX + 20.0, 2.0, OZ + 12.0]

json.dump(level, open(LEVEL, "w"), indent=1)

tons = sum(o["scale"][0] * o["scale"][1] * o["scale"][2] *
           float(o.get("metadata", {}).get("density", "2"))
           for o in objs
           if o["buildingType"] in ("platform_slab", "platform_wall")
           and o.get("aabbCollision", True))
print(f"{LEVEL}: {len(objs)} pieces after dressing ({n} added)")
print(f"the weighing: ~{tons:,.0f} t")
