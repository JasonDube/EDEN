# Model Editor Refactor Plan
## Safe, Incremental Approach - No Functionality Loss

**Created:** January 28, 2025
**Codebase Size:** ~18,000 lines
**Goal:** Better organization without breaking anything

---

## GOLDEN RULES

1. **Make a backup before starting** (zip the entire EDEN folder)
2. **One change at a time** - compile and test after each
3. **Never delete code** until replacement is proven working
4. **If something breaks, revert immediately** - don't debug for hours
5. **Each phase is optional** - stop anytime, codebase still works

---

## PHASE 1: Split ModelingMode.cpp (ZERO RISK)

**Current:** ModelingMode.cpp = 8,589 lines in one file
**Goal:** Same class, multiple .cpp files for organization

C++ allows a single class to have its implementation spread across multiple .cpp files. We just move functions - no logic changes.

### Step 1.1: Create new files (empty initially)

```
examples/model_editor/
├── ModelingMode.cpp          (keep - will shrink)
├── ModelingMode_UV.cpp       (new - UV editor functions)
├── ModelingMode_Gizmo.cpp    (new - gizmo/transform functions)
├── ModelingMode_Selection.cpp (new - selection handling)
├── ModelingMode_Paint.cpp    (new - texture painting)
├── ModelingMode_Snap.cpp     (new - snap mode functions)
└── ModelingMode_Primitives.cpp (new - mesh creation)
```

### Step 1.2: Update CMakeLists.txt

Add the new .cpp files to the build.

### Step 1.3: Move functions one file at a time

**To ModelingMode_UV.cpp (~1,500 lines):**
- `renderModelingUVWindow()`
- `findUVVertexAtPoint()`
- `findUVEdgeAtPoint()`
- `findUVFaceAtPoint()`
- `getEdgeUVs()`
- `getEdge3DPositions()`
- `screenToUV()` helper
- `storeOriginalUVs()`
- `storeOriginalUVsForVertices()`
- `getUVSelectionBounds()`
- `getUVIslandFaces()`
- `getIslandVertices()`
- `clearUVEdgeSelection()`
- `findTwinUVEdges()`
- `sewSelectedEdge()`
- `moveAndSewSelectedEdge()`
- `unsewSelectedEdge()`
- All UV-related helpers

**To ModelingMode_Gizmo.cpp (~800 lines):**
- `renderGizmo()`
- `processGizmoInput()`
- `getGizmoCenter()`
- `isPointOnGizmoAxis()`
- `projectPointOntoAxis()`
- All gizmo-related helpers

**To ModelingMode_Selection.cpp (~1,000 lines):**
- Selection handling from `processModelingInput()`
- `handleVertexSelection()`
- `handleEdgeSelection()`
- `handleFaceSelection()`
- Rectangle select logic
- Paint select logic

**To ModelingMode_Paint.cpp (~1,200 lines):**
- `renderImageRefWindow()`
- Painting logic from update/input
- Stamp handling
- Clone brush handling
- Smear handling
- Eyedropper

**To ModelingMode_Snap.cpp (~600 lines):**
- `startSnapMode()`
- `cancelSnapMode()`
- `processSnapModeInput()`
- `drawSnapVertexOverlay()`
- Snap-related helpers

**To ModelingMode_Primitives.cpp (~400 lines):**
- Primitive creation UI code
- Box/cylinder/sphere creation wrappers

### Step 1.4: Verification

After each file move:
1. Compile
2. Run the editor
3. Test that specific feature still works
4. Commit to git (if using)

**Time estimate:** 2-3 hours
**Risk:** Zero (just moving code, no logic changes)

---

## PHASE 2: Extract Constants (VERY LOW RISK)

**Current:** Magic numbers scattered throughout
**Goal:** Named constants in one place

### Step 2.1: Create EditorConstants.hpp

```cpp
#pragma once

namespace eden::editor {

// Selection thresholds
constexpr float UV_VERTEX_CLICK_THRESHOLD = 0.015f;
constexpr float UV_EDGE_CLICK_THRESHOLD = 0.02f;
constexpr float VERTEX_RAYCAST_THRESHOLD = 0.1f;
constexpr float EDGE_RAYCAST_THRESHOLD = 0.05f;

// Undo system
constexpr size_t MAX_UNDO_LEVELS = 50;

// Gizmo
constexpr float GIZMO_DEFAULT_SIZE = 1.0f;
constexpr float GIZMO_AXIS_LENGTH = 1.0f;
constexpr float GIZMO_PICK_THRESHOLD = 0.15f;

// Grid
constexpr float DEFAULT_GRID_SIZE = 20.0f;
constexpr float DEFAULT_GRID_SPACING = 1.0f;

// Camera
constexpr float MIN_ORBIT_DISTANCE = 0.01f;
constexpr float DEFAULT_ORBIT_DISTANCE = 5.0f;

// UI
constexpr float DOUBLE_CLICK_TIME = 0.3f;
constexpr float DRAG_THRESHOLD_PIXELS = 5.0f;

// UV Editor
constexpr float UV_MIN_ZOOM = 0.25f;
constexpr float UV_MAX_ZOOM = 8.0f;

// Mesh operations
constexpr float POSITION_EQUAL_TOLERANCE = 0.0001f;
constexpr float NORMAL_MERGE_THRESHOLD = 0.85f;

} // namespace eden::editor
```

### Step 2.2: Replace magic numbers one at a time

Find each magic number, replace with constant, compile, test.

**Example change:**
```cpp
// Before
float threshold = 0.02f / m_ctx.uvZoom;

// After
float threshold = editor::UV_EDGE_CLICK_THRESHOLD / m_ctx.uvZoom;
```

**Time estimate:** 1-2 hours
**Risk:** Very low (each change is one line)

---

## PHASE 3: Add State Enums (LOW RISK)

**Current:** Implicit states via multiple bool flags
**Goal:** Explicit state enums (no logic change yet, just naming)

### Step 3.1: Create EditorStates.hpp

```cpp
#pragma once

namespace eden::editor {

// Snap mode progression
enum class SnapState {
    Inactive,           // Not in snap mode
    SelectingSource,    // Picking source vertices
    SourceSelected,     // Have source, picking target object
    SelectingFace,      // Picking target face
    FaceSelected,       // Have face, picking target vertices
    ReadyToSnap         // All selections made
};

// UV editor sub-modes
enum class UVEditMode {
    Island = 0,
    Face = 1,
    Edge = 2,
    Vertex = 3
};

// Selection interaction
enum class SelectionState {
    Idle,
    Clicking,           // Mouse down, might be click or drag
    Dragging,           // Confirmed drag (moved > threshold)
    RectSelecting       // Drawing selection rectangle
};

// Gizmo interaction
enum class GizmoState {
    Idle,
    Hovering,           // Axis highlighted
    Dragging            // Actively transforming
};

// Paint stroke
enum class PaintState {
    Idle,
    Stroking,           // Mouse down, painting
    Sampling            // Alt+click eyedropper
};

} // namespace eden::editor
```

### Step 3.2: Add enums alongside existing flags (don't replace yet)

```cpp
// In EditorContext.hpp, ADD (don't remove old):
SnapState snapState = SnapState::Inactive;

// In code, ADD state tracking alongside existing logic:
if (startingSnapMode) {
    m_ctx.snapState = SnapState::SelectingSource;
    // ... existing code unchanged ...
}
```

### Step 3.3: Later, use enums to simplify conditionals

Only after enums are proven to track correctly.

**Time estimate:** 1 hour
**Risk:** Low (adding, not changing)

---

## PHASE 4: Group EditorContext (MEDIUM RISK)

**Current:** 100+ flat references in EditorContext
**Goal:** Logical groupings as sub-structs

### Step 4.1: Create grouping structs

```cpp
// In EditorContext.hpp

struct CameraState {
    Camera& primary;
    Camera& secondary;
    float& speed;
    bool& isLooking;
    bool& isTumbling;
    bool& isPanning;
    glm::vec3& orbitTarget;
    float& orbitYaw;
    float& orbitPitch;
};

struct UVEditorState {
    int& selectionMode;           // 0=Island, 1=Face, 2=Edge, 3=Vertex
    float& zoom;
    glm::vec2& pan;
    bool& panning;
    glm::vec2& panStart;
    bool& showWireframe;
    glm::vec3& wireframeColor;
    std::set<uint32_t>& selectedFaces;
    std::set<uint32_t>& selectedVertices;
    // ... etc
};

struct PaintState {
    bool& active;
    glm::vec3& color;
    float& radius;
    float& strength;
    bool& squareBrush;
    bool& useStamp;
    bool& useSmear;
    bool& useEyedropper;
    bool& useClone;
    // ... etc
};

struct GizmoState {
    GizmoMode& mode;
    GizmoAxis& hoveredAxis;
    GizmoAxis& activeAxis;
    bool& dragging;
    glm::vec3& dragStart;
    float& size;
    bool& localSpace;
    // ... etc
};
```

### Step 4.2: Add grouped accessors to EditorContext

```cpp
struct EditorContext {
    // NEW: Grouped access
    CameraState camera() { return { camera, camera2, cameraSpeed, ... }; }
    UVEditorState uv() { return { uvSelectionMode, uvZoom, ... }; }

    // OLD: Keep all existing flat references (don't break anything)
    Camera& camera;
    Camera& camera2;
    // ... all 100+ existing references stay ...
};
```

### Step 4.3: Gradually migrate code to use grouped access

```cpp
// Old (still works):
m_ctx.uvZoom = 1.0f;

// New (cleaner):
m_ctx.uv().zoom = 1.0f;
```

Migrate one group at a time. Old code keeps working.

**Time estimate:** 3-4 hours
**Risk:** Medium (more changes, but backwards compatible)

---

## PHASE 5: Extract Sub-Systems (HIGHER RISK - OPTIONAL)

Only attempt after Phases 1-4 are stable.

### Potential extractions:

1. **UVEditor class** - Pull UV window into its own class
2. **GizmoSystem class** - Separate gizmo rendering/picking
3. **SelectionManager class** - Handle all selection logic
4. **PaintSystem class** - Texture painting tools

Each would:
- Own its relevant state
- Expose clean interface
- Be constructed by ModelingMode
- Reduce EditorContext size

**Risk:** Higher - requires careful interface design

---

## BACKUP PROCEDURE

Before starting any phase:

```bash
# Create dated backup
cd /home/jasondube/Desktop
cp -r EDEN EDEN_backup_$(date +%Y%m%d_%H%M%S)

# Or create a zip
zip -r EDEN_backup_$(date +%Y%m%d).zip EDEN/
```

---

## RECOMMENDED ORDER

1. **Backup** (5 min)
2. **Phase 1** - Split ModelingMode.cpp (2-3 hours) - HIGHEST VALUE
3. **Phase 2** - Extract constants (1-2 hours) - EASY WIN
4. **Phase 3** - Add state enums (1 hour) - DOCUMENTATION VALUE
5. **Phase 4** - Group EditorContext (3-4 hours) - WHEN READY
6. **Phase 5** - Extract sub-systems (future) - ONLY IF NEEDED

---

## WHAT WE'RE NOT TOUCHING

- EditableMesh.cpp - Works great, half-edge is solid
- AnimationMode - Small and clean
- IEditorMode interface - Good abstraction
- Vulkan/rendering layer - Not the problem
- main.cpp ownership model - Complex but working

---

## SUCCESS CRITERIA

After refactoring:
- [ ] All existing features still work
- [ ] No new bugs introduced
- [ ] Code is easier to navigate
- [ ] Adding new features is less scary
- [ ] Build time might improve (parallel compilation of split files)

---

## ROLLBACK PLAN

If anything goes wrong:
1. Stop immediately
2. `cp -r EDEN_backup_YYYYMMDD/* EDEN/`
3. Rebuild
4. Figure out what went wrong before trying again

---

Good night! We'll tackle this carefully tomorrow.
