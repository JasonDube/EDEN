#!/usr/bin/env python3
"""Raise a ship from a 2D deck plan -- the Shipwright's generator, v1.

THE RULES, learned from the first real drawing (tools/plans/midship_01.plan):
  - WALLS MAKE ROOMS, LETTERS MAKE SOCKETS. A letter cell is an equipment
    footprint standing on plain floor, not a room label: connected same-letter
    cells cluster into ONE socket (six lone R cells = six robot stations; two
    3x3 E blocks = two engine mounts).
  - Doors are GAPS: a D cell is walkable floor where no wall gets built.
  - The hull footprint (floor under walls too) becomes greedy-rectangle
    platform_slab floors; '#' runs become platform_wall boxes. Everything is
    the loader's own primitive schema, so the level machinery -- collision at
    F5, hole-free door walking, selection -- treats it as hand-built.

Output: build/examples/terrain_editor/levels/plan_ship.eden -- a clone of
shipyard.eden's settings and terrain reference with the generated ship as its
objects, spawn set just aft of the stern.  V1 limits, stated: no hull shell or
bow styling yet (dressing comes later), no lintels over doors, and FLIGHT of
plan-ships needs the multi-slab manifest first -- walk it, don't fly it yet.
"""
import json, sys, copy, math, zlib

CELL   = 2.0    # world units per plan cell
FLOOR_Y = 0.2   # floor slab base
FLOOR_T = 0.4   # floor thickness -> deck top at FLOOR_Y + FLOOR_T
WALL_H  = 3.0
ORIGIN_X, ORIGIN_Z = 170.0, 130.0   # empty ground in shipyard, clear of the user's builds

ROLE = {'E': ('engine', (0.78, 0.35, 0.16, 1.0)),
        'R': ('robot',  (0.55, 0.35, 0.75, 1.0)),
        'B': ('helm',   (0.16, 0.63, 0.59, 1.0)),   # the helm SOCKET -- the room around it is the bridge
        'C': ('cargo',  (0.63, 0.43, 0.20, 1.0)),
        'P': ('power',  (0.93, 0.79, 0.22, 1.0))}   # reactor -- draw it 2x2, plants are big

plan_path = sys.argv[1] if len(sys.argv) > 1 else 'tools/plans/midship_01.plan'

# ---- hull materials --------------------------------------------------------
# The tech ladder, tier 1 -> 6. Density in t/unit^3, price in CR/unit^3,
# armor for the future per-slab damage model. KEEP IN SYNC with the table in
# Shipwright.cpp (the drafting table's live pricing) -- the yard here is the
# authority; the tint is multiplied into every structural piece so a hull
# wears its material. Most tiers are locked early game (the Shipwright
# enforces availability; the yard builds whatever it is asked to).
MATERIALS = {
    1: ("light_alloy",       "Light Alloy",              1.4,   2,  1, (1.02, 1.00, 0.96)),
    2: ("metallic_laminate", "Metallic Laminate",        2.0,   5,  2, (1.00, 1.00, 1.00)),
    3: ("adv_laminate",      "Adv. Metallic Laminate",   2.2,  14,  4, (0.95, 0.97, 1.05)),
    4: ("nanocomposite",     "Nanocomposite",            1.1,  40,  6, (0.85, 0.88, 0.92)),
    5: ("diamondoid",        "Diamondoid",               1.6, 150, 10, (1.05, 1.05, 1.10)),
    6: ("exotic_laminate",   "Exotic Armor Laminate",    5.0, 600, 25, (0.75, 0.72, 0.85)),
}
MAT = int(sys.argv[sys.argv.index('--material') + 1]) if '--material' in sys.argv else 1
if MAT not in MATERIALS: MAT = 1
MAT_KEY, MAT_NAME, MAT_DENSITY, MAT_PRICE, MAT_ARMOR, MAT_TINT = MATERIALS[MAT]
# --objects-json <path>: emit ONLY the ship's pieces, origin at zero, for the
# host to spawn into the CURRENT world wherever the player stands. The
# standalone .eden mode below stays as the dev CLI.
OBJ_JSON = None
if '--objects-json' in sys.argv:
    OBJ_JSON = sys.argv[sys.argv.index('--objects-json') + 1]
# --rooms-json <path>: SURVEY ONLY -- run the segmentation and naming, emit
# which cell belongs to which named room, build nothing. The drafting table's
# Finalize button uses this so the preview IS the yard's verdict, not a copy
# of its rules.
ROOMS_JSON = None
if '--rooms-json' in sys.argv:
    ROOMS_JSON = sys.argv[sys.argv.index('--rooms-json') + 1]
    ORIGIN_X = ORIGIN_Z = 0.0
rows = [r.rstrip('\n') for r in open(plan_path)]
# The LOFT LINE: an optional trailer 'loft: h h h ...' -- wall-top height per
# station (plan row), bow first. The keel stays flat (ships land); the loft
# sculpts the silhouette. Rows without a value, and plans without the line,
# get the classic WALL_H.
loft_line = None
rev_line = None
rows = [r for r in rows if not (r.startswith('loft:') and (loft_line := r))]
# 'revolve: s' -- lathe the half-plan 180 degrees about the centreline, keel
# flat, dome height = radius * s (an ellipse when s < 1).
rows = [r for r in rows if not (r.startswith('revolve:') and (rev_line := r))]
REVOLVE, REV_GLASS, REV_360 = 0.0, False, False
if rev_line:
    toks = rev_line.split(':', 1)[1].split()
    try: REVOLVE = max(0.0, min(1.0, float(toks[0])))
    except (ValueError, IndexError): pass
    REV_GLASS = 'glass' in toks   # bulkhead fill: glass instead of opaque hull
    REV_360   = '360' in toks     # full revolution -- space hulls, no flat keel
    REV_RIBS  = 'ribs' in toks    # thick cross-rings at boundaries, gap armour
W = max(len(r) for r in rows); H = len(rows)
LOFT = [WALL_H] * H
if loft_line:
    for i, v in enumerate(loft_line.split(':', 1)[1].split()):
        if i < H:
            try: LOFT[i] = max(2.0, min(9.0, float(v)))
            except ValueError: pass
def cell(x, y):
    if 0 <= y < H and 0 <= x < len(rows[y]): return rows[y][x]
    return '_'

walk  = lambda c: c == '.' or c == 'D' or c in ROLE
solid = lambda c: c == '#' or c == 'W' or c == 'X' or c == 'F'   # windows see, exhausts push, fins shed heat
hull  = lambda c: walk(c) or solid(c)

# ---- validation: exhaust grids back onto engine rooms ----------------------
# An X cell is an ion-thruster grid in the hull: machinery, not paint. The
# law: EVERY exhaust cell must touch an engine (E) cell -- a grid with no
# engine behind it is a drawing of a lie, and the yard refuses to build it.
# A grid buried inland (no outside face) gets a warning; it will build, but
# it pushes against the furniture.
exhaust_errors = []
for y in range(H):
    for x in range(W):
        if cell(x, y) != 'X': continue
        nbrs = [cell(x+1, y), cell(x-1, y), cell(x, y+1), cell(x, y-1)]
        if 'E' not in nbrs:
            exhaust_errors.append((x, y))
        if '_' not in nbrs:
            print(f"WARNING: exhaust at ({x},{y}) has no outside face -- an inboard thruster grid")
# The radiator laws, mirror of the exhaust law: every fin (F) must back onto
# a reactor room (P), and every reactor CLUSTER must reach at least one fin
# -- a reactor with no fins is a bomb with a schedule, fins with no reactor
# are jewellery.
radiator_errors = []
for y in range(H):
    for x in range(W):
        if cell(x, y) != 'F': continue
        if 'P' not in [cell(x+1, y), cell(x-1, y), cell(x, y+1), cell(x, y-1)]:
            radiator_errors.append(('F', x, y))
pseen = set()
for y in range(H):
    for x in range(W):
        if cell(x, y) != 'P' or (x, y) in pseen: continue
        blob, stack, finned = [], [(x, y)], False
        while stack:
            px, py = stack.pop()
            if (px, py) in pseen or cell(px, py) != 'P': continue
            pseen.add((px, py)); blob.append((px, py))
            for nx, ny in ((px+1,py),(px-1,py),(px,py+1),(px,py-1)):
                if cell(nx, ny) == 'F': finned = True
                stack.append((nx, ny))
        if not finned:
            radiator_errors.append(('P', blob[0][0], blob[0][1]))

if exhaust_errors or radiator_errors:
    for (x, y) in exhaust_errors:
        print(f"REFUSED: exhaust at ({x},{y}) has no adjacent engine (E) cell -- a grid needs an engine behind it")
    for (kind, x, y) in radiator_errors:
        if kind == 'F':
            print(f"REFUSED: radiator fin at ({x},{y}) has no adjacent reactor (P) cell -- fins with no reactor are jewellery")
        else:
            print(f"REFUSED: reactor room at ({x},{y}) reaches no radiator fin (F) on its walls -- a reactor with no fins is a bomb with a schedule")
    sys.exit(1)

# ---- validation: one connected walkable region, doors that go somewhere ----
seen, start = set(), None
for y in range(H):
    for x in range(W):
        if walk(cell(x, y)): start = (x, y); break
    if start: break
stack = [start]
while stack:
    x, y = stack.pop()
    if (x, y) in seen or not walk(cell(x, y)): continue
    seen.add((x, y))
    stack += [(x+1,y),(x-1,y),(x,y+1),(x,y-1)]
n_walk = sum(1 for y in range(H) for x in range(W) if walk(cell(x, y)))
if len(seen) != n_walk:
    print(f"WARNING: walkable area is not connected ({len(seen)} of {n_walk} reachable)")

# ---- rooms: doors are what separate them -----------------------------------
# Flood walkable cells WITHOUT crossing doors -> each region is a room. Door
# cells then adopt an adjacent room so their floor belongs somewhere. Rooms
# get names -- and the names go onto the floor plates, because a named plate
# is an ADDRESS: ship-to-ship damage lands on a slab, and the slab already
# says "engine_room_deck_2". The user asked for exactly this.
room_of = {}
rooms = []          # list of dicts: cells, letters
for y in range(H):
    for x in range(W):
        c = cell(x, y)
        if not walk(c) or c == 'D' or (x, y) in room_of: continue
        rid = len(rooms)
        blob, letters, stack = [], {}, [(x, y)]
        while stack:
            px, py = stack.pop()
            pc = cell(px, py)
            if (px, py) in room_of or not walk(pc) or pc == 'D': continue
            room_of[(px, py)] = rid
            blob.append((px, py))
            if pc in ROLE: letters[pc] = letters.get(pc, 0) + 1
            stack += [(px+1,py),(px-1,py),(px,py+1),(px,py-1)]
        rooms.append({'cells': blob, 'letters': letters})

# Room centroids first -- door adoption and naming both need them.
ship_cx = sum(x for r in rooms for (x, _) in r['cells']) / max(1, sum(len(r['cells']) for r in rooms))
for r in rooms:
    xs = [p[0] for p in r['cells']]; ys = [p[1] for p in r['cells']]
    r['cx'] = sum(xs)/len(xs); r['cy'] = sum(ys)/len(ys)

# Door cells adopt the adjacent room FARTHEST from the centreline (outboard),
# so port doors belong to the port bay and starboard doors to the starboard
# bay -- the first version adopted west-first and the two sides came out
# different sizes, which the room table caught immediately.
for y in range(H):
    for x in range(W):
        if cell(x, y) != 'D': continue
        best = None
        for nx, ny in ((x, y-1), (x-1, y), (x+1, y), (x, y+1)):
            if (nx, ny) in room_of:
                rid = room_of[(nx, ny)]
                d = abs(rooms[rid]['cx'] - ship_cx)
                if best is None or d > best[0]: best = (d, rid)
        if best:
            room_of[(x, y)] = best[1]
            rooms[best[1]]['cells'].append((x, y))

# Names: equipment first (engine room announces itself), then position --
# forwardmost unnamed room is the bridge (bow is the top of the plan),
# side rooms are port/starboard bays, the rest are holds.
for r in rooms:
    if   'E' in r['letters']: r['name'] = 'engine_room'
    elif 'P' in r['letters']: r['name'] = 'reactor_room'
    elif 'R' in r['letters']: r['name'] = 'robot_hall'
    elif 'C' in r['letters']: r['name'] = 'cargo_hold'
    elif 'B' in r['letters']: r['name'] = 'bridge'
    else: r['name'] = None
# Only a centreline room can be the bridge -- a wing cannot.
centreline = [r for r in rooms if r['name'] is None and abs(r['cx'] - ship_cx) <= 1.5]
if centreline:
    min(centreline, key=lambda r: r['cy'])['name'] = 'bridge'
for r in rooms:
    if r['name'] is None:
        r['name'] = 'port_bay' if r['cx'] < ship_cx - 1 else \
                    'starboard_bay' if r['cx'] > ship_cx + 1 else 'hold'
seen_names = {}
for r in rooms:
    n = seen_names.get(r['name'], 0) + 1
    seen_names[r['name']] = n
    if n > 1: r['name'] += f"_{n}"

if ROOMS_JSON:
    json.dump({"rooms": [{"name": r['name'], "cells": [list(c) for c in r['cells']]}
                         for r in rooms]}, open(ROOMS_JSON, 'w'))
    print(f"survey: {len(rooms)} rooms")
    sys.exit(0)

def rects_over(cells):
    cells = set(cells)
    out = []
    while cells:
        x, y = min(cells, key=lambda p: (p[1], p[0]))
        w = 1
        while (x+w, y) in cells: w += 1
        h = 1
        while all((x+i, y+h) in cells for i in range(w)): h += 1
        for yy in range(y, y+h):
            for xx in range(x, x+w): cells.discard((xx, yy))
        out.append((x, y, w, h))
    return out

floors = []                       # (x, y, w, h, room_name)
for r in rooms:
    for (x, y, w, h) in rects_over(r['cells']):
        floors.append((x, y, w, h, r['name']))
# Floor under the walls too -- the hull frame.
frame_cells = [(x, y) for y in range(H) for x in range(W) if solid(cell(x, y))]
frames = rects_over(frame_cells)

# ---- walls and windows: greedy runs, each material to itself ---------------
wclaimed = [[False]*W for _ in range(H)]
walls, windows = [], []
def run_pass(match, out):
    for y in range(H):
        for x in range(W):
            if wclaimed[y][x] or not match(cell(x, y)): continue
            w = 0
            while x+w < W and not wclaimed[y][x+w] and match(cell(x+w, y)): w += 1
            if w >= 2:
                for i in range(w): wclaimed[y][x+i] = True
                out.append((x, y, w, 1)); continue
            h = 1
            # A vertical run only merges stations of EQUAL loft -- a wall
            # cannot be one box and two heights.
            while y+h < H and not wclaimed[y+h][x] and match(cell(x, y+h)) and LOFT[y+h] == LOFT[y]: h += 1
            for i in range(h): wclaimed[y+i][x] = True
            out.append((x, y, 1, h))
run_pass(lambda c: c == '#', walls)
thrusters = []
run_pass(lambda c: c == 'X', thrusters)
radiators = []
run_pass(lambda c: c == 'F', radiators)

# ---- phase-2 texture: geometry is the skin -------------------------------
# Long wall runs split into short panel segments so the patchwork gets a
# plate rhythm -- each segment its own shade, seams as panel lines.
def split_panels(runs, seg=2):
    out = []
    for (x, y, w, h) in runs:
        if w >= h:
            xx = x
            while xx < x + w:
                sw = min(seg, x + w - xx)
                out.append((xx, y, sw, h)); xx += sw
        else:
            yy = y
            while yy < y + h:
                sh = min(seg, y + h - yy)
                out.append((x, yy, w, sh)); yy += sh
    return out
walls = split_panels(walls)
run_pass(lambda c: c == 'W', windows)

# ---- sockets: flood connected same-letter cells ----------------------------
sclaimed = set()
sockets = []
for y in range(H):
    for x in range(W):
        c = cell(x, y)
        if c not in ROLE or (x, y) in sclaimed: continue
        blob, stack = [], [(x, y)]
        while stack:
            px, py = stack.pop()
            if (px, py) in sclaimed or cell(px, py) != c: continue
            sclaimed.add((px, py)); blob.append((px, py))
            stack += [(px+1,py),(px-1,py),(px,py+1),(px,py-1)]
        xs = [p[0] for p in blob]; ys = [p[1] for p in blob]
        sockets.append((c, min(xs), min(ys), max(xs)-min(xs)+1, max(ys)-min(ys)+1))

# ---- emit ------------------------------------------------------------------
def wx(x, w): return ORIGIN_X + (x + w/2.0 - W/2.0) * CELL
def wz(y, h): return ORIGIN_Z + (y + h/2.0 - H/2.0) * CELL

def prim(name, bt, px, py, pz, sx, sy, sz, color, collide=True, bright=1.0):
    return {"name": name, "buildingType": bt, "modelPath": "",
            "position": [px, py, pz], "rotation": [0.0, 0.0, 0.0],
            "scale": [sx, sy, sz], "primitiveType": 1, "primitiveSize": 1.0,
            "primitiveColor": list(color), "primitiveHeight": 1.0,
            "primitiveRadius": 0.5, "primitiveSegments": 16,
            "aabbCollision": collide, "polygonCollision": False,
            "bulletCollisionType": 0, "beingType": 0, "visible": True,
            "isSkinned": False, "kinematicPlatform": False, "behaviors": [],
            "brightness": bright, "hueShift": 0.0, "saturation": 1.0,
            "dailySchedule": False, "patrolSpeed": 5.0}

import os
stem = os.path.splitext(os.path.basename(plan_path))[0]

objs = []
deck_counts = {}
for (x, y, w, h, rname) in floors:
    deck_counts[rname] = deck_counts.get(rname, 0) + 1
    objs.append(prim(f"{stem}_{rname}_deck_{deck_counts[rname]}", "platform_slab",
                     wx(x, w), FLOOR_Y, wz(y, h),
                     w*CELL, FLOOR_T, h*CELL, (0.42, 0.44, 0.50, 1.0)))
for i, (x, y, w, h) in enumerate(frames):
    objs.append(prim(f"{stem}_frame_{i+1}", "platform_slab",
                     wx(x, w), FLOOR_Y, wz(y, h),
                     w*CELL, FLOOR_T, h*CELL, (0.38, 0.40, 0.45, 1.0)))
deck_top = FLOOR_Y + FLOOR_T
for i, (x, y, w, h) in enumerate(walls):
    objs.append(prim(f"{stem}_wall_{i+1}", "platform_wall",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, LOFT[y], h*CELL, (0.58, 0.60, 0.66, 1.0)))
# Windows: wall-shaped, glass-coloured, nearly transparent -- the pilot's view.
# Same buildingType as walls so collision and the vessel weld treat them as
# hull; only the glazing differs.
for i, (x, y, w, h) in enumerate(windows):
    objs.append(prim(f"{stem}_window_{i+1}", "platform_wall",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, LOFT[y], h*CELL, (0.45, 0.70, 1.00, 0.22)))
# ---- the REVOLVE: the half-plan lathed 180 degrees over the flat keel ------
# Per station the radius is the hull's half-breadth plus one cell of
# clearance; the shell is a stepped surface of axis-aligned boxes (risers and
# annular plates), because collision is sacred and boxes are what the world
# is made of. Steps are honest: this is a lathe drawn in the same voxel hand
# as the rest of her.
if REVOLVE > 0.0:
    half, has_door = [], []
    for y in range(H):
        b = 0.0
        for x in range(W):
            if hull(cell(x, y)):
                b = max(b, abs(x + 0.5 - W / 2.0))
        half.append(b * CELL + CELL if b > 0 else 0.0)
        has_door.append(any(cell(x, y) == 'D' for x in range(W)))
    runs, y0 = [], None
    key = lambda y: (half[y], has_door[y]) if y < H else None
    for y in range(H + 1):
        if y0 is None or key(y) != key(y0):
            if y0 is not None and half[y0] > 0.0:
                runs.append((y0, y - y0, half[y0], has_door[y0]))
            y0 = y
    LAYER = 2.0
    shell_n = 0
    FILL_COL = (0.45, 0.70, 1.00, 0.22) if REV_GLASS else (0.52, 0.55, 0.62, 1.0)

    def shell_box(bt, px, zlo, zhi, sx, pz, szlen, color):
        # One piece of shell, and its mirror below the deck when the revolve
        # is full: 360 hulls have no flat keel -- they live in space.
        global shell_n
        shell_n += 1
        objs.append(prim(f"{stem}_shell_{shell_n}", bt,
                         px, deck_top + zlo, pz, sx, zhi - zlo, szlen, color))
        if REV_360:
            shell_n += 1
            objs.append(prim(f"{stem}_shell_{shell_n}", bt,
                             px, deck_top - zhi, pz, sx, zhi - zlo, szlen, color))

    # Each run's stepped cross-section: layer k -> (z0, z1, outer half-width).
    # Door runs record width 0 at the ground layer -- the arch is an absence
    # the bulkheads must respect.
    def section(R, door):
        DH = R * REVOLVE
        nL = max(1, int(math.ceil(DH / LAYER)))
        sec = {}
        for k in range(nL):
            z0 = k * LAYER
            z1 = min(z0 + LAYER, DH)
            w0 = R * math.sqrt(max(0.0, 1.0 - (z0 / DH) ** 2))
            w1 = R * math.sqrt(max(0.0, 1.0 - (z1 / DH) ** 2))
            if door and k == 0:
                w0 = w1 = 0.0
            sec[k] = (z0, z1, w0, w1)
        return sec

    secs = [section(R, d) for (_, _, R, d) in runs]

    for i, (y, h, R, door) in enumerate(runs):
        DH = R * REVOLVE
        nL = max(1, int(math.ceil(DH / LAYER)))
        for k in range(nL):
            if door and k == 0:
                continue
            z0, z1, w0, _ = secs[i][k]
            w1 = R * math.sqrt(max(0.0, 1.0 - (z1 / DH) ** 2))
            last = (k == nL - 1)
            for side in (-1.0, 1.0):
                shell_box("platform_wall", ORIGIN_X + side * (w0 - 0.2), z0, z1,
                          0.4, wz(y, h), h * CELL, (0.52, 0.55, 0.62, 1.0))
                if not last and (w0 - 0.4) - w1 > 0.05:
                    shell_box("platform_slab", ORIGIN_X + side * (w1 + (w0 - 0.4)) / 2.0,
                              z1 - 0.4, z1, (w0 - 0.4) - w1, wz(y, h), h * CELL,
                              (0.48, 0.51, 0.58, 1.0))
        zTop = (nL - 1) * LAYER
        wTop = R * math.sqrt(max(0.0, 1.0 - (zTop / DH) ** 2)) if nL > 1 else R
        shell_box("platform_slab", ORIGIN_X, DH - 0.4, DH,
                  max(2.0 * (wTop - 0.4), 1.0), wz(y, h), h * CELL,
                  (0.48, 0.51, 0.58, 1.0))
        # the spine: a dorsal ridge along each crown, the ship's backbone
        shell_box("platform_wall", ORIGIN_X, DH, DH + 0.14,
                  0.5, wz(y, h), h * CELL, (0.44, 0.47, 0.54, 1.0))

    # THE BULKHEADS ("could u fill those?"): at every step boundary and both
    # ends, the exposed ring-difference is walled, layer by layer -- opaque
    # hull or glass, the designer's call. Bands butt against the larger run
    # and sit inside the smaller one's territory, so no two top faces share
    # a plane (the z-fighting lesson, kept).
    # A is always the bow (-z) side of the boundary, B the stern (+z) side.
    # Each layer's band shifts toward whichever side is DEFICIENT AT THAT
    # LAYER (per-run shift parked bands inside intact rings -- the detector
    # caught eight coplanar tops). And the rule the trapped-hallway field
    # report taught: BELOW WALL HEIGHT A BULKHEAD MAY ONLY LIVE IN THE
    # ANNULUS between hull wall and shell ring -- full-width bands at ground
    # level walled a corridor straight through the rooms. Above the wall
    # tops, where the dome is the ceiling, full width is correct. Every band
    # splits at the wall line.
    def bulkhead(zb, secA, secB, bHull, hWall):
        layers = sorted(set(secA) | set(secB))
        for k in layers:
            gA = secA.get(k); gB = secB.get(k)
            # The layer's vertical extent is the UNION of both sides' -- a
            # short section's stub layer (say 2.0..2.5) beside a tall one's
            # full layer (2.0..4.0) must not shrink the rib to the stub: the
            # 1.5 left open was the final hole the seam detector found.
            z0 = min(g[0] for g in (gA, gB) if g)
            z1 = max(g[1] for g in (gA, gB) if g)
            wA = gA[2] if gA else 0.0
            wB = gB[2] if gB else 0.0
            if REV_RIBS:
                # THE RIB ("maybe a thicker ring will cover the gaps" -- it
                # does): one generous 1.2-thick cross-ring per layer, spanning
                # from just inside the layer's INNER rim to its outer, inset
                # 3cm from every ring face so nothing is coplanar, shifted
                # into the fuller side so door passages keep their width.
                # The hallway rule still holds: annulus only below the walls.
                # Insets depend on shift direction: two ribs meeting inside
                # a one-cell run arrive from OPPOSITE directions, and equal
                # insets gave them the same planes (the detector's last find).
                fwd = wB > wA
                ins = 0.03 if fwd else 0.06
                wOut = max(wA, wB) - ins
                wIn = max(0.0, min(gA[3] if gA else 1e9, gB[3] if gB else 1e9) - 0.4)
                # No near-match skip: two runs' width curves can CROSS --
                # nearly equal at one layer, wildly different around it --
                # and the skipped layer was exactly where the last thin gaps
                # lived (the seam detector found 4). A rib is gap armour; it
                # pours at every layer.
                if wOut < 0.1:
                    continue
                # Segments of one rib column ABUT: each reaches down to meet
                # the one below, and only the column top keeps its anti-fight
                # inset. The per-layer top-and-bottom insets opened 6-12cm
                # sky slits at every size change (field report: thin purple
                # gaps in the ceiling -- the sky through my own seams).
                zr0, zr1 = max(0.0, z0 - ins), z1 - ins
                zc = zb + (0.57 if fwd else -0.57)
                # The wall-line split is direction-inset too: two ribs
                # entering a one-cell run from opposite ends both topped
                # their annulus segment at exactly hWall -- one plane, one
                # fight, twice.
                zs = max(zr0, min(zr1, hWall - (0.0 if fwd else 0.04)))
                if zs > zr0:
                    lo = max(wIn, bHull)
                    if wOut - lo > 0.05:
                        for side in (-1.0, 1.0):
                            shell_box("platform_wall", ORIGIN_X + side * (lo + wOut) / 2.0,
                                      zr0, zs, wOut - lo, zc, 1.2, FILL_COL)
                if zr1 > zs:
                    if wIn < 0.3:
                        shell_box("platform_wall", ORIGIN_X, zs, zr1,
                                  max(2.0 * wOut, 1.0), zc, 1.2, FILL_COL)
                    else:
                        for side in (-1.0, 1.0):
                            shell_box("platform_wall", ORIGIN_X + side * (wIn + wOut) / 2.0,
                                      zs, zr1, wOut - wIn, zc, 1.2, FILL_COL)
                continue
            if abs(wA - wB) <= 0.05:
                continue
            wLo, wHi = sorted((wA, wB))
            pz = zb + (0.2 if wB < wA else -0.2)
            zs = max(z0, min(z1, hWall))
            # below the wall line: annulus only, both sides
            if zs > z0:
                lo = max(wLo, bHull)
                if wHi - lo > 0.05:
                    for side in (-1.0, 1.0):
                        shell_box("platform_wall", ORIGIN_X + side * (lo + wHi) / 2.0,
                                  z0, zs, wHi - lo, pz, 0.4, FILL_COL)
            # above the wall line: the full exposed face
            if z1 > zs:
                if wLo < 0.3:
                    shell_box("platform_wall", ORIGIN_X, zs, z1,
                              max(2.0 * wHi, 1.0), pz, 0.4, FILL_COL)
                else:
                    for side in (-1.0, 1.0):
                        shell_box("platform_wall", ORIGIN_X + side * (wLo + wHi) / 2.0,
                                  zs, z1, wHi - wLo, pz, 0.4, FILL_COL)

    def boundary_ctx(yb):
        # Hull half-breadth and wall height at a boundary come from the rows
        # on either side of it -- the larger of each, so bands clear both.
        bh, hw = 0.0, 3.0
        for yy in (yb - 1, yb):
            if 0 <= yy < H and half[yy] > 0.0:
                bh = max(bh, half[yy] - CELL)
                hw = max(hw, LOFT[yy])
        return bh, hw

    for i in range(len(runs) + 1):
        prev = runs[i - 1] if i > 0 else None
        nxt = runs[i] if i < len(runs) else None
        if prev is None and nxt is None:
            continue
        if prev is None:                      # bow cap of the first run
            yb = nxt[0]
            bh, hw = boundary_ctx(yb)
            bulkhead((yb - H / 2.0) * CELL + ORIGIN_Z, {}, secs[i], bh, hw)
        elif nxt is None:                     # stern cap of the last run
            yb = prev[0] + prev[1]
            bh, hw = boundary_ctx(yb)
            bulkhead((yb - H / 2.0) * CELL + ORIGIN_Z, secs[i - 1], {}, bh, hw)
        elif prev[0] + prev[1] == nxt[0]:     # a step between adjacent runs
            yb = nxt[0]
            bh, hw = boundary_ctx(yb)
            bulkhead((yb - H / 2.0) * CELL + ORIGIN_Z, secs[i - 1], secs[i], bh, hw)

    print(f"revolve: {shell_n} shell pieces at height scale {REVOLVE:g}"
          + (", glass fill" if REV_GLASS else ", opaque fill")
          + (", ribbed" if REV_RIBS else "")
          + (", full 360" if REV_360 else "")
          + (", door arches cut" if any(d for (_, _, _, d) in runs) else ""))

# GREEBLES -- the details that catch light. All structural (they weld and
# fly), none colliding (trim does not block boots), lamps and vents exempt
# from the patchwork by name.
greeble_n = 0
def greeble(kind, px, py, pz, sx, sy, sz, color, bright=1.0):
    global greeble_n
    greeble_n += 1
    objs.append(prim(f"{stem}_{kind}_{greeble_n}", "platform_wall",
                     px, py, pz, sx, sy, sz, color, collide=False, bright=bright))

# Bow running lights: a row of near-white blocks on the foremost wall's top.
bow_walls = [r for r in walls if r[3] == 1]
if bow_walls:
    bx, by, bw, bh = min(bow_walls, key=lambda r: r[1])
    n_lamp = max(2, min(5, bw))
    for i in range(n_lamp):
        fx = bx + (i + 0.5) * bw / n_lamp
        greeble("lamp", ORIGIN_X + (fx - W / 2.0) * CELL, deck_top + LOFT[by], wz(by, bh),
                0.3, 0.22, 0.3, (1.0, 0.98, 0.88, 1.0), bright=1.6)

# Engine-room vents: dark louvre blocks proud of the outermost hull walls on
# every row the plan marks E.
for y in range(H):
    if not any(cell(x, y) == 'E' for x in range(W)):
        continue
    xs = [x for x in range(W) if solid(cell(x, y))]
    if not xs:
        continue
    for x, sign in ((min(xs), -1.0), (max(xs), 1.0)):
        greeble("vent", ORIGIN_X + (x + 0.5 - W / 2.0) * CELL + sign * (CELL / 2.0 + 0.09),
                deck_top + 0.7, wz(y, 1), 0.18, 1.2, 1.5, (0.18, 0.19, 0.22, 1.0))

# SOCKETS ARE PRICED ("you're paying for each one of the sockets"): a
# robot station is an investment, not a doodle. KEEP IN SYNC with the
# Shipwright's live socket bill.
SOCKET_PRICE = {'helm': 1500.0, 'engine': 2000.0, 'robot': 3500.0, 'cargo': 800.0,
                'power': 2500.0}
socket_cost = 0.0
# Exhaust grids: wall-shaped, charcoal with a hot-orange cast -- machinery
# in the hull, lofted like any wall, welded and weighed like any hull piece.
for i, (x, y, w, h) in enumerate(thrusters):
    objs.append(prim(f"{stem}_thruster_grid_{i+1}", "platform_wall",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, LOFT[y], h*CELL, (0.42, 0.24, 0.13, 1.0)))

# Radiator fins: pale heat-shedding panels in the hull, lofted like walls.
for i, (x, y, w, h) in enumerate(radiators):
    objs.append(prim(f"{stem}_radiator_{i+1}", "platform_wall",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, LOFT[y], h*CELL, (0.66, 0.71, 0.76, 1.0)))

counts = {}
for (c, x, y, w, h) in sockets:
    role, color = ROLE[c]
    counts[role] = counts.get(role, 0) + 1
    socket_cost += SOCKET_PRICE.get(role, 500.0)
    objs.append(prim(f"Socket_{role}_{counts[role]}", "socket_marker",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, 0.06, h*CELL, color, collide=False))
    objs[-1]["metadata"] = {"socket": role}

# ---- the material is applied and the bill is drawn up ----------------------
# Structural pieces (plates, frames, walls, windows) take the material's
# density/armor as metadata -- the helm's weighing reads density off each
# plate -- and its tint into their colour. Sockets are painted intent and
# take neither. The bill is volume times price, whole hull.
# THE PANEL PATCHWORK: every plate is subtly its own -- a stable value
# jitter seeded by the piece's NAME (crc32, never python's salted hash: the
# same ship must wear the same skin on every build), and roughly one panel
# in eight a distinctly darker replacement plate, the way real hulls
# remember their repairs. Glass keeps its glaze; sockets keep their role
# colours. The material tint multiplies on top, so a diamondoid hull
# patchworks in diamondoid.
def patchwork(name, c):
    if c[3] < 0.9:
        return c
    if "_lamp_" in name or "_vent_" in name:
        return c
    h = zlib.crc32(name.encode())
    v = 0.92 + ((h >> 8) & 0xFF) / 255.0 * 0.16
    out = [min(1.0, c[0]*v), min(1.0, c[1]*v), min(1.0, c[2]*v), c[3]]
    if (h & 7) == 0:
        out = [out[0]*0.72, out[1]*0.72, out[2]*0.72, c[3]]
    return out

cost_cr = 0.0
for o in objs:
    if o["buildingType"] in ("platform_slab", "platform_wall"):
        sx, sy, sz = o["scale"]
        cost_cr += sx * sy * sz * MAT_PRICE
        o["metadata"] = {"material": MAT_KEY, "density": f"{MAT_DENSITY:g}",
                         "armor": str(MAT_ARMOR)}
        c = patchwork(o["name"], o["primitiveColor"])
        o["primitiveColor"] = [min(1.0, c[0]*MAT_TINT[0]), min(1.0, c[1]*MAT_TINT[1]),
                               min(1.0, c[2]*MAT_TINT[2]), c[3]]
cost_cr = round(cost_cr + socket_cost)

if OBJ_JSON:
    json.dump({"objects": objs, "cost_cr": cost_cr, "material": MAT_NAME},
              open(OBJ_JSON, 'w'))
    print(f"materials: {MAT_NAME} -- {cost_cr} CR (incl. {round(socket_cost)} CR of sockets)")
    print("rooms:")
    for r in rooms:
        print(f"  {r['name']:16s} {len(r['cells']):3d} cells   "
              f"{deck_counts.get(r['name'], 0)} deck plate(s)   letters {r['letters'] or '--'}")
    print(f"{OBJ_JSON}: {len(objs)} pieces for in-world spawn "
          f"({len(floors)} room plates, {len(frames)} frame plates, {len(walls)} walls, "
          f"{len(windows)} windows, {len(sockets)} sockets)")
    sys.exit(0)

src = json.load(open('build/examples/terrain_editor/levels/shipyard.eden'))
out = copy.deepcopy(src)
out['objects'] = objs
stern_z = ORIGIN_Z + (max(y+h for (x,y,w,h,_) in floors) - H/2.0) * CELL
out['settings']['spawnPosition'] = [ORIGIN_X, 2.0, stern_z + 8.0]
out['name'] = stem
dst = f'build/examples/terrain_editor/levels/{stem}.eden'
json.dump(out, open(dst, 'w'), indent=1)

print("rooms:")
for r in rooms:
    print(f"  {r['name']:16s} {len(r['cells']):3d} cells   "
          f"{deck_counts.get(r['name'], 0)} deck plate(s)   letters {r['letters'] or '--'}")
print(f"{dst}: {len(floors)} room plates + {len(frames)} frame plates, {len(walls)} wall runs, "
      f"{sum(1 for y in range(H) for x in range(W) if cell(x,y)=='D')} door cells, "
      f"{len(sockets)} sockets {[(ROLE[c][0], w, h) for (c,x,y,w,h) in sockets]}")
if not any(c == 'B' for (c, *_ ) in sockets):
    print("note: no B cells drawn -- the forward compartment is presumably the "
          "bridge; draw B where the helm should stand, or place one from the catalog.")
