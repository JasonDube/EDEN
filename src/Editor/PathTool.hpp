#pragma once

#include <eden/Terrain.hpp>
#include <eden/Camera.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace eden {

struct LoadedMesh;  // Forward declaration

class PathTool {
public:
    PathTool(Terrain& terrain, Camera& camera);

    // Control point management
    void addPoint(const glm::vec3& worldPos);
    void removeLastPoint();
    void clearPoints();

    // Spline evaluation (Catmull-Rom)
    glm::vec3 evaluateSpline(float t) const;
    std::vector<glm::vec3> sampleSpline(int samplesPerSegment = 16) const;

    // Apply brush along entire path
    void applyToPath(BrushMode mode, float radius, float strength, float falloff,
                     const glm::vec3& paintColor = glm::vec3(0.5f),
                     int textureIndex = 0,
                     float texHue = 0.0f, float texSat = 1.0f, float texBright = 1.0f,
                     float targetElevation = 0.0f);

    // Preview position update (call each frame when in path mode)
    void updatePreview(float normalizedMouseX, float normalizedMouseY, float aspect);

    // Getters
    const std::vector<glm::vec3>& getControlPoints() const { return m_controlPoints; }
    size_t getPointCount() const { return m_controlPoints.size(); }
    bool hasValidPreviewPos() const { return m_hasValidPreview; }
    glm::vec3 getPreviewPos() const { return m_previewPos; }

    // Get approximate path length
    float getPathLength() const;

    // Generate tube mesh along the spline path
    LoadedMesh generateTubeMesh(float radius = 0.1f, int segments = 8,
                                 const glm::vec3& color = glm::vec3(0.2f, 0.2f, 0.2f)) const;

    // Generate flat road mesh along the spline path
    LoadedMesh generateRoadMesh(float width = 4.0f,
                                 const glm::vec3& color = glm::vec3(0.4f, 0.4f, 0.4f),
                                 bool useFixedY = false,
                                 float fixedY = 0.0f) const;

private:
    Terrain& m_terrain;
    Camera& m_camera;

    std::vector<glm::vec3> m_controlPoints;
    glm::vec3 m_previewPos{0};
    bool m_hasValidPreview = false;

    // Catmull-Rom spline interpolation
    glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                         const glm::vec3& p2, const glm::vec3& p3, float t) const;
};

} // namespace eden
