# Internal TODO — Not Player-Facing

## Backburner

### Zone-Bucketed Scene Objects (Priority: High, Scope: Large)
Group scene objects by the zone cell they occupy. The zone system already has a 126x126 grid (32m cells) with ownership data — use it as the spatial organizer for everything.

**Benefits:**
- **UI**: Scene object list grouped by zone/territory instead of one flat list
- **Permissions**: Scripts can only operate on objects in zones owned by the script runner
- **Dropdowns**: PICKUP/PLACE_VERTICAL only show objects in the relevant zone
- **Serialization**: Save/load objects per zone chunk — partial loading, streaming
- **Rendering**: Per-zone LOD, culling, draw call batching
- **Pathfinding**: Zone-local navigation graphs, cheaper lookups

**Design sketch:**
- Each SceneObject gets a zone cell index (auto-computed from world position)
- Zone cells maintain a list of object pointers (updated on object move)
- UI groups objects by zone name/type/owner
- `pickup()` / `place_vertical()` filter by zone ownership at script eval time
- Level serializer writes objects grouped by zone chunk

**Depends on:** Zone system (complete), SceneObject storage refactor

### Bot Profiles (Priority: Medium)
Each AIA/AlgoBot gets a persistent profile: name, ID, memories, rank, credits, script library.
- Default blueprints ship with every AIA (build_frame_house.grove, etc.)
- Players create custom scripts stored per-bot
- Scripts are shareable/sellable for in-game credits
- Profile stored as JSON alongside level data

### run_file() in Behavior Editor (Priority: Low)
Add RUN_GROVE_FILE ActionType so behaviors can reference .grove files directly.
Behavior editor shows a file picker instead of inline script.

### Behavior Export Button (Priority: Low)
"Export as Script" button in behavior editor — iterates actions, emits corresponding Grove function calls, saves as .grove file. Makes the terrain editor a visual scripting tool.

### Script-Calls-Script (Priority: Medium)
`dofile("wall.grove")` or `import` so `build_house` can call `build_wall` four times. Unlocks reusable building templates. `run_file()` already does this — just needs documentation and patterns.

### World Object Triggers (Priority: Medium)
Doors, lifts, platforms scripted via Grove. Missing piece is trigger system:
- Proximity (player enters radius)
- Signal (another script sends via send_signal)
- Interaction (player presses E)
- Timer (repeat every N seconds)
- Game event (combat start, day/night)
