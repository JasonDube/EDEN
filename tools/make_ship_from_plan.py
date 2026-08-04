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
# The plan is a STACK OF LAYERS now. The first grid is deck 1. A 'deckN:'
# marker starts the grid for storey N at the same coordinates -- what stands
# on the roof of the storey below; every letter of the language lives up
# there, walls and rooms and sockets AND masts. 'topside:' survives as the
# legacy alias for layer 2 (it only ever carried A masts, and that meaning
# is unchanged: A = a mast rooted on the roof below it).
# Trailers (loft:/revolve:) are recognised wherever they appear -- but a
# stacked ship may not loft or revolve; that is the price of storeys.
loft_line = None
rev_line = None
layers = [[]]
_target = layers[0]
for _r in [r.rstrip('\n') for r in open(plan_path)]:
    if _r.startswith('loft:'):
        # The LOFT LINE: wall-top height per station (plan row), bow first.
        # The keel stays flat (ships land); the loft sculpts the silhouette.
        loft_line = _r
    elif _r.startswith('revolve:'):
        # 'revolve: s' -- lathe the half-plan 180 degrees about the
        # centreline, keel flat, dome height = radius * s.
        rev_line = _r
    elif _r.startswith('topside:') or _r.startswith('deck'):
        if _r.startswith('topside:'):
            k = 1
        else:
            try:
                k = int(_r[4:].rstrip(':')) - 1
            except ValueError:
                _target.append(_r)
                continue
        while len(layers) <= k:
            layers.append([])
        _target = layers[k]
    else:
        _target.append(_r)
rows = layers[0]
# A REAL upper deck has structure; a layer of nothing but masts is the old
# topside and stays revolve-friendly.
def _layer_has_structure(g):
    return any(c not in '_A' and not c.isspace() for r in g for c in r)
MULTI_DECK = any(_layer_has_structure(g) for g in layers[1:])
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

walk  = lambda c: c == '.' or c == 'D' or c == 'L' or c in ROLE
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

# ---- the stacking laws ------------------------------------------------------
# Layer access for every storey above the first. A is a mast wherever it
# appears in an upper layer; everything else up there is deck structure.
def lcell(k, x, y):
    g = layers[k] if k < len(layers) else []
    if 0 <= y < len(g) and 0 <= x < len(g[y]):
        c = g[y][x]
        if c == 'A' or c == '#' or c == '.' or c == 'D' or c == 'W' \
           or c == 'X' or c == 'F' or c == 'L' or c in ROLE:
            return c
    return '_'

def lhull(k, x, y):
    # The load-bearing footprint of a storey (masts are not floors).
    if k == 0:
        return hull(cell(x, y))
    c = lcell(k, x, y)
    return c != '_' and c != 'A'

stack_errors = []
if MULTI_DECK and (loft_line or rev_line):
    stack_errors.append("a stacked ship may not loft or revolve -- storeys are "
                        "the flat-built method's own reward")
# THE ANCHOR LAW (user's correction: "a larger floor CAN go on top of a
# smaller one -- the roof below is also the floor above"): a storey need
# not stand cell-for-cell on the one below. It may CANTILEVER -- the roof
# plate pours to the union of both footprints -- but every connected piece
# of a storey must touch the storey below in at least one cell. An island
# with no anchor is not an overhang, it is a drawing of a crash.
for k in range(1, len(layers)):
    seen_k = set()
    for y in range(H):
        for x in range(W):
            if lcell(k, x, y) in ('_', 'A') or (x, y) in seen_k:
                continue
            comp, stack, anchored = [], [(x, y)], False
            while stack:
                px, py = stack.pop()
                if (px, py) in seen_k or lcell(k, px, py) in ('_', 'A'):
                    continue
                seen_k.add((px, py)); comp.append((px, py))
                if lhull(k - 1, px, py):
                    anchored = True
                stack += [(px+1,py),(px-1,py),(px,py+1),(px,py-1)]
            if not anchored:
                stack_errors.append(f"deck {k+1} section at ({comp[0][0]},{comp[0][1]}) "
                                    f"touches nothing below -- an overhang needs an anchor")
# The machinery laws hold on every storey: exhaust wants its engine, fins
# want their reactor, reactors want their fins -- per deck, same words.
for k in range(1, len(layers)):
    for y in range(H):
        for x in range(W):
            c = lcell(k, x, y)
            nbrs = [lcell(k, x+1, y), lcell(k, x-1, y), lcell(k, x, y+1), lcell(k, x, y-1)]
            if c == 'X' and 'E' not in nbrs:
                exhaust_errors.append((x, y))
            if c == 'F' and 'P' not in nbrs:
                radiator_errors.append(('F', x, y))
    pseen_k = set()
    for y in range(H):
        for x in range(W):
            if lcell(k, x, y) != 'P' or (x, y) in pseen_k: continue
            blob, stk, finned = [], [(x, y)], False
            while stk:
                px, py = stk.pop()
                if (px, py) in pseen_k or lcell(k, px, py) != 'P': continue
                pseen_k.add((px, py)); blob.append((px, py))
                for nx, ny in ((px+1,py),(px-1,py),(px,py+1),(px,py-1)):
                    if lcell(k, nx, ny) == 'F': finned = True
                    stk.append((nx, ny))
            if not finned:
                radiator_errors.append(('P', blob[0][0], blob[0][1]))

# ---- the mast law: masts stand over hull -----------------------------------
# A mast (an 'A' in any upper layer) roots on the roof of the storey below
# it. The law: it must stand OVER that storey's structure -- an antenna
# rooted in vacuum hails nobody, and the yard refuses to plant one.
mast_errors = []
for k in range(1, len(layers)):
    for y in range(H):
        for x in range(W):
            if lcell(k, x, y) != 'A':
                continue
            beside = any(lcell(k, nx, ny) not in ('_', 'A')
                         for nx, ny in ((x+1,y),(x-1,y),(x,y+1),(x,y-1)))
            if not lhull(k - 1, x, y) and not beside:
                mast_errors.append((x, y))

# ---- THE LIFT LAW -----------------------------------------------------------
# An L cell is an elevator. Drawn on ANY floor of a column, it serves every
# deck that is open at that column -- all the way down, all the way up --
# and the yard cuts its shaft through every roof between served floors (the
# TOP roof stays closed; the lift is not a chimney). Adjacent L cells make
# one wider car. A shaft with a blocked middle deck is refused.
STOREY = WALL_H + FLOOR_T

def deck_walk(d, x, y):
    if d == 0:
        return walk(cell(x, y))
    c = lcell(d, x, y)
    return c == '.' or c == 'D' or c == 'L' or c in ROLE

n_decks = 1 + sum(1 for k in range(1, len(layers)) if _layer_has_structure(layers[k]))
lift_cells = set()
for y in range(H):
    for x in range(W):
        if cell(x, y) == 'L':
            lift_cells.add((x, y))
for k in range(1, len(layers)):
    for y in range(H):
        for x in range(W):
            if lcell(k, x, y) == 'L':
                lift_cells.add((x, y))
lifts = []            # (cells, served deck indices, stops)
SHAFT_HOLES = {}      # deck index -> set of cells whose roof stays open
_ls = set()
for (sx0, sy0) in sorted(lift_cells):
    if (sx0, sy0) in _ls:
        continue
    blob, stk = [], [(sx0, sy0)]
    while stk:
        px, py = stk.pop()
        if (px, py) in _ls or (px, py) not in lift_cells:
            continue
        _ls.add((px, py)); blob.append((px, py))
        stk += [(px+1,py),(px-1,py),(px,py+1),(px,py-1)]
    served = [d for d in range(n_decks)
              if all(deck_walk(d, x, y) for (x, y) in blob)]
    if len(served) < 2:
        print(f"WARNING: lift at ({blob[0][0]},{blob[0][1]}) serves "
              f"{len(served)} floor(s) -- a very expensive tile; no car built")
        continue
    if served != list(range(served[0], served[-1] + 1)):
        stack_errors.append(f"lift shaft at ({blob[0][0]},{blob[0][1]}) is blocked "
                            f"between floors -- clear the column or move the car")
        continue
    stops = [FLOOR_Y + d * STOREY + FLOOR_T for d in served]
    lifts.append((blob, served, stops))
    for d in served[:-1]:
        SHAFT_HOLES.setdefault(d, set()).update(blob)

if exhaust_errors or radiator_errors or mast_errors or stack_errors:
    for e in stack_errors:
        print(f"REFUSED: {e}")
    for (x, y) in exhaust_errors:
        print(f"REFUSED: exhaust at ({x},{y}) has no adjacent engine (E) cell -- a grid needs an engine behind it")
    for (kind, x, y) in radiator_errors:
        if kind == 'F':
            print(f"REFUSED: radiator fin at ({x},{y}) has no adjacent reactor (P) cell -- fins with no reactor are jewellery")
        else:
            print(f"REFUSED: reactor room at ({x},{y}) reaches no radiator fin (F) on its walls -- a reactor with no fins is a bomb with a schedule")
    for (x, y) in mast_errors:
        print(f"REFUSED: mast at ({x},{y}) stands over no hull -- an antenna rooted in vacuum hails nobody")
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

# ---- ROOFS: the honest lid --------------------------------------------------
# Flat-built ships were open to the sky -- floors, walls, and then nothing.
# Every room and every wall-top now gets a roof plate at its loft height,
# named like the decks are named (an address is an address on both faces).
# A revolve hull skips this: the dome already closes her, and a roof inside
# a dome is wasted tonnage. The roof of one storey is the floor of the next.
ROOF_COL = (0.40, 0.42, 0.47, 1.0)

def pour_roof_rects(cells):
    # Rectangles may not span rows of different loft -- a roof cannot be
    # one plate at two heights. Group by loft first, rect within.
    by_loft = {}
    for (x, y) in cells:
        by_loft.setdefault(LOFT[y], []).append((x, y))
    for lv in sorted(by_loft):
        for (x, y, w, h) in rects_over(by_loft[lv]):
            yield (x, y, w, h, lv)

if REVOLVE <= 0.0:
    roof_counts = {}
    hole0 = SHAFT_HOLES.get(0, set())
    for r in rooms:
        for (x, y, w, h, lv) in pour_roof_rects([c for c in r['cells'] if c not in hole0]):
            roof_counts[r['name']] = roof_counts.get(r['name'], 0) + 1
            objs.append(prim(f"{stem}_{r['name']}_roof_{roof_counts[r['name']]}",
                             "platform_slab", wx(x, w), deck_top + lv, wz(y, h),
                             w*CELL, FLOOR_T, h*CELL, ROOF_COL))
    for i, (x, y, w, h, lv) in enumerate(pour_roof_rects(frame_cells)):
        objs.append(prim(f"{stem}_frame_roof_{i+1}", "platform_slab",
                         wx(x, w), deck_top + lv, wz(y, h),
                         w*CELL, FLOOR_T, h*CELL, ROOF_COL))
    # THE CANTILEVER POUR: where the storey above reaches past this hull,
    # its floor still has to exist -- the roof below is also the floor
    # above (the user's law), so the plate extends to the union.
    if len(layers) > 1:
        over0 = [(x, y) for y in range(H) for x in range(W)
                 if lcell(1, x, y) != '_' and not hull(cell(x, y))]
        for i, (x, y, w, h) in enumerate(rects_over(over0)):
            objs.append(prim(f"{stem}_overhang_d2_{i+1}", "platform_slab",
                             wx(x, w), deck_top + WALL_H, wz(y, h),
                             w*CELL, FLOOR_T, h*CELL, ROOF_COL))
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

    # REAL NAMES ("rename the shell blocks with real names"): every shell
    # piece signs what it is and where -- ring_r3_L2_port, rib_row14_L1_lo,
    # spine_r5, cap_bow_L1 -- because the user reports bad blocks BY NAME
    # off the HUD, and shell_213 told nobody anything. The uniquifier keeps
    # names stable build-to-build (scan order) and collision-free; the
    # patchwork crc rides the name, so this re-rolls the skin exactly once.
    shell_names = {}
    def shell_name(base):
        n = shell_names.get(base, 0) + 1
        shell_names[base] = n
        return f"{stem}_{base}" if n == 1 else f"{stem}_{base}_{n}"

    def belly_box(name, bt, px, zlo, zhi, sx, pz, szlen, color):
        # A piece of the BELLY only -- used where the mirrored copy must
        # differ from the upper one (the cap bands: annulus above the deck
        # to spare the rooms, full width below because the belly has no
        # rooms to spare).
        global shell_n
        shell_n += 1
        objs.append(prim(shell_name(name), bt,
                         px, deck_top - zhi, pz, sx, zhi - zlo, szlen, color))

    def shell_box(name, bt, px, zlo, zhi, sx, pz, szlen, color, collide=True, mirror=True):
        # One piece of shell, and its mirror below the deck when the revolve
        # is full: 360 hulls have no flat keel -- they live in space. The
        # mirror signs itself "_belly".
        # (collide=False briefly marked full-width pieces visual-only to
        # cure interior hauntings; superseded same day by MODE ghosts: play
        # is solid everywhere -- walk and jump the dome -- and the painter
        # flies through everything. The parameter stays for future use.)
        global shell_n
        # No shell piece touches the deck plane: pieces that start at 0 are
        # lifted (and their belly mirrors stop short of it) -- ring L1 sat
        # exactly on y=deck and shimmered against every frame top and wall
        # bottom along the waterline (fightcheck). The belt is radially
        # backed by deck plates inboard and unthreadable by any straight
        # ray outboard; leakcheck referees.
        # THE PLANE LEDGER -- every family owns a distinct offset, or the
        # lifts just relocate the shimmer (0.03 here collided with the
        # bands' SH and the ribs' ins, both also 0.03):
        #   bands/caps ride SH = 0.03 and recess 0.02 in pz;
        #   ribs keep their directional ins 0.03/0.06;
        #   rings and all other shell pieces lift 0.045.
        zl = max(zlo, 0.045)
        if zhi - zl <= 0.0:
            return
        shell_n += 1
        objs.append(prim(shell_name(name), bt,
                         px, deck_top + zl, pz, sx, zhi - zl, szlen, color,
                         collide=collide))
        if REV_360 and mirror:
            shell_n += 1
            objs.append(prim(shell_name(name + "_belly"), bt,
                             px, deck_top - zhi, pz, sx, zhi - zl, szlen, color,
                             collide=collide))

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
            # SOLID RING BLOCKS, not riser-plus-shingle. The old thin plates
            # (0.4) covered each layer's top but left see-through seams
            # wherever adjacent runs' rims differed ("longitudinally the
            # steps reveal gaps"). Each layer is now ONE solid block per
            # side: full layer height, inner rim to outer -- the staircase
            # is stacked stone, and there is nothing to see between. Costs
            # honest tonnage; buys honest armour.
            lo = min(w1, w0 - 0.35)
            for side in (-1.0, 1.0):
                ps = "port" if side < 0 else "stbd"
                shell_box(f"ring_r{i+1}_L{k+1}_{ps}", "platform_wall",
                          ORIGIN_X + side * (lo + w0) / 2.0, z0, z1,
                          w0 - lo, wz(y, h), h * CELL, (0.52, 0.55, 0.62, 1.0))
        # (The crown plate retired with the solid-block change: the top
        # layer's blocks reach the centreline themselves -- w1 goes to zero
        # at the apex -- so the dome closes without a lid, and the lid was
        # lying coplanar on the blocks, fighting them.)
        # the spine: a dorsal ridge along each crown, the ship's backbone
        shell_box(f"spine_r{i+1}", "platform_wall", ORIGIN_X, DH, DH + 0.14,
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
    def bulkhead(zb, secA, secB, bHull, hWall, tag):
        # `tag` names the boundary in every piece it pours: bow, stern, or
        # row<N> -- the plan row the step lives at, readable off the HUD.
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
                # Missing side counts as rim zero -- end caps pour full, not
                # as rings with open centres (the leakcheck lesson, applied
                # to the rib path's same line).
                wIn = max(0.0, min(gA[3] if gA else 0.0, gB[3] if gB else 0.0) - 0.4)
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
                # An L1 rib clamped to zero sat ON the deck plane (220
                # fights); a 0.03 floor then landed under shell_box's 0.045
                # ring lift and shimmered against ring bottoms instead. The
                # floor rides ABOVE the ring plane and stays directional,
                # so opposed ribs never share it either (the plane ledger:
                # rings 0.045, ribs 0.075/0.105).
                zr0, zr1 = max(0.045 + ins, z0 - ins), z1 - ins
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
                            ps = "port" if side < 0 else "stbd"
                            shell_box(f"rib_{tag}_L{k+1}_lo_{ps}", "platform_wall",
                                      ORIGIN_X + side * (lo + wOut) / 2.0,
                                      zr0, zs, wOut - lo, zc, 1.2, FILL_COL, mirror=False)
                    if REV_360:
                        # The belly cap: full width -- no rooms below deck.
                        # Column abuts (leakcheck), rides 3cm off the layer
                        # grid (fightcheck: L1 cap tops sat exactly on the
                        # deck plane and shimmered against every floor),
                        # and is half-a-cell thick so opposite caps abut at
                        # a one-cell run's midline instead of overlapping.
                        belly_box(f"cap_{tag}_L{k+1}", "platform_wall",
                                  ORIGIN_X, z0 + 0.03, zs + 0.03,
                                  max(2.0 * (wOut - ins), 1.0),
                                  zb + (0.52 if fwd else -0.52), 1.0, FILL_COL)
                if zr1 > zs:
                    if wIn < 0.3:
                        shell_box(f"rib_{tag}_L{k+1}_hi", "platform_wall",
                                  ORIGIN_X, zs, zr1,
                                  max(2.0 * wOut, 1.0), zc, 1.2, FILL_COL)
                    else:
                        for side in (-1.0, 1.0):
                            ps = "port" if side < 0 else "stbd"
                            shell_box(f"rib_{tag}_L{k+1}_hi_{ps}", "platform_wall",
                                      ORIGIN_X + side * (wIn + wOut) / 2.0,
                                      zs, zr1, wOut - wIn, zc, 1.2, FILL_COL)
                continue
            if abs(wA - wB) <= 0.05:
                continue
            wLo, wHi = sorted((wA, wB))
            # THE INNER LIP (leakcheck conviction, 2026-08-03): a stepped
            # dome has TWO rims at every boundary -- the outer lip between
            # the runs' outer edges, and the inner lip where the ceiling
            # inside jumps curves. These bands covered only [wLo..wHi], the
            # outer lip; the ribs always covered rim to rim (their wIn).
            # Gentle curves keep the lips close and hid this; a full-height
            # revolve pulled them apart and every gradation leaked sky.
            # A MISSING side (bow/stern end cap) counts as rim ZERO -- the
            # leak detector convicted the first draft of this line (1e9 for
            # the empty side turned the nose cap into a ring with an open
            # centre, and escapes went UP).
            wIn = max(0.0, min(gA[3] if gA else 0.0, gB[3] if gB else 0.0) - 0.4)
            # BAND THICKNESS: 1.0, not the old 0.4 foil (field report: "half
            # as thick as it should be", holes in un-ribbed hulls) and not
            # 1.2 either -- exactly HALF A CELL, so two bands entering a
            # one-cell run from opposite boundaries ABUT at its midline
            # instead of overlapping (overlap = coplanar tops = shimmer,
            # fightcheck conviction). One face still lands on the boundary.
            BAND_T = 1.0
            # THE GRID SHIFT: bands and caps ride 3cm high of the LAYER
            # grid. Rings, decks and bands all lived on the same y-planes,
            # and every shared plane with overlap was a z-fight (576 pairs,
            # counted). All boundary pieces shift TOGETHER, so every
            # abutment inside the column survives; the belly mirrors shift
            # down with their originals. leakcheck stays the referee that
            # no slit reopened.
            SH = 0.03
            # ...and 2cm off the boundary plane in z (the ribs' old trick,
            # their 0.03/0.06 insets): the band front shared its plane with
            # ring END faces and deck-plate edges, and shimmered wherever
            # the larger run's solid did not hide it. Recessed 2cm, the
            # seam reads as a panel line; a straight ray cannot turn into
            # a 2cm groove, and leakcheck confirms.
            pz = zb + (BAND_T / 2.0 + 0.02 if wB < wA else -(BAND_T / 2.0 + 0.02))
            zs = max(z0, min(z1, hWall))
            # below the wall line: annulus only ABOVE deck (the hallway rule
            # protects rooms); the belly has no rooms, so its cap is full.
            if zs > z0:
                lo = max(wLo, bHull)
                if wHi - lo > 0.05:
                    for side in (-1.0, 1.0):
                        ps = "port" if side < 0 else "stbd"
                        shell_box(f"bulk_{tag}_L{k+1}_lo_{ps}", "platform_wall",
                                  ORIGIN_X + side * (lo + wHi) / 2.0,
                                  z0 + SH, zs + SH, wHi - lo, pz, BAND_T, FILL_COL,
                                  mirror=False)
                if REV_360:
                    insB = 0.04 if wB < wA else 0.07
                    # The column abuts: cap bottom meets the cap below, cap
                    # top meets the hi band -- all riding the same shift.
                    belly_box(f"cap_{tag}_L{k+1}", "platform_wall",
                              ORIGIN_X, z0 + SH, zs + SH,
                              max(2.0 * (wHi - insB), 1.0), pz, BAND_T, FILL_COL)
            # above the wall line: the full exposed face
            if z1 > zs:
                # THE APEX SLIVER, reported by name off the HUD (row15_L5,
                # row21_L4, row30_L4, all _hi_belly, all keel-line): the
                # dome height rarely divides evenly into layers, so the last
                # layer is a remainder -- 0.5 of band sealing the boundary
                # exactly where the belly runs deepest. A keel band is
                # armour, not foil: short bands grow DOWN into the layer
                # below to the rib gauge, nudged off the shared boundary
                # plane so they cannot z-fight the band they now overlap.
                # Only when the wall-line split is not in play (zs == z0) --
                # a full-width band must never dig below the wall tops (the
                # hallway rule), and the slivers all live near the apex,
                # far above the rooms.
                zlo, poff = zs, 0.0
                if z1 - zs < 1.2 and zs <= z0 + 1e-6 and z1 - 1.2 >= hWall:
                    zlo = z1 - 1.2
                    poff = 0.04 if wB < wA else -0.04
                if wIn < 0.3:
                    shell_box(f"bulk_{tag}_L{k+1}_hi", "platform_wall",
                              ORIGIN_X, zlo + SH, z1 + SH,
                              max(2.0 * wHi, 1.0), pz + poff, BAND_T, FILL_COL)
                else:
                    for side in (-1.0, 1.0):
                        ps = "port" if side < 0 else "stbd"
                        shell_box(f"bulk_{tag}_L{k+1}_hi_{ps}", "platform_wall",
                                  ORIGIN_X + side * (wIn + wHi) / 2.0,
                                  zlo + SH, z1 + SH, wHi - wIn, pz + poff, BAND_T,
                                  FILL_COL)

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
            bulkhead((yb - H / 2.0) * CELL + ORIGIN_Z, {}, secs[i], bh, hw, "bow")
        elif nxt is None:                     # stern cap of the last run
            yb = prev[0] + prev[1]
            bh, hw = boundary_ctx(yb)
            bulkhead((yb - H / 2.0) * CELL + ORIGIN_Z, secs[i - 1], {}, bh, hw, "stern")
        elif prev[0] + prev[1] == nxt[0]:     # a step between adjacent runs
            yb = nxt[0]
            bh, hw = boundary_ctx(yb)
            bulkhead((yb - H / 2.0) * CELL + ORIGIN_Z, secs[i - 1], secs[i], bh, hw,
                     f"row{yb}")

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
    # Lamps stand on the ROOF now, not the bare wall top -- flat ships have
    # lids (the roof plate claimed the wall-top plane and the lamps fought
    # its underside, fightcheck's smallest-ever conviction: 0.09 u^2).
    # ...and 2cm proud of the roof plane, where storey walls also stand.
    lamp_y = deck_top + LOFT[by] + (FLOOR_T + 0.02 if REVOLVE <= 0.0 else 0.0)
    for i in range(n_lamp):
        fx = bx + (i + 0.5) * bw / n_lamp
        greeble("lamp", ORIGIN_X + (fx - W / 2.0) * CELL, lamp_y, wz(by, bh),
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

# ---- LIFT CARS --------------------------------------------------------------
# The car rides 2cm proud of its floor (no plane shared with the deck) and
# a hair inside its shaft, and carries its stop list on the metadata rail.
# In play, E sends it to the next floor up; the top wraps back to the
# bottom. The walk path's ground-follow does the actual carrying.
for li, (blob, served, stops) in enumerate(lifts):
    xs = [p[0] for p in blob]; ys = [p[1] for p in blob]
    lx, ly = min(xs), min(ys)
    lw, lh = max(xs) - lx + 1, max(ys) - ly + 1
    o = prim(f"{stem}_lift_{li+1}", "platform_slab",
             wx(lx, lw), stops[0] + 0.02, wz(ly, lh),
             lw * CELL - 0.3, 0.12, lh * CELL - 0.3, (0.55, 0.85, 0.35, 1.0))
    o["metadata"] = {"lift": "1",
                     "lift_stops": ",".join(f"{s + 0.02:.2f}" for s in stops)}
    objs.append(o)
if lifts:
    print(f"lifts: {len(lifts)} car(s), serving "
          f"{'/'.join(str(len(s[1])) + ' floors' for s in lifts)}")

# ---- MASTS: the comms era begins topside ------------------------------------
# Connected A cells cluster into ONE mast ("letters make sockets" holds above
# deck too) -- a bigger cluster is a heavier array: taller pole, a crossarm.
# The mast roots on whatever the hull raises at its cell: the wall top, or
# the revolve's dome at that station. The pole carries comms metadata on the
# rail -- the day hails and remote robot orders are gated by hardware, this
# is the piece that carries the voice. Priced like the investment it is.
MAST_PRICE_BASE, MAST_PRICE_CELL = 1200.0, 400.0
STOREY = WALL_H + FLOOR_T
mast_cost = 0.0
mast_blobs = []          # (layer k, [cells])
for _k in range(1, len(layers)):
    tseen = set()
    for y in range(H):
        for x in range(W):
            if lcell(_k, x, y) != 'A' or (x, y) in tseen:
                continue
            blob, stack = [], [(x, y)]
            while stack:
                px, py = stack.pop()
                if (px, py) in tseen or lcell(_k, px, py) != 'A':
                    continue
                tseen.add((px, py)); blob.append((px, py))
                stack += [(px+1, py), (px-1, py), (px, py+1), (px, py-1)]
            mast_blobs.append((_k, blob))

def crown_height(x, y):
    """Where the hull's top is at plan cell (x,y) -- wall top, or the dome."""
    h = deck_top
    if solid(cell(x, y)):
        h += LOFT[y]
    if REVOLVE > 0.0:
        b = 0.0
        for xx in range(W):
            if hull(cell(xx, y)):
                b = max(b, abs(xx + 0.5 - W / 2.0))
        R = b * CELL + CELL if b > 0 else 0.0
        if R > 0.0:
            woff = abs(x + 0.5 - W / 2.0) * CELL
            if woff < R:
                h = max(h, deck_top + R * REVOLVE *
                        math.sqrt(1.0 - (woff / R) ** 2))
    return h

MAST_STEEL = (0.30, 0.31, 0.36, 1.0)
MAST_AMBER = (0.95, 0.62, 0.18, 1.0)
for mi, (mk, blob) in enumerate(mast_blobs):
    ncells = len(blob)
    mast_cost += MAST_PRICE_BASE + MAST_PRICE_CELL * (ncells - 1)
    cx = sum(p[0] for p in blob) / ncells
    cy = sum(p[1] for p in blob) / ncells
    px = ORIGIN_X + (cx + 0.5 - W / 2.0) * CELL
    pz = ORIGIN_Z + (cy + 0.5 - H / 2.0) * CELL
    # The root: a dome's crown on a revolve hull; otherwise the roof-top of
    # the storey below the mast's layer (roofs exist now -- a mast on a
    # flat ship stands on the lid, not inside the room).
    if REVOLVE > 0.0:
        base = crown_height(int(round(cx)), int(round(cy))) - 0.3
    elif mk == 1:
        base = deck_top + LOFT[int(round(cy))] + FLOOR_T - 0.3
    else:
        base = FLOOR_Y + mk * STOREY + FLOOR_T - 0.3
    pole_h = 3.5 + 1.5 * math.sqrt(ncells)
    objs.append(prim(f"{stem}_mast_comms_{mi+1}", "platform_wall",
                     px, base, pz, 0.35, pole_h, 0.35, MAST_STEEL))
    objs[-1]["metadata"] = {"mast": "comms", "comms": "1"}
    if ncells >= 2:                       # a heavier array earns a crossarm
        objs.append(prim(f"{stem}_mast_comms_{mi+1}_arm", "platform_wall",
                         px, base + pole_h * 0.78, pz, 2.4, 0.18, 0.18,
                         MAST_STEEL, collide=False))
    for j, frac in enumerate((0.55, 0.9)):
        objs.append(prim(f"{stem}_mast_comms_{mi+1}_collar_{j+1}",
                         "platform_wall", px, base + pole_h * frac, pz,
                         0.8, 0.22, 0.8, MAST_AMBER, collide=False,
                         bright=1.8))
    objs.append(prim(f"{stem}_mast_comms_{mi+1}_lamp", "platform_wall",
                     px, base + pole_h + 0.25, pz, 0.26, 0.26, 0.26,
                     (1.0, 0.95, 0.8, 1.0), collide=False, bright=2.3))

counts = {}
for (c, x, y, w, h) in sockets:
    role, color = ROLE[c]
    counts[role] = counts.get(role, 0) + 1
    socket_cost += SOCKET_PRICE.get(role, 500.0)
    objs.append(prim(f"Socket_{role}_{counts[role]}", "socket_marker",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, 0.06, h*CELL, color, collide=False))
    objs[-1]["metadata"] = {"socket": role}

# ---- THE STOREYS: build on the roof, roof it, build again -------------------
# Each real upper layer is a full deck plan standing on the roof below it
# (the support law already swore to that). No loft, no revolve up here --
# storeys are the flat method's own reward -- so walls are the classic
# height and every storey pours its own roof, which is the next one's
# floor. Rooms get named with their storey (_d2, _d3...), sockets keep
# counting in the same ledger, and the top roof is where the masts stand.
n_storeys = 0
for dk in range(1, len(layers)):
    if not _layer_has_structure(layers[dk]):
        continue
    n_storeys += 1
    d_floor_top = FLOOR_Y + dk * STOREY + FLOOR_T   # roof-top of the storey below
    d_wall_top = d_floor_top + WALL_H
    sfx = f"_d{dk+1}"
    cD = lambda x, y, _k=dk: lcell(_k, x, y)
    walkD = lambda c: c == '.' or c == 'D' or c == 'L' or c in ROLE
    solidD = lambda c: c == '#' or c == 'W' or c == 'X' or c == 'F'

    # rooms, the same flood as below decks
    room_of_d, rooms_d = {}, []
    for y in range(H):
        for x in range(W):
            c = cD(x, y)
            if not walkD(c) or c == 'D' or (x, y) in room_of_d: continue
            rid = len(rooms_d)
            blob, letters, stack = [], {}, [(x, y)]
            while stack:
                px, py = stack.pop()
                pc = cD(px, py)
                if (px, py) in room_of_d or not walkD(pc) or pc == 'D': continue
                room_of_d[(px, py)] = rid
                blob.append((px, py))
                if pc in ROLE: letters[pc] = letters.get(pc, 0) + 1
                stack += [(px+1,py),(px-1,py),(px,py+1),(px,py-1)]
            rooms_d.append({'cells': blob, 'letters': letters})
    for y in range(H):
        for x in range(W):
            if cD(x, y) != 'D': continue
            for nx, ny in ((x, y-1), (x-1, y), (x+1, y), (x, y+1)):
                if (nx, ny) in room_of_d:
                    rooms_d[room_of_d[(nx, ny)]]['cells'].append((x, y))
                    room_of_d[(x, y)] = room_of_d[(nx, ny)]
                    break
    for r in rooms_d:
        if   'E' in r['letters']: r['name'] = 'engine_room'
        elif 'P' in r['letters']: r['name'] = 'reactor_room'
        elif 'R' in r['letters']: r['name'] = 'robot_hall'
        elif 'C' in r['letters']: r['name'] = 'cargo_hold'
        elif 'B' in r['letters']: r['name'] = 'bridge'
        else: r['name'] = 'hold'
    seen_d = {}
    for r in rooms_d:
        nn = seen_d.get(r['name'], 0) + 1
        seen_d[r['name']] = nn
        r['name'] += (f"_{nn}" if nn > 1 else "") + sfx

    # walls, windows, grids, fins: greedy runs at the storey's height
    claimed_d = [[False]*W for _ in range(H)]
    def runs_d(match):
        out = []
        for y in range(H):
            for x in range(W):
                if claimed_d[y][x] or not match(cD(x, y)): continue
                w = 0
                while x+w < W and not claimed_d[y][x+w] and match(cD(x+w, y)): w += 1
                if w >= 2:
                    for i in range(w): claimed_d[y][x+i] = True
                    out.append((x, y, w, 1)); continue
                h = 1
                while y+h < H and not claimed_d[y+h][x] and match(cD(x, y+h)): h += 1
                for i in range(h): claimed_d[y+i][x] = True
                out.append((x, y, 1, h))
        return out
    walls_d = split_panels(runs_d(lambda c: c == '#'))
    thrusters_d = runs_d(lambda c: c == 'X')
    radiators_d = runs_d(lambda c: c == 'F')
    windows_d = runs_d(lambda c: c == 'W')
    for i, (x, y, w, h) in enumerate(walls_d):
        objs.append(prim(f"{stem}_wall{sfx}_{i+1}", "platform_wall",
                         wx(x, w), d_floor_top, wz(y, h),
                         w*CELL, WALL_H, h*CELL, (0.58, 0.60, 0.66, 1.0)))
    for i, (x, y, w, h) in enumerate(windows_d):
        objs.append(prim(f"{stem}_window{sfx}_{i+1}", "platform_wall",
                         wx(x, w), d_floor_top, wz(y, h),
                         w*CELL, WALL_H, h*CELL, (0.45, 0.70, 1.00, 0.22)))
    for i, (x, y, w, h) in enumerate(thrusters_d):
        objs.append(prim(f"{stem}_thruster_grid{sfx}_{i+1}", "platform_wall",
                         wx(x, w), d_floor_top, wz(y, h),
                         w*CELL, WALL_H, h*CELL, (0.42, 0.24, 0.13, 1.0)))
    for i, (x, y, w, h) in enumerate(radiators_d):
        objs.append(prim(f"{stem}_radiator{sfx}_{i+1}", "platform_wall",
                         wx(x, w), d_floor_top, wz(y, h),
                         w*CELL, WALL_H, h*CELL, (0.66, 0.71, 0.76, 1.0)))

    # sockets: same ledger, same prices, one storey up
    sclaimed_d = set()
    for y in range(H):
        for x in range(W):
            c = cD(x, y)
            if c not in ROLE or (x, y) in sclaimed_d: continue
            blob, stack = [], [(x, y)]
            while stack:
                px, py = stack.pop()
                if (px, py) in sclaimed_d or cD(px, py) != c: continue
                sclaimed_d.add((px, py)); blob.append((px, py))
                stack += [(px+1,py),(px-1,py),(px,py+1),(px,py-1)]
            xs = [p[0] for p in blob]; ys = [p[1] for p in blob]
            sx0, sy0 = min(xs), min(ys)
            sw, sh = max(xs)-sx0+1, max(ys)-sy0+1
            role, color = ROLE[c]
            counts[role] = counts.get(role, 0) + 1
            socket_cost += SOCKET_PRICE.get(role, 500.0)
            objs.append(prim(f"Socket_{role}_{counts[role]}", "socket_marker",
                             wx(sx0, sw), d_floor_top, wz(sy0, sh),
                             sw*CELL, 0.06, sh*CELL, color, collide=False))
            objs[-1]["metadata"] = {"socket": role}

    # this storey's roof -- the next storey's floor
    roof_counts_d = {}
    hole_d = SHAFT_HOLES.get(dk, set())
    for r in rooms_d:
        for (x, y, w, h) in rects_over([c for c in r['cells'] if c not in hole_d]):
            roof_counts_d[r['name']] = roof_counts_d.get(r['name'], 0) + 1
            objs.append(prim(f"{stem}_{r['name']}_roof_{roof_counts_d[r['name']]}",
                             "platform_slab", wx(x, w), d_wall_top, wz(y, h),
                             w*CELL, FLOOR_T, h*CELL, ROOF_COL))
    solid_cells_d = [(x, y) for y in range(H) for x in range(W) if solidD(cD(x, y))]
    for i, (x, y, w, h) in enumerate(rects_over(solid_cells_d)):
        objs.append(prim(f"{stem}_frame_roof{sfx}_{i+1}", "platform_slab",
                         wx(x, w), d_wall_top, wz(y, h),
                         w*CELL, FLOOR_T, h*CELL, ROOF_COL))
    if dk + 1 < len(layers):
        over_d = [(x, y) for y in range(H) for x in range(W)
                  if lcell(dk + 1, x, y) != '_' and not lhull(dk, x, y)]
        for i, (x, y, w, h) in enumerate(rects_over(over_d)):
            objs.append(prim(f"{stem}_overhang_d{dk+2}_{i+1}", "platform_slab",
                             wx(x, w), d_wall_top, wz(y, h),
                             w*CELL, FLOOR_T, h*CELL, ROOF_COL))
    print(f"storey {dk+1}: {len(rooms_d)} room(s) "
          f"{[r['name'] for r in rooms_d]}, {len(walls_d)} wall runs")

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
    if "_lamp" in name or "_vent_" in name or "_mast_" in name:
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
        # MERGE, never replace -- the mast pole already carries its comms
        # metadata, and the material joins it on the rail.
        md = o.get("metadata", {})
        md.update({"material": MAT_KEY, "density": f"{MAT_DENSITY:g}",
                   "armor": str(MAT_ARMOR)})
        o["metadata"] = md
        c = patchwork(o["name"], o["primitiveColor"])
        o["primitiveColor"] = [min(1.0, c[0]*MAT_TINT[0]), min(1.0, c[1]*MAT_TINT[1]),
                               min(1.0, c[2]*MAT_TINT[2]), c[3]]
cost_cr = round(cost_cr + socket_cost + mast_cost)

# Every piece answers to exactly one name -- the HUD reads them, the
# patchwork seeds from them, the damage model will address them. A
# duplicate is a lie waiting to be reported.
_names = [o["name"] for o in objs]
assert len(_names) == len(set(_names)), \
    f"duplicate piece names: {sorted(n for n in set(_names) if _names.count(n) > 1)}"

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
if mast_blobs:
    print(f"masts: {len(mast_blobs)} comms ({round(mast_cost)} CR) -- her voice stands topside")
if not any(c == 'B' for (c, *_ ) in sockets):
    print("note: no B cells drawn -- the forward compartment is presumably the "
          "bridge; draw B where the helm should stand, or place one from the catalog.")
