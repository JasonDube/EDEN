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
import json, sys, copy

CELL   = 2.0    # world units per plan cell
FLOOR_Y = 0.2   # floor slab base
FLOOR_T = 0.4   # floor thickness -> deck top at FLOOR_Y + FLOOR_T
WALL_H  = 3.0
ORIGIN_X, ORIGIN_Z = 170.0, 130.0   # empty ground in shipyard, clear of the user's builds

ROLE = {'E': ('engine', (0.78, 0.35, 0.16, 1.0)),
        'R': ('robot',  (0.55, 0.35, 0.75, 1.0)),
        'B': ('helm',   (0.16, 0.63, 0.59, 1.0)),
        'C': ('cargo',  (0.63, 0.43, 0.20, 1.0))}

plan_path = sys.argv[1] if len(sys.argv) > 1 else 'tools/plans/midship_01.plan'
rows = [r.rstrip('\n') for r in open(plan_path)]
W = max(len(r) for r in rows); H = len(rows)
def cell(x, y):
    if 0 <= y < H and 0 <= x < len(rows[y]): return rows[y][x]
    return '_'

walk  = lambda c: c == '.' or c == 'D' or c in ROLE
solid = lambda c: c == '#'
hull  = lambda c: walk(c) or solid(c)

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

# ---- walls: greedy runs over '#' -------------------------------------------
wclaimed = [[False]*W for _ in range(H)]
walls = []
for y in range(H):
    for x in range(W):
        if wclaimed[y][x] or not solid(cell(x, y)): continue
        w = 0
        while x+w < W and not wclaimed[y][x+w] and solid(cell(x+w, y)): w += 1
        if w >= 2:
            for i in range(w): wclaimed[y][x+i] = True
            walls.append((x, y, w, 1)); continue
        h = 1
        while y+h < H and not wclaimed[y+h][x] and solid(cell(x, y+h)): h += 1
        for i in range(h): wclaimed[y+i][x] = True
        walls.append((x, y, 1, h))

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

def prim(name, bt, px, py, pz, sx, sy, sz, color, collide=True):
    return {"name": name, "buildingType": bt, "modelPath": "",
            "position": [px, py, pz], "rotation": [0.0, 0.0, 0.0],
            "scale": [sx, sy, sz], "primitiveType": 1, "primitiveSize": 1.0,
            "primitiveColor": list(color), "primitiveHeight": 1.0,
            "primitiveRadius": 0.5, "primitiveSegments": 16,
            "aabbCollision": collide, "polygonCollision": False,
            "bulletCollisionType": 0, "beingType": 0, "visible": True,
            "isSkinned": False, "kinematicPlatform": False, "behaviors": [],
            "brightness": 1.0, "hueShift": 0.0, "saturation": 1.0,
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
                     w*CELL, WALL_H, h*CELL, (0.58, 0.60, 0.66, 1.0)))
counts = {}
for (c, x, y, w, h) in sockets:
    role, color = ROLE[c]
    counts[role] = counts.get(role, 0) + 1
    objs.append(prim(f"Socket_{role}_{counts[role]}", "socket_marker",
                     wx(x, w), deck_top, wz(y, h),
                     w*CELL, 0.06, h*CELL, color, collide=False))

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
