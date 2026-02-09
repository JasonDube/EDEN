#include "Gizmo.hpp"
#include <cmath>
#include <algorithm>

namespace eden {

Gizmo::Gizmo() {
    rebuildMesh();
}

void Gizmo::updateHover(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    if (m_dragging || !m_visible) {
        return;
    }

    const float hitThreshold = m_size * 0.15f;  // How close to axis to register hover

    // Check each axis
    float distX = rayAxisDistance(rayOrigin, rayDir, glm::vec3(1, 0, 0));
    float distY = rayAxisDistance(rayOrigin, rayDir, glm::vec3(0, 1, 0));
    float distZ = rayAxisDistance(rayOrigin, rayDir, glm::vec3(0, 0, 1));

    GizmoAxis newHover = GizmoAxis::None;
    float minDist = hitThreshold;

    if (distX < minDist) {
        minDist = distX;
        newHover = GizmoAxis::X;
    }
    if (distY < minDist) {
        minDist = distY;
        newHover = GizmoAxis::Y;
    }
    if (distZ < minDist) {
        minDist = distZ;
        newHover = GizmoAxis::Z;
    }

    if (newHover != m_hoverAxis) {
        m_hoverAxis = newHover;
        rebuildMesh();
    }
}

bool Gizmo::beginDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    if (!m_visible || m_hoverAxis == GizmoAxis::None) {
        return false;
    }

    m_dragging = true;
    m_dragAxis = m_hoverAxis;

    glm::vec3 axisDir;
    switch (m_dragAxis) {
        case GizmoAxis::X: axisDir = glm::vec3(1, 0, 0); break;
        case GizmoAxis::Y: axisDir = glm::vec3(0, 1, 0); break;
        case GizmoAxis::Z: axisDir = glm::vec3(0, 0, 1); break;
        default: return false;
    }

    m_dragStartPoint = projectPointOnAxis(rayOrigin, rayDir, axisDir);
    return true;
}

glm::vec3 Gizmo::updateDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    if (!m_dragging) {
        return glm::vec3(0);
    }

    glm::vec3 axisDir;
    switch (m_dragAxis) {
        case GizmoAxis::X: axisDir = glm::vec3(1, 0, 0); break;
        case GizmoAxis::Y: axisDir = glm::vec3(0, 1, 0); break;
        case GizmoAxis::Z: axisDir = glm::vec3(0, 0, 1); break;
        default: return glm::vec3(0);
    }

    glm::vec3 currentPoint = projectPointOnAxis(rayOrigin, rayDir, axisDir);
    glm::vec3 delta = currentPoint - m_dragStartPoint;

    // Only allow movement along the active axis
    delta = axisDir * glm::dot(delta, axisDir);

    m_dragStartPoint = currentPoint;
    m_position += delta;

    return delta;
}

void Gizmo::endDrag() {
    m_dragging = false;
    m_dragAxis = GizmoAxis::None;
    rebuildMesh();
}

float Gizmo::rayAxisDistance(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& axisDir) const {
    // Find closest point between ray and axis line
    glm::vec3 axisOrigin = m_position;
    glm::vec3 axisEnd = m_position + axisDir * m_size;

    // Vector from ray origin to axis origin
    glm::vec3 w0 = rayOrigin - axisOrigin;

    float a = glm::dot(rayDir, rayDir);
    float b = glm::dot(rayDir, axisDir);
    float c = glm::dot(axisDir, axisDir);
    float d = glm::dot(rayDir, w0);
    float e = glm::dot(axisDir, w0);

    float denom = a * c - b * b;
    if (std::abs(denom) < 0.0001f) {
        return 1000.0f;  // Lines are parallel
    }

    float sc = (b * e - c * d) / denom;
    float tc = (a * e - b * d) / denom;

    // Clamp tc to the axis segment
    tc = std::clamp(tc, 0.0f, 1.0f);

    // Ensure we're looking at the positive part of the ray
    if (sc < 0) {
        return 1000.0f;
    }

    glm::vec3 closestOnRay = rayOrigin + rayDir * sc;
    glm::vec3 closestOnAxis = axisOrigin + axisDir * (tc * m_size);

    return glm::length(closestOnRay - closestOnAxis);
}

glm::vec3 Gizmo::projectPointOnAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& axisDir) const {
    // Project ray onto the plane that contains the axis
    // Find the point on the ray that's closest to the axis line

    glm::vec3 axisOrigin = m_position;

    // Use the method from rayAxisDistance but return the point
    glm::vec3 w0 = rayOrigin - axisOrigin;

    float a = glm::dot(rayDir, rayDir);
    float b = glm::dot(rayDir, axisDir);
    float c = glm::dot(axisDir, axisDir);
    float d = glm::dot(rayDir, w0);
    float e = glm::dot(axisDir, w0);

    float denom = a * c - b * b;
    if (std::abs(denom) < 0.0001f) {
        return axisOrigin;
    }

    float tc = (a * e - b * d) / denom;

    return axisOrigin + axisDir * tc;
}

void Gizmo::rebuildMesh() {
    m_vertices.clear();
    m_indices.clear();

    // Build arrows for each axis
    bool xHighlight = (m_hoverAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X);
    bool yHighlight = (m_hoverAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y);
    bool zHighlight = (m_hoverAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z);

    buildArrow(glm::vec3(1, 0, 0), glm::vec3(1, 0, 0), xHighlight);  // X axis - red
    buildArrow(glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), yHighlight);  // Y axis - green
    buildArrow(glm::vec3(0, 0, 1), glm::vec3(0, 0, 1), zHighlight);  // Z axis - blue

    m_needsUpload = true;
}

void Gizmo::buildArrow(const glm::vec3& dir, const glm::vec3& baseColor, bool highlighted) {
    // Make highlighted arrows brighter/yellow
    glm::vec3 color = highlighted ? glm::vec3(1.0f, 1.0f, 0.3f) : baseColor;

    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

    // Create a simple arrow using a cylinder and cone
    // For simplicity, we'll create a line with a small cone tip

    glm::vec3 up = (std::abs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    up = glm::normalize(glm::cross(right, dir));

    float shaftRadius = m_size * 0.03f;
    float coneRadius = m_size * 0.08f;
    float shaftLength = m_size * 0.75f;
    float coneLength = m_size * 0.25f;

    // Shaft (cylinder as 8-sided prism)
    const int segments = 8;
    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float nextAngle = (float)(i + 1) / segments * 2.0f * 3.14159f;

        glm::vec3 offset1 = (right * std::cos(angle) + up * std::sin(angle)) * shaftRadius;
        glm::vec3 offset2 = (right * std::cos(nextAngle) + up * std::sin(nextAngle)) * shaftRadius;

        // Bottom of shaft
        uint32_t v0 = static_cast<uint32_t>(m_vertices.size());
        m_vertices.push_back({offset1, color});
        m_vertices.push_back({offset2, color});
        // Top of shaft
        m_vertices.push_back({offset1 + dir * shaftLength, color});
        m_vertices.push_back({offset2 + dir * shaftLength, color});

        // Two triangles for this quad
        m_indices.push_back(v0);
        m_indices.push_back(v0 + 1);
        m_indices.push_back(v0 + 2);
        m_indices.push_back(v0 + 1);
        m_indices.push_back(v0 + 3);
        m_indices.push_back(v0 + 2);
    }

    // Cone tip
    glm::vec3 coneBase = dir * shaftLength;
    glm::vec3 coneTip = dir * (shaftLength + coneLength);

    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float nextAngle = (float)(i + 1) / segments * 2.0f * 3.14159f;

        glm::vec3 offset1 = (right * std::cos(angle) + up * std::sin(angle)) * coneRadius;
        glm::vec3 offset2 = (right * std::cos(nextAngle) + up * std::sin(nextAngle)) * coneRadius;

        uint32_t v0 = static_cast<uint32_t>(m_vertices.size());
        m_vertices.push_back({coneBase + offset1, color});
        m_vertices.push_back({coneBase + offset2, color});
        m_vertices.push_back({coneTip, color});

        m_indices.push_back(v0);
        m_indices.push_back(v0 + 1);
        m_indices.push_back(v0 + 2);
    }
}

} // namespace eden
