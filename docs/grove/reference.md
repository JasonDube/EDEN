# Function Reference

Complete list of all Grove host functions available in EDEN.

## Utility

| Function | Returns | Description |
|----------|---------|-------------|
| `log(...)` | — | Print values to the Grove console. Accepts any number/type of arguments. |
| `terrain_height(vec3)` | number | Returns terrain surface height at the X,Z position. |
| `get_player_pos()` | vec3 | Returns the player's current world position. |

## Instant Construction

These execute immediately when the script runs.

| Function | Returns | Description |
|----------|---------|-------------|
| `spawn_cube(name, pos, size, r, g, b)` | bool | Spawn a colored cube. Bottom placed on terrain. |
| `spawn_cylinder(name, pos, radius, height, r, g, b)` | bool | Spawn a colored cylinder. Bottom placed on terrain. |
| `spawn_model(name, path, pos)` | bool | Load a `.glb` or `.lime` model at position. Bottom placed on terrain. |
| `spawn(name, pos)` | object | Spawn a posthole object at position (legacy). |
| `set_object_rotation(name, rx, ry, rz)` | bool | Set euler rotation in degrees on a named object. |
| `set_object_scale(name, sx, sy, sz)` | bool | Set scale on a named object. |
| `delete_object(name)` | bool | Remove the first object matching the name from the scene. |

### Parameter Details

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | string | Unique identifier for the object |
| `pos` | vec3 | World position — Y is auto-sampled from terrain, so `vec3(x, 0, z)` works |
| `size` | number | Cube edge length in meters |
| `radius` | number | Cylinder radius in meters |
| `height` | number | Cylinder height in meters |
| `r, g, b` | number | Color components, 0.0 to 1.0 |
| `path` | string | Model file path (relative to level directory or absolute) |
| `rx, ry, rz` | number | Rotation in degrees |
| `sx, sy, sz` | number | Scale factors |

## Queued Construction

These schedule actions in a behavior sequence. They only execute when the NPC reaches that point in the sequence. Requires `bot_target()` to be set first.

| Function | Returns | Description |
|----------|---------|-------------|
| `queue_spawn_cube(name, pos, size, r, g, b)` | bool | Queue a cube spawn in the behavior sequence. |
| `queue_spawn_cylinder(name, pos, radius, height, r, g, b)` | bool | Queue a cylinder spawn in the behavior sequence. |
| `queue_set_rotation(name, rx, ry, rz)` | bool | Queue a rotation change in the behavior sequence. |
| `queue_set_scale(name, sx, sy, sz)` | bool | Queue a scale change in the behavior sequence. |
| `queue_delete(name)` | bool | Queue an object deletion in the behavior sequence. |

## AlgoBot Behavior

These queue actions on a target SceneObject's behavior. Call `bot_target()` first, queue actions, then `bot_run()` to start.

| Function | Returns | Description |
|----------|---------|-------------|
| `bot_target(name)` | bool | Select a scene object by name to program. |
| `move_to(vec3, duration?, animation?)` | — | Queue movement to a position over N seconds. Default: 2s. |
| `rotate_to(vec3, duration?)` | — | Queue rotation to euler angles. Default: 1s. |
| `turn_to(vec3, duration?)` | — | Queue turning to face a world position (yaw only). Default: 0.5s. |
| `wait(seconds)` | — | Queue a pause. |
| `set_visible(bool)` | — | Queue visibility toggle. |
| `play_anim(name, duration?)` | — | Queue an animation. Duration 0 = play indefinitely. |
| `send_signal(name, target?)` | — | Queue a signal broadcast. Optional target name. |
| `follow_path(path_name)` | — | Queue following a named AI path. |
| `bot_loop(bool)` | — | Set whether the behavior repeats. Default: false. |
| `bot_clear()` | — | Clear all queued actions on the target. |
| `bot_run()` | — | Start the behavior. Must be called last. |

## Economy

| Function | Returns | Description |
|----------|---------|-------------|
| `get_credits()` | number | Returns the player's current credit balance. |
| `add_credits(amount)` | number | Add credits. Returns new balance. |
| `deduct_credits(amount)` | bool | Deduct credits. Returns true if sufficient funds. |
| `buy_plot(vec3)` | bool | Purchase the land plot at position. Auto-checks price, funds, and ownership. |
| `sell_plot(vec3)` | bool | Sell an owned plot for 50% refund. |
| `plot_price(vec3)` | number | Returns the price of the plot at position. |
| `plot_status(vec3)` | string | Returns `"available"`, `"owned"`, `"spawn_zone"`, `"battlefield"`, or `"too_expensive"`. |

## Zones

| Function | Returns | Description |
|----------|---------|-------------|
| `zone_type(vec3)` | string | Returns zone type: `"wilderness"`, `"battlefield"`, `"spawn_safe"`, `"residential"`, `"commercial"`, `"industrial"`, `"resource"`. |
| `zone_resource(vec3)` | string | Returns resource type: `"wood"`, `"limestone"`, `"iron"`, `"oil"`, `"none"`. |
| `zone_owner(vec3)` | number | Returns owner ID of the plot (0 = unclaimed). |
| `can_build(vec3)` | bool | Returns whether building is allowed at position. |
