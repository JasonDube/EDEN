#pragma once

#include "IEditorMode.hpp"
#include "EditorContext.hpp"

/**
 * @brief Modeling Editor mode for mesh editing
 *
 * Features:
 * - Vertex/Edge/Face selection modes
 * - Extrude, delete, merge operations
 * - Edge loops and rings
 * - UV editor integration
 * - Reference images for ortho views
 * - Grid and transform tools
 */
class ModelingMode : public IEditorMode {
public:
    explicit ModelingMode(EditorContext& ctx);
    ~ModelingMode() override = default;

    void onActivate() override;
    void onDeactivate() override;
    void processInput(float deltaTime) override;
    void update(float deltaTime) override;
    void renderUI() override;
    void renderSceneOverlay(VkCommandBuffer cmd, const glm::mat4& viewProj) override;
    void drawOverlays(float vpX, float vpY, float vpW, float vpH) override;
    const char* getName() const override { return "Modeling Editor"; }
    bool wantsGrid() const override { return m_ctx.showGrid; }
    bool supportsSplitView() const override { return true; }

    // Mesh operations
    void buildEditableMeshFromObject();
    void updateMeshFromEditable();
    void saveEditableMeshAsGLB();
    void saveEditableMeshAsOBJ();
    void saveEditableMeshAsLime();
    void loadOBJFile();
    void loadLimeFile();
    void quickSave();  // F5 - save to current file path/format

    // Reference image operations
    void loadReferenceImage(int viewIndex);

private:
    void renderModelingEditorUI();
    void duplicateSelectedObject();  // Duplicate with random color and select
    void renderModelingUVWindow();
    void renderImageRefWindow();  // Clone source images window
    void createPerspectiveCorrectedStamp(const CloneSourceImage& img);  // Perspective correction
    void processModelingInput(float deltaTime, bool gizmoActive = false);
    void renderModelingOverlay(VkCommandBuffer cmd, const glm::mat4& viewProj);
    void renderGrid3D(VkCommandBuffer cmd, const glm::mat4& viewProj);
    void renderWireframeOverlay3D(VkCommandBuffer cmd, const glm::mat4& viewProj);
    void drawQuadWireframeOverlay(Camera& camera, float vpX, float vpY, float vpW, float vpH);
    void drawFaceNormalsOverlay(Camera& camera, float vpX, float vpY, float vpW, float vpH);
    void drawReferenceImages(Camera& camera, float vpX, float vpY, float vpW, float vpH);

    // Gizmo methods
    void renderGizmo(VkCommandBuffer cmd, const glm::mat4& viewProj);
    bool processGizmoInput();  // Returns true if gizmo consumed the mouse click
    glm::vec3 getGizmoPosition();  // Get position for gizmo (selection center or object origin)
    void getGizmoAxes(glm::vec3& xAxis, glm::vec3& yAxis, glm::vec3& zAxis);  // Get local/world axes
    GizmoAxis pickGizmoAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPos);
    float rayAxisDistance(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                          const glm::vec3& axisOrigin, const glm::vec3& axisDir);
    glm::vec3 projectPointOntoAxis(const glm::vec3& point, const glm::vec3& axisOrigin, const glm::vec3& axisDir);

    // Camera helpers
    void startCameraTumble();

    // UV helpers
    bool pointInUVTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c);
    int findUVFaceAtPoint(const glm::vec2& uvPoint);
    int findUVVertexAtPoint(const glm::vec2& uvPoint, float threshold);
    void selectUVIsland(uint32_t startFace);
    std::set<uint32_t> getUVSelectedVertices();
    void getUVSelectionBounds(glm::vec2& outMin, glm::vec2& outMax);
    void storeOriginalUVs();
    void storeOriginalUVsForVertices();
    void moveSelectedUVs(const glm::vec2& delta);
    void moveSelectedUVVertices(const glm::vec2& delta);
    void scaleSelectedUVs(const glm::vec2& center, float scale);
    void scaleSelectedUVsFromAnchor(const glm::vec2& anchor, float scaleX, float scaleY);
    void rotateSelectedUVs(const glm::vec2& center, float angleDegrees);

    // Edge path extrusion
    void extrudeBoxAlongSelectedEdges(float boxSize, float taper = 1.0f, bool autoUV = true);
    std::vector<uint32_t> orderSelectedEdgesIntoPath();

    // Pipe network extrusion (handles junctions and corners)
    void extrudePipeNetwork(float boxSize, float blockSizeMultiplier = 1.0f, bool autoUV = true);

    // UV sewing helpers
    float pointToLineSegmentDistUV(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b);
    std::pair<uint32_t, uint32_t> findUVEdgeAtPoint(const glm::vec2& uvPoint, float threshold = 0.02f);
    std::pair<glm::vec3, glm::vec3> getEdge3DPositions(uint32_t faceIdx, uint32_t localEdgeIdx);
    std::pair<glm::vec2, glm::vec2> getEdgeUVs(uint32_t faceIdx, uint32_t localEdgeIdx);
    bool positions3DEqual(const glm::vec3& a, const glm::vec3& b, float tol = 0.0001f);
    void findTwinUVEdges(uint32_t selectedFaceIdx, uint32_t selectedEdgeIdx);
    void clearUVEdgeSelection();
    std::set<uint32_t> getUVIslandFaces(uint32_t startFace);
    std::set<uint32_t> getIslandVertices(const std::set<uint32_t>& faces);
    void sewSelectedEdge();
    void moveAndSewSelectedEdge();
    void unsewSelectedEdge();

    // UV baking - draws UV edges onto texture
    void bakeUVEdgesToTexture(const glm::vec3& edgeColor, int lineThickness = 1);

    // Vertex paint state
    bool m_vertexPaintMode = false;
    glm::vec3 m_vertexPaintColor = glm::vec3(1.0f, 0.0f, 0.0f);
    float m_vertexPaintRadius = 0.2f;
    float m_vertexPaintStrength = 1.0f;
    bool m_vertexPaintingActive = false;  // Currently in a paint stroke
    bool m_vertexPaintSquare = true;      // Square brush (pixel art style) vs circular

    // Image reference window state
    int m_pendingCloneImageDelete = -1;  // Index to delete, -1 means none

    // Rectangle stamp selection state
    bool m_stampSelecting = false;       // Currently dragging a selection
    glm::vec2 m_stampSelectStart{0.0f};  // Start pixel in image
    glm::vec2 m_stampSelectEnd{0.0f};    // End pixel in image
    int m_stampSelectImageIdx = -1;      // Which image we're selecting from
    bool m_pendingStampPreviewUpdate = false;  // Deferred stamp preview texture update

    // Perspective correction state
    bool m_perspectiveMode = false;      // Placing corners for perspective correction
    glm::vec2 m_perspectiveCorners[4];   // 4 corner positions in image pixel coordinates
    int m_perspectiveCornerCount = 0;    // How many corners placed (0-4)
    int m_perspectiveImageIdx = -1;      // Which image we're placing corners on

    // Face snap state
    bool m_snapMode = false;             // Snap tool is active
    bool m_snapMergeMode = false;        // If true, merge objects after snap
    SceneObject* m_snapSourceObject = nullptr;  // First object selected
    int m_snapSourceFace = -1;           // Face index on source object
    glm::vec3 m_snapSourceCenter{0.0f};  // Center of source face (world space)
    glm::vec3 m_snapSourceNormal{0.0f};  // Normal of source face (world space)

    // Snap & Merge vertex selection mode (ordered vertex correspondence)
    bool m_snapVertexMode = false;       // Vertex selection mode active
    SceneObject* m_snapSrcObj = nullptr; // Source object for merge
    SceneObject* m_snapDstObj = nullptr; // Target object for merge
    std::vector<glm::vec3> m_snapSrcVerts;   // Source vertices (world positions, in order)
    std::vector<glm::vec3> m_snapDstVerts;   // Target vertices (world positions, in order)
    std::vector<uint32_t> m_snapSrcVertIndices;  // Source vertex indices (for rendering)
    std::vector<uint32_t> m_snapDstVertIndices;  // Target vertex indices (for rendering)

    // Custom gizmo pivot (for post-snap rotation)
    bool m_useCustomGizmoPivot = false;
    glm::vec3 m_customGizmoPivot{0.0f};

    // Mode switch notification
    float m_modeNotificationTimer = 0.0f;
    float m_saveNotificationTimer = 0.0f;

    // UV rectangle selection
    bool m_uvRectSelecting = false;
    glm::vec2 m_uvRectStart{0.0f};
    glm::vec2 m_uvRectEnd{0.0f};

    // Snap helper methods
    void cancelSnapMode();
    void cancelSnapVertexMode();
    glm::vec3 getFaceCenter(SceneObject* obj, int faceIdx);
    glm::vec3 getFaceNormal(SceneObject* obj, int faceIdx);
    void snapObjectToFace(SceneObject* srcObj, int srcFace, SceneObject* dstObj, int dstFace);
    void snapAndMergeObjects(SceneObject* srcObj, int srcFace, SceneObject* dstObj, int dstFace);
    void snapAndMergeWithVertexCorrespondence();  // Uses m_snapSrcVerts/m_snapDstVerts
    void drawSnapVertexOverlay(float vpX, float vpY, float vpW, float vpH);

    // Retopology state
    bool m_retopologyMode = false;           // Place Vertex tool active
    SceneObject* m_retopologyLiveObj = nullptr;  // "Live" reference surface
    std::vector<glm::vec3> m_retopologyVerts;    // Placed vertices (world positions)
    std::vector<glm::vec3> m_retopologyNormals;  // Surface normals at placed vertices
    std::vector<uint32_t> m_retopologyVertMeshIdx; // Editable mesh index (UINT32_MAX = new vert)
    bool m_retopologyObjCreated = false;     // Whether we've created the retopo scene object
    bool m_retopologyDragging = false;       // G-key grab mode active
    int m_retopologyDragQuadIdx = -1;        // Which quad overlay entry to update
    int m_retopologyDragQuadVert = -1;       // Which corner of that quad (0-3)
    glm::vec3 m_retopologyDragOrigPos{0.0f}; // Original position for cancel

    // Accumulated retopo quads (for overlay drawing before finalize)
    struct RetopologyQuad {
        glm::vec3 verts[4];  // World positions
    };
    std::vector<RetopologyQuad> m_retopologyQuads;

    // Retopology methods
    void drawRetopologyOverlay(float vpX, float vpY, float vpW, float vpH);
    void cancelRetopologyMode();
    void createRetopologyQuad();     // Creates quad from 4 placed vertices
    void finalizeRetopologyMesh();   // Build GPU mesh from accumulated quads

    // Auto-retopology (voxel remesh)
    void autoRetopology();           // Auto-retopo from live surface
    int m_autoRetopResolution = 32;
    int m_autoRetopSmoothIter = 5;
};
