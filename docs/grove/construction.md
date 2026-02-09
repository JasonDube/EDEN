# Construction

Grove provides two ways to build structures: **instant** and **animated**.

## Instant Construction

Objects appear immediately. Use these in `run_script` actions or the Grove console.

### Spawning Primitives

```lua
-- Spawn a brown wooden post (cylinder)
spawn_cylinder("post_01", vec3(10, 0, 20), 0.15, 3.0, 0.6, 0.4, 0.2)

-- Spawn a gray concrete block (cube)
spawn_cube("base_01", vec3(10, 0, 20), 0.5, 0.65, 0.63, 0.58)

-- Spawn a 3D model
spawn_model("tree_01", "models/oak_tree.glb", vec3(15, 0, 25))
```

All spawn functions automatically place the object's bottom on the terrain surface. The Y coordinate in the position is ignored — terrain height is sampled at X,Z.

### Modifying Objects

```lua
-- Rotate a wall 90 degrees around Y axis
set_object_rotation("wall_01", 0, 90, 0)

-- Scale a beam to be long and thin
set_object_scale("beam_01", 4.0, 1.0, 1.0)

-- Remove an object
delete_object("post_01")
```

### Example: Build a Simple Shelter

```lua
local p = get_player_pos()
local x = p.x
local z = p.z
local w = 4  -- 4 meter square

-- Four corner posts
spawn_cylinder("post_sw", vec3(x - w/2, 0, z - w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
spawn_cylinder("post_se", vec3(x + w/2, 0, z - w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
spawn_cylinder("post_nw", vec3(x - w/2, 0, z + w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
spawn_cylinder("post_ne", vec3(x + w/2, 0, z + w/2), 0.15, 3.0, 0.6, 0.4, 0.2)

-- Roof slab
spawn_cube("roof", vec3(x, 0, z), 0.3, 0.5, 0.2, 0.2)
set_object_scale("roof", w + 1, 0.5, w + 1)

log("Shelter built.")
```

## Animated Construction

The NPC walks to each location and builds step by step. Use `bot_target()` with the NPC's own name, queue movement and spawn actions, then call `bot_run()`.

### Queue Functions

These schedule actions to execute during the behavior sequence rather than immediately:

| Function | Purpose |
|----------|---------|
| `queue_spawn_cube(name, pos, size, r, g, b)` | Spawn cube when reached in sequence |
| `queue_spawn_cylinder(name, pos, radius, height, r, g, b)` | Spawn cylinder when reached |
| `queue_set_rotation(name, rx, ry, rz)` | Set rotation when reached |
| `queue_set_scale(name, sx, sy, sz)` | Set scale when reached |
| `queue_delete(name)` | Delete object when reached |

### Movement Functions

| Function | Purpose |
|----------|---------|
| `move_to(vec3, duration)` | Walk to position over N seconds |
| `turn_to(vec3, duration)` | Turn to face a position |
| `wait(seconds)` | Pause before next action |

### Example: Animated Frame Build

Xenk walks to each corner, spawns a post, then adds beams and a roof:

```lua
local p = get_player_pos()
local cx = p.x + 6   -- build 6m ahead of player
local cz = p.z
local w = 4

bot_target("Xenk")    -- target yourself
bot_clear()           -- clear any previous behavior

-- Walk to SW corner, spawn post
move_to(vec3(cx - w/2, 0, cz - w/2), 3.0)
turn_to(vec3(cx, 0, cz), 0.5)
queue_spawn_cylinder("post_sw", vec3(cx - w/2, 0, cz - w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
wait(0.5)

-- Walk to SE corner, spawn post
move_to(vec3(cx + w/2, 0, cz - w/2), 2.0)
turn_to(vec3(cx, 0, cz), 0.5)
queue_spawn_cylinder("post_se", vec3(cx + w/2, 0, cz - w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
wait(0.5)

-- Walk to NE corner, spawn post
move_to(vec3(cx + w/2, 0, cz + w/2), 2.0)
turn_to(vec3(cx, 0, cz), 0.5)
queue_spawn_cylinder("post_ne", vec3(cx + w/2, 0, cz + w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
wait(0.5)

-- Walk to NW corner, spawn post
move_to(vec3(cx - w/2, 0, cz + w/2), 2.0)
turn_to(vec3(cx, 0, cz), 0.5)
queue_spawn_cylinder("post_nw", vec3(cx - w/2, 0, cz + w/2), 0.15, 3.0, 0.6, 0.4, 0.2)
wait(0.5)

-- Walk to center, add roof
move_to(vec3(cx, 0, cz), 1.5)
queue_spawn_cube("roof", vec3(cx, 0, cz), 0.3, 0.5, 0.2, 0.2)
queue_set_scale("roof", w + 1, 0.5, w + 1)
wait(0.5)

bot_loop(false)       -- run once, don't repeat
bot_run()             -- start the sequence
log("Build sequence started.")
```

!!! note
    `bot_run()` must always be the last call. The NPC begins walking immediately if the game is in play mode.

## Terrain Height

On sloped terrain, each post may sit at a different height. Use `terrain_height()` to query the ground level:

```lua
local h1 = terrain_height(vec3(10, 0, 20))
local h2 = terrain_height(vec3(14, 0, 20))
log("Height difference: " .. (h2 - h1) .. " meters")
```

Spawn functions handle this automatically — you only need `terrain_height()` when computing offsets between objects (e.g., angling a beam between posts at different heights).
