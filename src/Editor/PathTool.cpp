#include "PathTool.hpp"
#include "GLBLoader.hpp"
#include "Renderer/ModelRenderer.hpp"
#include <cmath>
#include <algorithm>
#include <glm/gtc/constants.hpp>

namespace eden {

PathTool::PathTool(Terrain& terrain, Camera& camera)
    : m_terrain(terrain)
    , m_camera(camera)
{
}

void PathTool::addPoint(const glm::vec3& worldPos) {
    m_controlPoints.push_back(worldPos);
}

void PathTool::removeLastPoint() {
    if (!m_controlPoints.empty()) {
        m_controlPoints.pop_back();
    }
}

void PathTool::clearPoints() {
    m_controlPoints.clear();
}

void PathTool::updatePreview(float normalizedMouseX, float normalizedMouseY, float aspect) {
    glm::vec3 rayDir = m_camera.screenToWorldRay(normalizedMouseX, normalizedMouseY, aspect);
    glm::vec3 hitPos;

    if (m_terrain.raycast(m_camera.getPosition(), rayDir, hitPos)) {
        m_previewPos = hitPos;
        m_hasValidPreview = true;
    } else {
        m_hasValidPreview = false;
    }
}

glm::vec3 PathTool::catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                               const glm::vec3& p2, const glm::vec3& p3, float t) const {
    // Catmull-Rom spline formula
    float t2 = t * t;
    float t3 = t2 * t;

    glm::vec3 result = 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );

    return result;
}

glm::vec3 PathTool::evaluateSpline(float t) const {
    if (m_controlPoints.size() < 2) {
        if (m_controlPoints.size() == 1) {
            return m_controlPoints[0];
        }
        return glm::vec3(0);
    }

    // Number of segments is (n-1) for n control points
    size_t numSegments = m_controlPoints.size() - 1;

    // Scale t to segment index
    float scaledT = t * numSegments;
    size_t segmentIndex = static_cast<size_t>(std::floor(scaledT));
    segmentIndex = std::min(segmentIndex, numSegments - 1);

    float localT = scaledT - segmentIndex;

    // Get the 4 control points for this segment
    // For boundary segments, we repeat endpoints
    size_t i0 = (segmentIndex == 0) ? 0 : segmentIndex - 1;
    size_t i1 = segmentIndex;
    size_t i2 = segmentIndex + 1;
    size_t i3 = std::min(segmentIndex + 2, m_controlPoints.size() - 1);

    return catmullRom(m_controlPoints[i0], m_controlPoints[i1],
                      m_controlPoints[i2], m_controlPoints[i3], localT);
}

std::vector<glm::vec3> PathTool::sampleSpline(int samplesPerSegment) const {
    std::vector<glm::vec3> samples;

    if (m_controlPoints.size() < 2) {
        // Just return the control points if we have any
        for (const auto& pt : m_controlPoints) {
            samples.push_back(pt);
        }
        return samples;
    }

    size_t numSegments = m_controlPoints.size() - 1;
    int totalSamples = static_cast<int>(numSegments) * samplesPerSegment + 1;

    samples.reserve(totalSamples);

    for (int i = 0; i < totalSamples; i++) {
        float t = static_cast<float>(i) / (totalSamples - 1);
        glm::vec3 point = evaluateSpline(t);

        // Sample terrain height at this position to keep curve on terrain
        point.y = m_terrain.getHeightAt(point.x, point.z);

        samples.push_back(point);
    }

    return samples;
}

float PathTool::getPathLength() const {
    if (m_controlPoints.size() < 2) {
        return 0.0f;
    }

    // Sample the spline and sum distances
    auto samples = sampleSpline(16);
    float length = 0.0f;

    for (size_t i = 1; i < samples.size(); i++) {
        length += glm::length(samples[i] - samples[i-1]);
    }

    return length;
}

void PathTool::applyToPath(BrushMode mode, float radius, float strength, float falloff,
                           const glm::vec3& paintColor, int textureIndex,
                           float texHue, float texSat, float texBright) {
    if (m_controlPoints.size() < 2) {
        return;
    }

    // Sample the spline densely - sample spacing should be less than radius
    // to ensure continuous brush application
    float pathLength = getPathLength();
    float sampleSpacing = radius * 0.5f;  // Sample every half-radius for good coverage
    int numSamples = std::max(2, static_cast<int>(pathLength / sampleSpacing));

    // Scale strength for per-sample application
    // Since we're applying multiple times, reduce strength proportionally
    float scaledStrength = strength * sampleSpacing / radius;

    for (int i = 0; i < numSamples; i++) {
        float t = static_cast<float>(i) / (numSamples - 1);
        glm::vec3 point = evaluateSpline(t);

        // Apply the appropriate brush at this point
        switch (mode) {
            case BrushMode::Paint:
                m_terrain.applyColorBrush(point.x, point.z, radius, scaledStrength, falloff, paintColor);
                break;

            case BrushMode::Texture:
                m_terrain.applyTextureBrush(point.x, point.z, radius, scaledStrength, falloff,
                                            textureIndex, texHue, texSat, texBright);
                break;

            case BrushMode::Select:
                m_terrain.applySelectionBrush(point.x, point.z, radius, scaledStrength, falloff, true);
                break;

            case BrushMode::Deselect:
                m_terrain.applySelectionBrush(point.x, point.z, radius, scaledStrength, falloff, false);
                break;

            default:
                // Height-modifying brushes (Raise, Lower, Smooth, Flatten, Trench, etc.)
                m_terrain.applyBrush(point.x, point.z, radius, scaledStrength, falloff, mode);
                break;
        }
    }
}

LoadedMesh PathTool::generateTubeMesh(float radius, int segments, const glm::vec3& color) const {
    LoadedMesh mesh;
    mesh.name = "TubePath";

    if (m_controlPoints.size() < 2) {
        return mesh;
    }

    // Sample the spline (terrain-following)
    auto samples = sampleSpline(16);
    if (samples.size() < 2) {
        return mesh;
    }

    // Lift samples slightly above terrain to prevent z-fighting
    for (auto& s : samples) {
        s.y += radius * 0.5f;
    }

    const float twoPi = glm::two_pi<float>();
    glm::vec4 vertColor(color, 1.0f);

    // For each sample point, generate a circle of vertices
    // Use parallel transport to maintain consistent orientation along the curve
    glm::vec3 prevNormal(0, 1, 0);  // Start with up vector

    for (size_t i = 0; i < samples.size(); i++) {
        // Calculate tangent (forward direction)
        glm::vec3 tangent;
        if (i == 0) {
            tangent = glm::normalize(samples[1] - samples[0]);
        } else if (i == samples.size() - 1) {
            tangent = glm::normalize(samples[i] - samples[i - 1]);
        } else {
            tangent = glm::normalize(samples[i + 1] - samples[i - 1]);
        }

        // Calculate normal and binormal using parallel transport
        // Project previous normal onto plane perpendicular to new tangent
        glm::vec3 normal = prevNormal - glm::dot(prevNormal, tangent) * tangent;
        if (glm::length(normal) < 0.001f) {
            // Degenerate case - pick arbitrary perpendicular
            normal = glm::abs(tangent.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            normal = normal - glm::dot(normal, tangent) * tangent;
        }
        normal = glm::normalize(normal);
        prevNormal = normal;

        glm::vec3 binormal = glm::cross(tangent, normal);

        // Generate circle vertices at this position
        for (int j = 0; j < segments; j++) {
            float angle = (static_cast<float>(j) / segments) * twoPi;
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Position on circle
            glm::vec3 offset = (normal * cosA + binormal * sinA) * radius;
            glm::vec3 pos = samples[i] + offset;

            // Normal points outward from center
            glm::vec3 norm = glm::normalize(offset);

            // UV coordinates
            float u = static_cast<float>(j) / segments;
            float v = static_cast<float>(i) / (samples.size() - 1);

            ModelVertex vert;
            vert.position = pos;
            vert.normal = norm;
            vert.texCoord = glm::vec2(u, v);
            vert.color = vertColor;

            mesh.vertices.push_back(vert);
        }
    }

    // Generate indices connecting adjacent rings
    for (size_t i = 0; i < samples.size() - 1; i++) {
        uint32_t ringStart = static_cast<uint32_t>(i * segments);
        uint32_t nextRingStart = static_cast<uint32_t>((i + 1) * segments);

        for (int j = 0; j < segments; j++) {
            uint32_t j0 = static_cast<uint32_t>(j);
            uint32_t j1 = static_cast<uint32_t>((j + 1) % segments);

            // Two triangles per quad (reversed winding)
            // Triangle 1
            mesh.indices.push_back(ringStart + j0);
            mesh.indices.push_back(ringStart + j1);
            mesh.indices.push_back(nextRingStart + j0);

            // Triangle 2
            mesh.indices.push_back(ringStart + j1);
            mesh.indices.push_back(nextRingStart + j1);
            mesh.indices.push_back(nextRingStart + j0);
        }
    }

    // Cap the start (first ring)
    {
        // Add center vertex for start cap
        ModelVertex centerVert;
        centerVert.position = samples[0];
        glm::vec3 tangent = glm::normalize(samples[1] - samples[0]);
        centerVert.normal = -tangent;  // Point backward
        centerVert.texCoord = glm::vec2(0.5f, 0.0f);
        centerVert.color = vertColor;
        uint32_t centerIdx = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(centerVert);

        // Fan triangles (reversed winding)
        for (int j = 0; j < segments; j++) {
            uint32_t j0 = static_cast<uint32_t>(j);
            uint32_t j1 = static_cast<uint32_t>((j + 1) % segments);
            mesh.indices.push_back(centerIdx);
            mesh.indices.push_back(j0);
            mesh.indices.push_back(j1);
        }
    }

    // Cap the end (last ring)
    {
        uint32_t lastRingStart = static_cast<uint32_t>((samples.size() - 1) * segments);

        ModelVertex centerVert;
        centerVert.position = samples.back();
        glm::vec3 tangent = glm::normalize(samples.back() - samples[samples.size() - 2]);
        centerVert.normal = tangent;  // Point forward
        centerVert.texCoord = glm::vec2(0.5f, 1.0f);
        centerVert.color = vertColor;
        uint32_t centerIdx = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(centerVert);

        // Fan triangles (reversed winding)
        for (int j = 0; j < segments; j++) {
            uint32_t j0 = static_cast<uint32_t>(j);
            uint32_t j1 = static_cast<uint32_t>((j + 1) % segments);
            mesh.indices.push_back(lastRingStart + j1);
            mesh.indices.push_back(lastRingStart + j0);
            mesh.indices.push_back(centerIdx);
        }
    }

    // Calculate bounding box
    mesh.bounds.min = samples[0];
    mesh.bounds.max = samples[0];
    for (const auto& v : mesh.vertices) {
        mesh.bounds.min = glm::min(mesh.bounds.min, v.position);
        mesh.bounds.max = glm::max(mesh.bounds.max, v.position);
    }

    return mesh;
}

} // namespace eden
