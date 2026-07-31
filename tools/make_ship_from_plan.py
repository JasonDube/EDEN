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

# ---- floors: greedy rectangles over the hull footprint ---------------------
claimed = [[False]*W for _ in range(H)]
floors = []
for y in range(H):
    for x in range(W):
        if claimed[y][x] or not hull(cell(x, y)): continue
        w = 0
        while x+w < W and not claimed[y][x+w] and hull(cell(x+w, y)): w += 1
        h = 1
        while y+h < H and all(not claimed[y+h][x+i] and hull(cell(x+i, y+h)) for i in range(w)): h += 1
        for yy in range(y, y+h):
            for xx in range(x, x+w): claimed[yy][xx] = True
        floors.append((x, y, w, h))

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

objs = []
for i, (x, y, w, h) in enumerate(floors):
    objs.append(prim(f"Plan_Floor_{i}", "platform_slab",
                     wx(x, w), FLOOR_Y, wz(y, h),
                     w*CELL, FLOOR_T, h*CELL, (0.42, 0.44, 0.50, 1.0)))
deck_top = FLOOR_Y + FLOOR_T
for i, (x, y, w, h) in enumerate(walls):
    objs.append(prim(f"Plan_Wall_{i}", "platform_wall",
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
stern_z = ORIGIN_Z + (max(y+h for (x,y,w,h) in floors) - H/2.0) * CELL
out['settings']['spawnPosition'] = [ORIGIN_X, 2.0, stern_z + 8.0]
out['name'] = 'plan_ship'
dst = 'build/examples/terrain_editor/levels/plan_ship.eden'
json.dump(out, open(dst, 'w'), indent=1)

print(f"{dst}: {len(floors)} floor slabs, {len(walls)} wall runs, "
      f"{sum(1 for y in range(H) for x in range(W) if cell(x,y)=='D')} door cells, "
      f"{len(sockets)} sockets {[(ROLE[c][0], w, h) for (c,x,y,w,h) in sockets]}")
if not any(c == 'B' for (c, *_ ) in sockets):
    print("note: no B cells drawn -- the forward compartment is presumably the "
          "bridge; draw B where the helm should stand, or place one from the catalog.")
