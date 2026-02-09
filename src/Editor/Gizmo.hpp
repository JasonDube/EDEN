#pragma once

#include <glm/glm.hpp>
#include <eden/Camera.hpp>
#include <vector>

namespace eden {

enum class GizmoAxis {
    None,
    X,
    Y,
    Z
};

struct GizmoVertex {
    glm::vec3 position;
    glm::vec3 color;
};

class Gizmo {
public:
    Gizmo();

    // Set gizmo position in world space
    void setPosition(const glm::vec3& pos) { m_position = pos; }
    glm::vec3 getPosition() const { return m_position; }

    // Set visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Update hover state based on mouse position
    void updateHover(const glm::vec3& rayOrigin, const glm::vec3& rayDir);

    // Begin dragging on currently hovered axis
    bool beginDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir);

    // Update drag and return world-space delta movement
    glm::vec3 updateDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir);

    // End dragging
    void endDrag();

    // Check if currently dragging
    bool isDragging() const { return m_dragging; }

    // Get currently hovered/active axis
    GizmoAxis getActiveAxis() const { return m_dragging ? m_dragAxis : m_hoverAxis; }

    // Get vertices for rendering (rebuilt when hover changes)
    const std::vector<GizmoVertex>& getVertices() const { return m_vertices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    // Check if mesh needs to be re-uploaded
    bool needsUpload() const { return m_needsUpload; }
    void markUploaded() { m_needsUpload = false; }

    // Set buffer handle for rendering
    void setBufferHandle(uint32_t handle) { m_bufferHandle = handle; }
    uint32_t getBufferHandle() const { return m_bufferHandle; }

    // Get axis size for camera-based scaling
    float getSize() const { return m_size; }
    void setSize(float size) { m_size = size; }

private:
    void rebuildMesh();
    void buildArrow(const glm::vec3& dir, const glm::vec3& color, bool highlighted);
    float rayAxisDistance(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& axisDir) const;
    glm::vec3 projectPointOnAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& axisDir) const;

    glm::vec3 m_position{0};
    float m_size = 5.0f;  // Size of gizmo arrows
    bool m_visible = false;

    GizmoAxis m_hoverAxis = GizmoAxis::None;
    GizmoAxis m_dragAxis = GizmoAxis::None;
    bool m_dragging = false;
    glm::vec3 m_dragStartPoint{0};

    std::vector<GizmoVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    bool m_needsUpload = true;
    uint32_t m_bufferHandle = UINT32_MAX;
};

} // namespace eden
