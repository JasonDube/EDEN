#pragma once

#include <eden/Terrain.hpp>
#include <eden/Camera.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace eden {

class TerrainBrushTool {
public:
    TerrainBrushTool(Terrain& terrain, Camera& camera);

    // Update brush preview position (call every frame)
    void updatePreview(float normalizedMouseX, float normalizedMouseY, float aspect);

    // Apply brush at current position
    void apply(float deltaTime);

    // Grab brush methods
    void beginGrab();
    void updateGrab(float deltaY);  // deltaY = mouse movement in world units
    void endGrab();
    bool isGrabbing() const { return m_isGrabbing; }

    // Setters for brush parameters
    void setMode(BrushMode mode) { m_mode = mode; }
    void setRadius(float radius) { m_radius = radius; }
    void setStrength(float strength) { m_strength = strength; }
    void setFalloff(float falloff) { m_falloff = falloff; }
    void setShape(BrushShape shape) { m_shapeParams.shape = shape; }
    void setShapeAspectRatio(float ratio) { m_shapeParams.aspectRatio = ratio; }
    void setShapeRotation(float radians) { m_shapeParams.rotation = radians; }
    const BrushShapeParams& getShapeParams() const { return m_shapeParams; }
    void setPaintColor(const glm::vec3& color) { m_paintColor = color; }
    void setTextureIndex(int index) { m_textureIndex = index; }
    void setTextureHSB(float hue, float saturation, float brightness) {
        m_texHue = hue;
        m_texSaturation = saturation;
        m_texBrightness = brightness;
    }
    void setTargetElevation(float y) { m_targetElevation = y; }

    // Getters
    bool hasValidPosition() const { return m_hasValidPosition; }
    glm::vec3 getPosition() const { return m_position; }
    BrushMode getMode() const { return m_mode; }

private:
    Terrain& m_terrain;
    Camera& m_camera;

    // Brush state
    BrushMode m_mode = BrushMode::Raise;
    BrushShapeParams m_shapeParams;
    float m_radius = 15.0f;
    float m_strength = 20.0f;
    float m_falloff = 0.5f;
    glm::vec3 m_paintColor{0.2f, 0.5f, 0.15f};
    int m_textureIndex = 1;
    float m_texHue = 0.0f;
    float m_texSaturation = 1.0f;
    float m_texBrightness = 1.0f;
    float m_targetElevation = 0.0f;

    // Current brush position
    glm::vec3 m_position{0};
    bool m_hasValidPosition = false;

    // Grab brush state
    struct GrabbedVertex {
        std::shared_ptr<TerrainChunk> chunk;
        int localX, localZ;
        float originalHeight;
        float weight;  // Falloff weight
    };
    std::vector<GrabbedVertex> m_grabbedVertices;
    bool m_isGrabbing = false;
    glm::vec3 m_grabStartPos{0};
};

} // namespace eden
