# The `.eden` Level Format — Specification (Draft 1)

**Status:** DRAFT for review — 2026-07-21
**Purpose:** `.eden` is the contract between the **World Builder** (C++ authoring
tool) and any **game runtime** (HEIDIC-compiled games, or C++ games linking the
EDEN engine). The World Builder writes it; the engine's loader reads it; neither
side ever needs to know the other's language.

This spec formalizes and extends the format `LevelSerializer` writes today
(v1 = current behavior, documented; v2 = the additions). Nothing in v1 breaks.

---

## 1. Design Rules

1. **One loader.** The parser lives in the shared EDEN engine
   (`src/Editor/LevelSerializer.*` → to be moved to `src/Level/`). World
   Builder and games link the same code. No second implementation, ever.
2. **JSON manifest + binary sidecars.** Human-diffable structure; bulk data
   (terrain, splatmaps) stays binary. This is what we do today — keep it.
3. **Additive evolution.** Readers MUST ignore unknown keys and preserve them
   on re-save. New features are new keys, not changed keys. `version` bumps
   only when a reader would otherwise misinterpret old data.
4. **Scripts are referenced, never embedded.** `.eden` points at `.grove`
   files and names HEIDIC components. Behavior lives in script/code files
   where it can be edited, diffed, and hot-reloaded independently of the level.
5. **Everything is an entity.** Every placed object becomes an ECS entity at
   load. Tags/components/triggers attach to entities uniformly — a door, an
   NPC, and a spawn marker differ only in their components.

## 2. File Layout (bundle)

A level is a directory bundle (recommended) or a bare pair of files:

```
bridge.eden/                  ← directory bundle
├── level.json                ← the manifest (this spec, sections below)
├── terrain.bin               ← binary terrain sidecar (chunked, v4 format)
├── scripts/                  ← level-local Grove scripts
│   └── door_open.grove
└── overrides/                ← optional level-local asset overrides
```

Bare form: `bridge.eden` (the JSON) + `bridge.terrain` beside it — this is
exactly what the terrain editor saves today and remains valid forever.

**Asset resolution order:** `overrides/` in the bundle → project `assets/`
directory → engine defaults. All paths in the manifest are relative; absolute
paths are a validation error.

## 3. Manifest Sections (`level.json`)

```jsonc
{
  "format": "eden-level",
  "version": 2,
  "meta":        { ... },   // §3.1  identity & provenance
  "terrain":     { ... },   // §3.2  terrain reference + config
  "environment": { ... },   // §3.3  sky, water, lighting, time
  "objects":     [ ... ],   // §3.4  placed things (THE core section)
  "markers":     [ ... ],   // §3.5  spawns, cameras, waypoints
  "paths":       [ ... ],   // §3.6  named point sequences
  "zones":       [ ... ],   // §3.7  named volumes
  "aiNodes":     [ ... ],   // §3.8  AI navigation/behavior nodes (as today)
  "doors":       [ ... ]    // §3.9  level-to-level portals (as today)
}
```

### 3.1 `meta`
```jsonc
"meta": {
  "name": "The Broken Bridge",
  "author": "jason",
  "created": "2026-07-21T14:00:00Z",
  "modified": "2026-07-21T16:30:00Z",
  "builder": "eden-world-builder 0.1",   // tool provenance
  "description": "Slag Legion act 1 crossing"
}
```

### 3.2 `terrain`
Formalizes today's `terrainConfig` + binary sidecar. The binary format
(chunk table, heightmap, 32-texture splatmap, texHSB, grass density — header
versions 1–4) is unchanged and documented in `LevelSerializer.cpp`.

```jsonc
"terrain": {
  "source": "terrain.bin",          // sidecar path, relative to manifest
  "minChunk": [-16, -16],           // world bounds in chunk coords
  "maxChunk": [15, 15],
  "chunkResolution": 64,            // vertices per chunk edge
  "tileSize": 1.0,                  // meters per tile
  "wrapWorld": true,
  "useFixedBounds": true,
  "textures": ["grass.png", "rock.png", ...],   // splat palette, index-ordered
  "physics": { "backend": "jolt" }
}
```
A level MAY omit `terrain` entirely (interior/space levels).

### 3.3 `environment`
```jsonc
"environment": {
  "sky":   { "preset": "day", "sunAngle": 35.0 },
  "water": { "enabled": true, "level": 2.5 },
  "time":  { "start": "0800", "scale": 60.0 }    // for ON_GAME_TIME triggers
}
```

### 3.4 `objects` — the core section

Each entry is one placed thing. Loader contract: **every entry spawns one ECS
entity** with `Transform` + listed components, then registers its triggers.

```jsonc
{
  "id": "front_door_01",            // unique within level; stable across saves
  "name": "Front Door",             // display name (editor/UI)
  "model": "models/door_oak.lime",  // .lime | .glb | primitive (see below)
  "position": [12.0, 0.0, -4.5],    // meters, Y-up right-handed (CONVENTIONS)
  "rotation": [0.0, 90.0, 0.0],     // euler degrees XYZ
  "scale":    [1.0, 1.0, 1.0],

  // -- optional visual/physics (all default sensibly) --
  "visible": true,
  "static": false,                  // static ⇒ no per-frame transform updates
  "collision": "mesh",              // "none" | "aabb" | "mesh" | "capsule"
  "primitive": { "type": "cube", "size": [1,2,1], "color": [0.6,0.4,0.2] },
                                    // instead of "model", as today

  // -- v2: THE TAG BLOCK — binds HEIDIC components --
  "components": {
    "Door":   { "openAngle": 110.0, "locked": false },
    "Health": { "max": 50 }
  },

  // -- v2: THE TRIGGER BLOCK — binds Grove scripts --
  "triggers": [
    { "on": "proximity", "radius": 3.0, "script": "scripts/door_open.grove" },
    { "on": "interact",  "prompt": "Open", "script": "scripts/door_open.grove" },
    { "on": "signal",    "signal": "unlock_all", "script": "scripts/unlock.grove" }
  ],

  // -- editor-authored behaviors (Action sequences), as today --
  "behaviors": [ ... ]              // existing ActionType serialization, unchanged
}
```

**`components` semantics (HEIDIC binding):**
- Keys are component type names as declared in `.hd` (or C++ registered
  components). Values are field initializers by field name.
- Loader looks the component up in the engine's component registry, default-
  constructs it, applies listed fields, attaches to the entity. Unknown
  component name ⇒ load-time **warning** listing known components (never a
  crash — levels must outlive code churn). Unknown field ⇒ same.
- A bare tag with no data is an empty object: `"Enemy": {}`.
- HEIDIC systems then bind by query — `system patrol(query Patrol, Position)`
  picks up every entity the level tagged `Patrol`. No lookup code.

**`triggers` semantics (Grove binding):**
- `on` values map 1:1 to the existing `TriggerType` enum
  (`include/eden/Action.hpp`): `gamestart`, `game_time`, `interact`,
  `proximity`, `signal`, `collision`, `command`.
- Per-trigger params: `radius` (proximity), `time` (game_time, "HHMM"),
  `signal` (signal), `prompt` (interact UI hint), `once` (bool, default
  false), `cooldown` (seconds, default 0).
- `script` is a Grove file resolved per §2. The engine runs it via
  `grove_eval()` with the **triggering entity bound as the script's target**
  (i.e. `bot_target(self)` is implicit; `self`, `player`, and `trigger`
  tables are pre-bound in the environment).
- Alternative inline form for one-liners: `"run": "rotate_to(vec3(0,110,0), 1.2)"`
  — allowed, but files are preferred (rule 4).

### 3.5 `markers`
Typed, invisible, position-only entities. Replaces today's single
`spawnPosition` with a general mechanism (loader still honors the old key).

```jsonc
"markers": [
  { "id": "spawn_default", "type": "player_spawn", "position": [0,0,0], "rotation": [0,180,0] },
  { "id": "cam_intro",     "type": "camera", "position": [10,8,-20], "lookAt": [0,1,0], "fov": 55 },
  { "id": "boss_entry",    "type": "generic", "position": [40,0,12],
    "components": { "EncounterStart": {} } }     // markers take tags too
]
```

### 3.6 `paths`
```jsonc
"paths": [
  { "name": "patrol_route_a", "loop": true,
    "points": [[0,0,0],[10,0,0],[10,0,10]] }
]
```
Referenced by name from `FOLLOW_PATH` actions, Grove `follow_path()`, or
HEIDIC components.

### 3.7 `zones`
Named volumes (today's `zones`, formalized). AABB or sphere. Zones may carry
`components` and `triggers` exactly like objects — a kill-zone is just a zone
with a trigger.

### 3.8 `aiNodes` / 3.9 `doors`
Carried over from the current format unchanged (AI navigation nodes;
`targetLevel`/`targetDoorId` level portals).

## 4. Loader Contract (engine side)

`eden::LoadedLevel eden::loadLevel(const std::string& path)` guarantees:

1. Terrain uploaded/chunked per `terrain` (or skipped if absent).
2. Environment applied.
3. One ECS entity per object/marker/zone entry: `Transform` + components from
   the tag block + `Name`/`Id` components for lookups.
4. Triggers registered with the trigger system; Grove scripts pre-parsed
   (parse errors reported at load, with file:line, not at first fire).
5. Behaviors deserialized onto entities (existing Action pipeline).
6. Unknown keys preserved in an opaque blob for round-tripping.
7. **Never aborts on content errors** — collects a `std::vector<LoadIssue>`
   (missing model → placeholder cube; unknown component → warning; bad
   script → trigger disabled + warning). The Builder shows these in a panel;
   games log them.

Exposed to HEIDIC as `eden_load_level(path: string): i32` plus
`eden_level_issues()` etc. in the `heidic_*`/stdlib surface.

## 5. Versioning & Migration

- `version: 1` = files LevelSerializer writes today (implicitly; they lack
  the `format` key). Loader treats missing `format` as v1 and maps old keys
  (`spawnPosition` → a `player_spawn` marker, `terrainConfig` → `terrain`,
  top-level `sky`/`waterLevel`/`waterEnabled` → `environment`).
- `version: 2` = this spec.
- Writers always write the newest version. The Builder's "Save" silently
  upgrades v1 → v2 (old keys dropped in favor of their new homes).
- Terrain binary versioning stays independent (its own header, currently v4).

## 6. What `.eden` Is NOT

- **Not a model format.** Geometry lives in `.lime`/`.glb`; `.eden` references.
- **Not a script container.** Grove files and HEIDIC code live beside, not
  inside (exception: one-line `run` triggers).
- **Not a save-game.** `.eden` is authored initial state. Runtime save-games
  are a different format (future spec) that records deltas against a level.
- **Not engine-internal.** No GPU handles, no pointers, no absolute paths —
  a bundle must load on any machine with the project's assets.

## 7. Open Questions (for review)

1. Directory bundle vs. single zipped `.eden` — bundles are diff/git-friendly
   (proposed default); zip ship-packaging can come later.
2. Should `components` values support expressions (`"speed": "2.0 + rand(1)"`)
   or stay literal? **Proposal: literal only.** Randomization belongs in
   HEIDIC/Grove, not the data format.
3. Per-instance material overrides on objects (tint, texture swap) — needed
   now or v3? (Note: if deferred, they land naturally as prefab-style value
   overrides, §8.4.)
4. Prefabs — deferred to v3; design sketch agreed and captured in §8.

## 8. Prefabs — Deferred Design Sketch (v3)

**Status:** agreed direction, NOT in v2. Recorded here so v2 decisions stay
compatible with it. (Discussed & settled 2026-07-21.)

### 8.1 Concept

A prefab is a level fragment stamped into other levels: **a sublevel is a
place; a prefab is a rubber stamp.** It is just a `.eden` bundle referenced
by another `.eden` — same format, same loader, no new file type:

```jsonc
{ "id": "tower_north", "prefab": "prefabs/guard_tower.eden",
  "position": [40, 0, 12], "rotation": [0, 90, 0] },
{ "id": "tower_south", "prefab": "prefabs/guard_tower.eden",
  "position": [-40, 0, 30] }
```

Semantics are **live-referenced instancing**: fix the ladder trigger in
`guard_tower.eden` once and every placement in every level is fixed on next
load. `#include` for world content.

The endpoint is Godot's insight — *everything is a scene*: a level is simply
the outermost prefab. House contains furniture prefabs, village contains
house prefabs, world contains villages. One format, fully recursive.

Lineage note: the `.limes` blueprint assemblies (pre-positioned machine
parts placed as one unit) are the proto-prefab for *models*. An `.eden`
prefab is the same idea one level up — meshes **plus components, triggers,
behaviors, markers** — so a generator assembly doesn't just look assembled,
it *arrives working*.

### 8.2 Rollout stages

- **v3a — instance + transform only.** Identical stamps. No override
  semantics at all. Covers towers, lampposts, machine assemblies — most of
  the value for a fraction of the complexity.
- **v3b — flat value overrides only:**
  ```jsonc
  { "id": "tower_east", "prefab": "prefabs/guard_tower.eden",
    "position": [60, 0, -5],
    "overrides": { "guard.Patrol.speed": 0, "torch.Light.enabled": false } }
  ```
  Paths are `<child-id>.<Component>.<field>`, values are literals.

### 8.3 The hard rule: values yes, structure no

Per-instance overrides may change **component field values** on a prefab's
children. They may NEVER add, remove, reparent, or re-model objects inside
an instance. Structural changes mean you edit the prefab or fork a copy.
This single rule is what keeps us out of the deep-override swamp (nested
prefab overrides were Unity's most-demanded, most-delayed feature for a
decade — the complexity lives entirely on the structural side of this line).

### 8.4 Consequences already baked into v2

- Every object has a **stable `id`** (override paths + instance addressing
  need them).
- Loaders **round-trip unknown keys** (a v2 loader carries `prefab` /
  `overrides` entries through a re-save instead of destroying them).
- ID namespacing when instantiated: children get `<instance-id>/<child-id>`
  (e.g. `tower_north/guard`) so signals, Grove `bot_target()`, and lookups
  stay unambiguous with many instances.

### 8.5 Loader rules (when built)

- Cycle detection: a prefab may not (transitively) include itself — load
  issue, instance skipped.
- Depth cap (sanity default: 8) with a clear LoadIssue, not a crash.
- A prefab bundle normally omits `terrain`/`environment`; if present they
  are ignored on instancing (only the outermost level's count) with a
  warning.
