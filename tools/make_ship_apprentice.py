#!/usr/bin/env python3
"""THE APPRENTICE -- the first ship in the fleet designed by the shipwright's
apprentice rather than the shipwright, laid down from a pixel-art reference.

Two stages, one command:
  1. The YARD: runs make_ship_from_plan.py on tools/plans/apprentice_01.plan
     (nanocomposite hull, revolve 0.55 ribs 360) -- rooms, sockets, laws,
     exactly as any hand-drawn ship. The plan is the authority for everything
     structural; nothing here touches rooms or systems.
  2. The DRESSING: what the plan language cannot yet say, added as honest
     prims in the same voxel hand -- the reference's twin equipment pods on
     outrigger struts, the bow mast, the stern boom, the lit amber galleries
     on the wedge flanks, and a night-paint pass that takes the hull down to
     the reference's dark slate. Dressing is welded like hull (platform
     pieces, material metadata, honest tonnage); lamps and panes are
     collide-false so trim never blocks boots.

Positions are derived from the SAME cell math as the yard (ORIGIN 170/130,
CELL 2, plan 24x36) -- the row numbers below are the plan's, and if the plan
changes shape, change them here too. Bespoke by design: this dresses ONE
ship. The lesson it teaches can graduate into the plan language later.
"""
import json, subprocess, sys, zlib

PLAN = "tools/plans/apprentice_01.plan"
LEVEL = "build/examples/terrain_editor/levels/apprentice_01.eden"

r = subprocess.run([sys.executable, "tools/make_ship_from_plan.py", PLAN,
                    "--material", "4"])
if r.returncode != 0:
    sys.exit(r.returncode)

level = json.load(open(LEVEL))
objs = level["objects"]

# ---- the same math the yard uses -------------------------------------------
OX, OZ = 170.0, 130.0
CELL, W, H = 2.0, 24, 36
DECK = 0.6                      # FLOOR_Y + FLOOR_T
def zrow(y): return OZ + (y + 0.5 - H / 2.0) * CELL

INK   = (0.055, 0.062, 0.085, 1.0)
AMBER = (1.00, 0.63, 0.16, 1.0)
STEEL = (0.26, 0.27, 0.34, 1.0)
HULL  = (0.23, 0.25, 0.32, 1.0)   # matches the night-painted shell

n = 0
def piece(kind, px, py, pz, sx, sy, sz, color, collide=True, bright=1.0):
    global n
    n += 1
    # Dressing is DESIGNED in y-centres; the engine anchors a primitive's y
    # at its BASE (PrimitiveMeshBuilder::createCube) -- convert here, once.
    # Before this line every dressed piece sat half its own height too high
    # (the bow mast floated 0.55 above the dome it was rooted in).
    o = {"name": f"apprentice_01_{kind}_{n}", "buildingType": "platform_wall",
         "modelPath": "", "position": [px, py - sy / 2.0, pz], "rotation": [0.0, 0.0, 0.0],
         "scale": [sx, sy, sz], "primitiveType": 1, "primitiveSize": 1.0,
         "primitiveColor": list(color), "primitiveHeight": 1.0,
         "primitiveRadius": 0.5, "primitiveSegments": 16,
         "aabbCollision": collide, "polygonCollision": False,
         "bulletCollisionType": 0, "beingType": 0, "visible": True,
         "isSkinned": False, "kinematicPlatform": False, "behaviors": [],
         "brightness": bright, "hueShift": 0.0, "saturation": 1.0,
         "dailySchedule": False, "patrolSpeed": 5.0,
         "metadata": {"material": "nanocomposite", "density": "1.1",
                      "armor": "6"}}
    objs.append(o)

# ---- night paint ------------------------------------------------------------
# The reference hull is dark slate; the yard's shell is daylight gray. One
# multiplicative pass takes every opaque structural piece down while the
# patchwork variance survives untouched. Glass keeps its glaze; grids, fins,
# lamps and vents keep their working colours.
EXEMPT = ("_thruster_", "_radiator_", "_lamp", "_vent_", "_mast_")
for o in objs:
    if o["buildingType"] not in ("platform_slab", "platform_wall"):
        continue
    if any(t in o["name"] for t in EXEMPT):
        continue
    c = o["primitiveColor"]
    if c[3] < 0.9:
        continue
    o["primitiveColor"] = [c[0] * 0.52, c[1] * 0.54, c[2] * 0.66, c[3]]

# ---- the twin pods ----------------------------------------------------------
# Sealed equipment nacelles beside the reactor, the reference's signature.
# A mini-lathe in the yard's own manner: stacked full-width ring boxes, an
# ink intake disc forward, a mast with an amber collar on top. No interior
# -- they are machinery, not rooms -- but they collide, weld and weigh.
POD_Z, POD_X, POD_YC = zrow(18.0), 18.0, 1.8
POD_RY, POD_RX, POD_RZ = 3.25, 3.2, 4.6
LAYER = 1.3
for side in (-1.0, 1.0):
    px = OX + side * POD_X
    k = -POD_RY
    while k < POD_RY - 1e-6:
        z0, z1 = k, min(k + LAYER, POD_RY)
        edge = min(abs(z0), abs(z1)) if z0 * z1 > 0 else 0.0
        c = (1.0 - (edge / POD_RY) ** 2) ** 0.5
        # three plan segments per layer -- an octagon, not a crate: the
        # nose and tail of each ring are narrower than its waist
        for f0, f1 in ((-1.0, -0.55), (-0.55, 0.55), (0.55, 1.0)):
            mid = (f0 + f1) / 2.0
            cw = (1.0 - mid * mid * 0.85) ** 0.5
            piece("pod_ring", px, POD_YC + (z0 + z1) / 2.0,
                  POD_Z + mid * POD_RZ * c,
                  max(0.8, 2 * POD_RX * c * cw), z1 - z0,
                  max(0.9, (f1 - f0) * POD_RZ * c), HULL)
        k += LAYER
    piece("pod_intake", px, POD_YC, POD_Z - POD_RZ - 0.15,
          2 * POD_RX * 0.72, 2 * POD_RY * 0.72, 0.5, INK, collide=False)
    piece("pod_mast", px, POD_YC + POD_RY + 1.3, POD_Z, 0.35, 2.6, 0.35, STEEL,
          collide=False)
    piece("pod_collar", px, POD_YC + POD_RY + 0.55, POD_Z, 0.85, 0.26, 0.85,
          AMBER, collide=False, bright=1.8)
    piece("pod_lamp", px, POD_YC + POD_RY + 2.75, POD_Z, 0.26, 0.26, 0.26,
          (1.0, 0.97, 0.85, 1.0), collide=False, bright=2.4)
    # outrigger struts: one load bearer, one brace, overlapping hull and pod
    piece("strut", OX + side * 11.7, 1.6, POD_Z, 6.8, 1.1, 1.7, HULL)
    piece("strut_brace", OX + side * 11.7, 3.0, POD_Z, 6.8, 0.45, 0.8, STEEL)

# ---- citadel spur antennae --------------------------------------------------
# The reference's horizontal cross-arms off the forward tower.
SPUR_Z = zrow(5.0)
for side in (-1.0, 1.0):
    piece("spur", OX + side * 8.6, 3.4, SPUR_Z, 3.6, 0.22, 0.22, STEEL,
          collide=False)
    piece("spur_lamp", OX + side * 10.5, 3.4, SPUR_Z, 0.24, 0.24, 0.24, AMBER,
          collide=False, bright=2.2)

# ---- bow mast ---------------------------------------------------------------
BOW_Z = zrow(2.5)
piece("bow_mast", OX, 5.0, BOW_Z, 0.4, 7.0, 0.4, STEEL, collide=False)
for y in (6.2, 7.4):
    piece("bow_collar", OX, y, BOW_Z, 1.0, 0.26, 1.0, AMBER,
          collide=False, bright=1.8)
piece("bow_lamp", OX, 8.8, BOW_Z, 0.28, 0.28, 0.28, (1.0, 0.98, 0.9, 1.0),
      collide=False, bright=2.4)

# ---- stern boom -------------------------------------------------------------
BOOM_Z0 = zrow(33.5) + 0.6
piece("boom", OX, 1.4, BOOM_Z0 + 3.4, 0.65, 0.65, 7.4, HULL)
for dz in (2.2, 5.2):
    piece("boom_collar", OX, 1.4, BOOM_Z0 + dz, 1.3, 1.3, 0.35, AMBER,
          collide=False, bright=1.8)
piece("boom_lamp", OX, 1.4, BOOM_Z0 + 7.3, 0.3, 0.3, 0.3, AMBER,
      collide=False, bright=2.4)

# ---- amber galleries --------------------------------------------------------
# The reference's glowing window banks: an ink backing panel proud of the
# shell, a grid of hot panes prouder still. Pane brightness varies by crc so
# the grid flickers like a crewed deck, not a billboard.
def gallery(kind, x_abs, zc, rows, cols, pane_dy=0.72, pane_dz=0.95,
            yc=1.9, back_sy=None, back_sz=None):
    back_sy = back_sy or rows * pane_dy + 0.7
    back_sz = back_sz or cols * pane_dz + 0.7
    for side in (-1.0, 1.0):
        piece(f"{kind}_backing", OX + side * x_abs, yc, zc,
              0.16, back_sy, back_sz, INK, collide=False)
        for i in range(rows):
            for j in range(cols):
                py = yc + (i - (rows - 1) / 2.0) * pane_dy
                pz = zc + (j - (cols - 1) / 2.0) * pane_dz
                h = zlib.crc32(f"{kind}{side}{i}{j}".encode())
                b = 1.6 + (h & 0xFF) / 255.0 * 1.0
                col = AMBER if (h & 7) else (1.0, 0.5, 0.12, 1.0)
                # Panes stand fully proud of the backing -- overlapping it
                # made their bottom faces coplanar (fightcheck conviction).
                piece(f"{kind}_pane", OX + side * (x_abs + 0.22), py, pz,
                      0.12, 0.5, 0.62, col, collide=False, bright=b)

gallery("wedge_gallery", 15.1, zrow(23.5), rows=3, cols=5)      # the great bays
gallery("neck_gallery", 7.1, zrow(11.0), rows=2, cols=4, yc=1.7)
gallery("citadel_gallery", 7.1, zrow(6.0), rows=2, cols=3, yc=1.7)

# ---- spawn beside the starboard boarding arch -------------------------------
level["settings"]["spawnPosition"] = [OX + 14.0, 2.0, zrow(24.0) + 6.0]

json.dump(level, open(LEVEL, "w"), indent=1)

# ---- the weighing, told honestly --------------------------------------------
tons = thrust = 0.0
n_eng = 0
for o in objs:
    if o["buildingType"] in ("platform_slab", "platform_wall") and o["aabbCollision"]:
        sx, sy, sz = o["scale"]
        tons += sx * sy * sz * float(o.get("metadata", {}).get("density", "2"))
    if o["buildingType"] == "socket_marker" and \
       o.get("metadata", {}).get("socket") == "engine":
        n_eng += 1
        thrust += 2500.0
print(f"{LEVEL}: {len(objs)} pieces after dressing ({n} added)")
print(f"the weighing: ~{tons:,.0f} t of hull; {n_eng} engine sockets = "
      f"{thrust:,.0f} thrust when fully outfitted with stock engines"
      + (" -- she flies" if thrust > tons else
         f" -- she needs {max(0, (tons - thrust) / 2500):,.0f} more engines' "
         "worth of thrust (or lighter bones) to lift planetside; "
         "in space, doctrine says otherwise"))
