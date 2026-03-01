# Notes for Future Reference

## 1. Vulkan GPU Buffer Synchronization Bug (renderLines/renderPoints)

### The Problem
When multiple render calls (e.g., `renderLines`, `renderPoints`) share the same GPU buffer and are called multiple times per frame, later calls overwrite the buffer data before the GPU executes the earlier draw commands.

### Why This Happens
1. CPU copies vertex data to a mapped GPU buffer
2. CPU records a draw command to the command buffer (this just references the buffer, doesn't copy data)
3. CPU returns and the next render call overwrites the same buffer with new data
4. GPU executes the command buffer LATER, but now the buffer contains the LAST call's data

### Symptoms
- Diagonal lines appearing on quads (line buffer was overwritten by point data)
- Missing vertices (only seeing 3 of 4 vertices because multiple `renderPoints` calls overwrote each other)
- Rendering artifacts that only appear when specific features are enabled (e.g., vertex mode)

### The Fix
Use SEPARATE buffers for each type of render call that can occur in the same frame:
- `m_lineBuffer` for `renderLines()`
- `m_pointBuffers[NUM_POINT_BUFFERS]` array for `renderPoints()` - cycles through buffers so each call per frame gets its own

```cpp
// In renderPoints:
size_t bufferIdx = m_currentPointBuffer;
m_currentPointBuffer = (m_currentPointBuffer + 1) % NUM_POINT_BUFFERS;
memcpy(m_pointMappedMemories[bufferIdx], vertices.data(), ...);
// Bind m_pointBuffers[bufferIdx] for this draw call
```

### Key Insight
Command buffer recording and GPU execution are ASYNCHRONOUS. The CPU records commands that reference buffers by handle, but the GPU reads the actual buffer contents later. If you overwrite a buffer between recording and execution, the GPU sees the wrong data.

### Files Involved
- `src/Renderer/ModelRenderer.hpp` - buffer declarations
- `src/Renderer/ModelRenderer.cpp` - buffer creation, destruction, and usage in render functions

---

## 2. Maya-Style Camera Controls

### Controls
- **Alt + LMB drag** → Tumble / Orbit camera around target point
- **Alt + MMB drag** → Pan / Track camera (moves camera and target together)
- **Alt + RMB drag** → Dolly / Zoom (moves camera toward/away from target)
- **Mouse wheel** → Fast dolly zoom
- **F** or **.** (period) → Frame selected object (centers view on selection)

### Implementation Details
- `m_orbitTarget` stores the point the camera orbits around
- Tumble rotates the camera position around `m_orbitTarget` while keeping it facing the target
- Pan moves both the camera AND `m_orbitTarget` together (parallel to view plane)
- Dolly moves the camera along the view direction toward/away from target
- Frame selected calculates object bounding box and positions camera to see the whole object

### View Presets
- **F1** → Front view (Ctrl+F1 for Back)
- **F2** → Right view (Ctrl+F2 for Left)
- **F3** → Top view (Ctrl+F3 for Bottom)
- **F5** → Toggle perspective/orthographic

### Files Involved
- `main.cpp` - `processCameraInput()` and `frameSelected()` functions
- `include/eden/Input.hpp` - KEY_PERIOD added for frame selected shortcut

---

## 3. Move Gizmo System

### Controls
- **W** → Enable/toggle move gizmo
- **Click + drag gizmo axis** → Move selection along that axis
- **Numeric input** → Type exact translation values in the Tools panel

### Implementation Details
- Gizmo renders as 3 colored arrows (X=red, Y=green, Z=blue)
- Hovered/active axis highlights yellow
- Ray-axis distance calculation determines which axis is being picked
- Drag projects mouse movement onto the selected axis
- Supports vertex selection (moves selected vertices)
- Undo state is saved when drag starts

### Key Functions
- `getGizmoPosition()` - Returns center of selection or object origin
- `pickGizmoAxis()` - Ray-axis intersection to determine hovered axis
- `rayAxisDistance()` - Calculates distance from ray to axis line
- `processGizmoInput()` - Handles mouse input for gizmo interaction
- `renderGizmo()` - Draws the gizmo arrows using renderLines

### Files Involved
- `EditorContext.hpp` - GizmoMode and GizmoAxis enums, gizmo state variables
- `ModelingMode.hpp` - Gizmo method declarations
- `ModelingMode.cpp` - Gizmo implementation (processGizmoInput, renderGizmo, etc.)
- `main.cpp` - Gizmo state variables and EditorContext wiring

---

## 4. EditableMesh Data Must Be Copied During Object Operations

### The Problem
SceneObject stores TWO representations of mesh data:
1. **Triangulated data** (`m_vertices`, `m_indices`) - for GPU rendering
2. **Half-edge data** (`m_heVertices`, `m_heHalfEdges`, `m_heFaces`) - preserves quad topology

When copying/duplicating objects, BOTH must be copied. The half-edge data is what preserves quads - without it, `buildEditableMeshFromObject()` falls back to `mergeTrianglesToQuads()` which fails for coplanar faces.

### Symptoms
- Duplicated mesh has diagonal lines in wireframe (quads became triangles)
- Face count changes after duplicate (e.g., 10 quads → 14 faces with tris)
- Works fine on simple meshes, breaks on meshes with edge loops or coplanar faces

### The Fix
Any operation that creates a new SceneObject from an existing one must copy both data sets:
```cpp
newObj->setMeshData(srcObj->getVertices(), srcObj->getIndices());
if (srcObj->hasEditableMeshData()) {
    newObj->setEditableMeshData(
        srcObj->getHEVertices(),
        srcObj->getHEHalfEdges(),
        srcObj->getHEFaces()
    );
}
```

### Future Scenarios Where This Will Bite You
1. **Copy/paste between scenes** - if you add scene copy functionality
2. **Undo/redo of object deletion** - restoring object must include HE data
3. **Level save/load** - serialization must include HE data or it's lost
4. **Object instancing** - instances sharing mesh data need HE data too
5. **Merge objects** - combining meshes needs to merge HE structures

### Files Involved
- `SceneObject.hpp` - `StoredHE*` structs and `setEditableMeshData()`/`getHE*()` methods
- `ModelingMode.cpp` - `updateMeshFromEditable()` saves HE data, duplicate operation copies it
- `EditableMesh.hpp/cpp` - `setFromData()` restores from stored HE data

---

## 5. GLB Export and Load Must Preserve Triangle Topology

### The Problem
Two related issues:

1. **Export**: The export was re-triangulating from half-edge data instead of using the stored triangulated data. This could produce different triangulation than what was rendered.

2. **Load**: When loading GLB files (which are always triangulated), `buildEditableMeshFromObject()` would call `mergeTrianglesToQuads()` to try reconstructing quads. This fails for meshes with non-coplanar adjacent faces (like cylinder caps with normal dot < 0.99), creating corrupted half-edge topology that produces spiral patterns when re-triangulated.

### Symptoms
- Exported/loaded GLB has spiral patterns on cylinder caps
- Topology corruption visible as wrong triangle connections
- Works fine for simple meshes but breaks on cylinders, spheres, etc.

### The Fix
1. **Export**: Use the stored triangulated data from SceneObject directly (same data used for GPU rendering):
```cpp
std::vector<ModelVertex> vertices = m_ctx.selectedObject->getVertices();
std::vector<uint32_t> indices = m_ctx.selectedObject->getIndices();
```

2. **Load**: Don't try to merge triangles to quads - keep loaded meshes as triangles:
```cpp
// In buildEditableMeshFromObject fallback path:
m_ctx.editableMesh.buildFromTriangles(vertices, indices);
// DON'T call: m_ctx.editableMesh.mergeTrianglesToQuads();
```

3. **Transform baking**: Bake object transform into vertices before writing GLB.

### Files Involved
- `ModelingMode.cpp` - `saveEditableMeshAsGLB()` and `buildEditableMeshFromObject()`

---

## 6. Duplicate Must Copy Transform and Texture

### The Problem
When duplicating objects, the original code only offset the position but didn't copy scale/rotation. Texture data was also not copied.

### The Fix
Copy the full transform (scale, rotation) and texture data:
```cpp
// Copy transform
transform.setScale(srcTransform.getScale());
transform.setRotation(srcTransform.getRotation());
transform.setPosition(srcTransform.getPosition() + glm::vec3(1, 0, 0));

// Copy texture
if (srcObj->hasTextureData()) {
    newObj->setTextureData(srcObj->getTextureData(), srcObj->getTextureWidth(), srcObj->getTextureHeight());
}
```

### Files Involved
- `ModelingMode.cpp` - duplicate object code
- `Transform.hpp` - added `setRotation(const glm::quat&)` overload

---

## 7. GLB Format Cannot Preserve Quads - Use OBJ for Ongoing Projects

### The Problem
GLB/glTF format is always triangulated - it has no native support for quads or n-gons. When saving and loading GLB:
1. Half-edge data (which preserves quad topology) is lost
2. On load, `buildEditableMeshFromObject()` tries `mergeTrianglesToQuads()` to reconstruct quads
3. This algorithm uses normal coplanarity threshold and fails for faceted geometry (cylinder caps have ~0.924 normal dot, less than 0.99 threshold)
4. Result: spiral patterns, corrupted topology, lost quad structure

### Why Duplicate Works But Save/Load Doesn't
- **Duplicate**: Copies stored half-edge data directly (`setEditableMeshData()`) - quads preserved
- **GLB Save/Load**: GLB only stores triangulated mesh - half-edge data lost, must reconstruct

### Solutions Attempted
1. **Embed HE data in GLB extras as base64**: Works but bloats file size and is fragile
2. **Lower threshold to 0.85**: Helps with faceted geometry but still imperfect reconstruction

### Final Solution: Use OBJ Format
OBJ format natively supports quads via `f v1 v2 v3 v4` syntax. No conversion needed.

```cpp
// OBJ preserves quads directly:
// f 1/1/1 2/2/1 3/3/1 4/4/1   <- quad face
```

Added functions:
- `EditableMesh::saveOBJ()` / `loadOBJ()` - native quad save/load
- `ModelingMode::saveEditableMeshAsOBJ()` / `loadOBJFile()` - UI integration
- File menu: "Save as OBJ...", "Load OBJ..."

### OBJ Limitation: No Embedded Textures
OBJ format doesn't embed textures - it references external files via MTL. This is problematic for ongoing projects with textures.

### Solution: LIME Format with Embedded Textures (IMPLEMENTED)
The `.lime` format (v2.0) now stores full half-edge mesh data AND embedded textures:

**Mesh Data:**
- Vertices (position, normal, uv, color, halfEdgeIndex, selected)
- Faces (halfEdgeIndex, vertexCount, selected, vertex indices)
- Half-edges (vertexIndex, faceIndex, nextIndex, prevIndex, twinIndex)

**Texture Data:**
- `tex_size: width height` - texture dimensions
- `tex_data: <base64>` - RGBA texture data encoded as base64

**Usage:**
- Ctrl+S saves as LIME (the default working format)
- File menu: "Save as LIME...", "Load LIME..."
- Best format for ongoing projects - preserves quads AND textures

**Format Priority:**
1. **LIME** - Primary working format (quads + textures)
2. **OBJ** - Exchange format (quads, no textures)
3. **GLB** - Final export format (triangulated, textures embedded)

Note: `.eden` extension is reserved for level/scene files.

### Files Involved
- `EditableMesh.hpp/cpp` - `saveOBJ()`, `loadOBJ()`, `saveLime()` (with texture overload), `loadLime()` (with texture overload)
- `ModelingMode.hpp/cpp` - `saveEditableMeshAsOBJ()`, `loadOBJFile()`, `saveEditableMeshAsLime()`, `loadLimeFile()`
- `main.cpp` - File menu integration
- `GLBLoader.hpp/cpp` - attempted HE data embedding (kept as fallback)

---

## 8. Procedurally Generated Meshes Must Store Half-Edge Data

### The Problem
When creating meshes procedurally (e.g., `extrudePipeNetwork`, `extrudeBoxAlongSelectedEdges`), the code creates triangulated vertex/index data for GPU rendering. If you only store this triangulated data, when the object is later selected, `buildEditableMeshFromObject()` calls `mergeTrianglesToQuads()` which incorrectly reconstructs the topology.

### Symptoms
- Diagonal lines appearing in wireframe where there should be clean quads
- Spiral/twisted patterns on tubes and extruded geometry
- Wireframe edges don't match the intended quad structure

### Why This Happens
1. Procedural code creates quads, splits them into 2 triangles for GPU
2. Only triangulated data is stored in SceneObject (`setMeshData()`)
3. When selected, `buildEditableMeshFromObject()` has no half-edge data
4. Falls back to `buildFromTriangles()` + `mergeTrianglesToQuads()`
5. `mergeTrianglesToQuads()` uses coplanarity heuristics that fail for many geometries
6. Result: wrong edges, diagonal lines, corrupted topology

### The Fix
After creating triangulated mesh data, ALSO build an EditableMesh with proper quad faces and store its half-edge data:

```cpp
// Build EditableMesh with proper quad topology
EditableMesh pipeMesh;
pipeMesh.clear();

// Add vertices (4 per cross-section, not 5)
std::vector<uint32_t> crossSectionStarts;
for (size_t v = 0; v < allVerts.size(); v += 5) {
    crossSectionStarts.push_back(static_cast<uint32_t>(pipeMesh.getVertexCount()));
    for (int c = 0; c < 4; ++c) {
        HEVertex hv;
        hv.position = allVerts[v + c].position;
        hv.normal = allVerts[v + c].normal;
        hv.uv = allVerts[v + c].texCoord;
        hv.color = allVerts[v + c].color;
        hv.halfEdgeIndex = UINT32_MAX;
        hv.selected = false;
        pipeMesh.addVertex(hv);
    }
}

// Add quad faces - walk through triangles in groups of 6 (2 tris per quad)
for (size_t i = 0; i < allIndices.size(); i += 6) {
    // Convert 5-vert indexing to 4-vert indexing and create quad
    // ... (see extrudePipeNetwork for full implementation)
    pipeMesh.addQuadFace(quadVerts);
}

// Store both triangulated data AND half-edge data
newObject->setMeshData(allVerts, allIndices);

// Convert and store half-edge data
std::vector<SceneObject::StoredHEVertex> storedVerts;
// ... convert meshVerts to storedVerts
std::vector<SceneObject::StoredHalfEdge> storedHE;
// ... convert meshHE to storedHE
std::vector<SceneObject::StoredHEFace> storedFaces;
// ... convert meshFaces to storedFaces
newObject->setEditableMeshData(storedVerts, storedHE, storedFaces);
```

### Key Insight
The 5-vertex-per-cross-section pattern (4 corners + 1 UV seam duplicate) is for GPU rendering with proper UV continuity. The EditableMesh only needs 4 vertices per cross-section (the actual geometry). When converting indices, map from 5-vert indexing to 4-vert indexing: `section = idx / 5`, `corner = idx % 5 % 4`.

### Files Involved
- `ModelingMode.cpp` - `extrudePipeNetwork()`, `extrudeBoxAlongSelectedEdges()`
- `EditableMesh.hpp/cpp` - `addVertex()`, `addQuadFace()` for building topology
- `SceneObject.hpp` - `setEditableMeshData()` for storage

---

## 9. Combine/Merge Objects Must Preserve Half-Edge Data

### The Problem
When combining multiple objects into one ("Combine Selected" or "Combine All"), the code was only combining the triangulated vertex/index data, losing the half-edge topology. This causes the same diagonal wireframe issue as section 8.

### The Fix
When combining objects, also combine their half-edge data with proper index offsets:

```cpp
// Track combined half-edge data
std::vector<SceneObject::StoredHEVertex> combinedHEVerts;
std::vector<SceneObject::StoredHalfEdge> combinedHE;
std::vector<SceneObject::StoredHEFace> combinedHEFaces;
bool allHaveHEData = true;

for (auto& obj : objects) {
    // ... combine triangulated data as before ...

    // Combine half-edge data if available
    if (obj->hasEditableMeshData()) {
        uint32_t heVertOffset = combinedHEVerts.size();
        uint32_t heOffset = combinedHE.size();
        uint32_t heFaceOffset = combinedHEFaces.size();

        for (const auto& v : obj->getHEVertices()) {
            StoredHEVertex newV = v;
            // Apply transform to position/normal
            if (newV.halfEdgeIndex != UINT32_MAX) newV.halfEdgeIndex += heOffset;
            combinedHEVerts.push_back(newV);
        }

        for (const auto& he : obj->getHEHalfEdges()) {
            StoredHalfEdge newHE = he;
            // Offset ALL indices: vertexIndex, faceIndex, nextIndex, prevIndex, twinIndex
            if (newHE.vertexIndex != UINT32_MAX) newHE.vertexIndex += heVertOffset;
            if (newHE.faceIndex != UINT32_MAX) newHE.faceIndex += heFaceOffset;
            if (newHE.nextIndex != UINT32_MAX) newHE.nextIndex += heOffset;
            if (newHE.prevIndex != UINT32_MAX) newHE.prevIndex += heOffset;
            if (newHE.twinIndex != UINT32_MAX) newHE.twinIndex += heOffset;
            combinedHE.push_back(newHE);
        }

        for (const auto& f : obj->getHEFaces()) {
            StoredHEFace newF = f;
            if (newF.halfEdgeIndex != UINT32_MAX) newF.halfEdgeIndex += heOffset;
            combinedHEFaces.push_back(newF);
        }
    } else {
        allHaveHEData = false;  // Can't preserve if any source lacks HE data
    }
}

// Store combined HE data only if ALL sources had it
if (allHaveHEData && !combinedHEVerts.empty()) {
    combinedObj->setEditableMeshData(combinedHEVerts, combinedHE, combinedHEFaces);
}
```

### Key Points
- Must offset ALL half-edge indices (vertex, face, next, prev, twin)
- Check for UINT32_MAX before offsetting (invalid/boundary markers)
- Only store combined HE data if ALL source objects have it
- Apply object transforms to positions and normals

### Files Involved
- `ModelingMode.cpp` - "Combine Selected" and "Combine All" button handlers
