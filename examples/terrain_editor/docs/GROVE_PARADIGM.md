# Grove Scripting Paradigm

Internal design document — not player-facing.

## Core Principle

Grove is the universal automation language for everything in EDEN/Slag Legion. One scripting system, multiple interfaces to it. The AIA writes the same code the player writes. The editor creates the same behaviors the scripts create.

## Who Writes Scripts, Who Executes Them

| Writer | Executor | How |
|--------|----------|-----|
| AIA (LLM) | AIA itself | AIA writes Grove to build, move, interact — executes on its own SceneObject |
| AIA (LLM) | AlgoBot | AIA writes Grove to program a bot — `bot_target("bot_name")` + actions + `bot_run()` |
| Human player | AIA | Player writes/edits a .grove file, assigns it to an AIA |
| Human player | AlgoBot | Player writes/edits a .grove file, assigns it to a bot |
| Editor (visual) | Any SceneObject | Behaviors created in terrain editor use the same Action types that Grove functions map to |

## Two Execution Modes

### Instant (`run_script`)
Script runs via `grove_eval()` and all functions execute immediately during evaluation. Used for construction where objects should appear right away.

Functions: `spawn_cube`, `spawn_cylinder`, `spawn_model`, `set_object_rotation`, `set_object_scale`, `delete_object`, `get_player_pos`, `terrain_height`, `log`

### Queued (`program_bot` / behavior system)
Functions queue Actions on a target SceneObject's behavior. Actions execute sequentially during play mode, one per frame-tick. Used for animated movement, timed construction, bot programming.

Functions: `bot_target`, `move_to`, `turn_to`, `rotate_to`, `wait`, `set_visible`, `play_anim`, `send_signal`, `follow_path`, `bot_loop`, `bot_clear`, `bot_run`, `queue_spawn_cube`, `queue_spawn_cylinder`, `queue_set_rotation`, `queue_set_scale`, `queue_delete`

### Mixed Mode
A single script can do both. Typical pattern for animated construction:
```
local p = get_player_pos()
bot_target("Xenk")          -- target self
move_to(vec3(p.x+5, 0, p.z), 3)  -- walk to build site (queued)
queue_spawn_cube("post1", vec3(p.x+5, 0, p.z), 0.3, 0.6, 0.3, 0.1)  -- spawn when NPC arrives (queued)
spawn_cube("marker", p, 0.5, 1, 0, 0)  -- place marker immediately (instant)
bot_run()                    -- start the behavior
```

## Behavior ↔ Script Equivalence

Editor behaviors are sequences of Actions (MOVE_TO, TURN_TO, WAIT, GROVE_COMMAND, etc.). Grove functions map 1:1 to these Action types. This means:

- **Script → Behavior**: Already works. `grove_eval()` pushes Actions onto a SceneObject's behavior list.
- **Behavior → Script**: Mechanical conversion. Iterate the action list, emit the corresponding Grove function call for each. **Needs an "Export as Script" button in the editor.**

This makes the terrain editor a visual scripting tool for players who don't want to write code.

## Planned Extensions

### Script-Calls-Script (Priority: High)
Without composability, every script is a monolith. Need a mechanism like `dofile("wall.grove")` or `import` so that `build_house` can call `build_wall` four times, which calls `place_beam` repeatedly. This unlocks reusable building templates.

### World Object Scripting (Priority: High)
Doors, lifts, rotating platforms, switches — these are all SceneObjects. `bot_target("front_door")` + `rotate_to(vec3(0,90,0), 1.5)` already opens a door in principle. The missing piece is **triggers**: what causes the script to run.

Trigger types needed:
- Proximity (player enters radius)
- Signal (another script sends a signal via `send_signal`)
- Button/interaction (player presses E)
- Timer (repeat every N seconds)
- Game event (combat start, day/night cycle)

### Behavior Export Button (Priority: Medium)
Low effort, high value. Player sets up a behavior visually in the editor, clicks export, gets a .grove file they can hand to their AIA to modify or share with other players.

## Architecture Stack

```
Player (natural language)
    ↓
AIA (LLM: Grok/Claude/GPT/Gemini/Ollama)
    ↓
server.py (prompt → action selection → Grove code generation)
    ↓
grove_eval() (Rust interpreter via C FFI)
    ↓
Host functions (C++ callbacks registered in initGroveVM)
    ↓
SceneObject behaviors / direct scene manipulation
    ↓
Rendering layer (Vulkan)
```

## File Locations

| Component | Location |
|-----------|----------|
| Grove interpreter (Rust) | `modules/eden_script/grove/` |
| C FFI header | `modules/eden_script/grove/grove.h` |
| Host function registration | `examples/terrain_editor/main.cpp` → `initGroveVM()` |
| Host function implementations | `examples/terrain_editor/main.cpp` → `grove*Fn` statics |
| Action types | `include/eden/Action.hpp` |
| Behavior execution | `examples/terrain_editor/main.cpp` → `updateActiveBehavior()` |
| LLM prompt / action routing | `backend/server.py` |
| Player-facing docs | `docs/grove/` (mkdocs) |

## Current Function Count: 38

See `docs/grove/reference.md` for the full list, or grep `grove_register_fn` in main.cpp.
