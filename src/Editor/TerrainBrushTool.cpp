#include "TerrainBrushTool.hpp"
#include <cmath>

namespace eden {

TerrainBrushTool::TerrainBrushTool(Terrain& terrain, Camera& camera)
    : m_terrain(terrain)
    , m_camera(camera)
{
}

void TerrainBrushTool::updatePreview(float normalizedMouseX, float normalizedMouseY, float aspect) {
    glm::vec3 rayDir = m_camera.screenToWorldRay(normalizedMouseX, normalizedMouseY, aspect);
    glm::vec3 hitPos;

    if (m_terrain.raycast(m_camera.getPosition(), rayDir, hitPos)) {
        m_position = hitPos;
        m_hasValidPosition = true;
    } else {
        m_hasValidPosition = false;
    }
}

void TerrainBrushTool::apply(float deltaTime) {
    if (!m_hasValidPosition) {
        return;
    }

    // Grab mode is handled separately via beginGrab/updateGrab/endGrab
    if (m_mode == BrushMode::Grab) {
        return;
    }

    float scaledStrength = m_strength * deltaTime;

    switch (m_mode) {
        case BrushMode::Paint:
            m_terrain.applyColorBrush(
                m_position.x, m_position.z,
                m_radius, scaledStrength, m_falloff,
                m_paintColor, m_shapeParams
            );
            break;

        case BrushMode::Texture:
            m_terrain.applyTextureBrush(
                m_position.x, m_position.z,
                m_radius, scaledStrength, m_falloff,
                m_textureIndex,
                m_texHue, m_texSaturation, m_texBrightness,
                m_shapeParams
            );
            break;

        case BrushMode::Select:
            m_terrain.applySelectionBrush(
                m_position.x, m_position.z,
                m_radius, scaledStrength, m_falloff,
                true, m_shapeParams  // select = true
            );
            break;

        case BrushMode::Deselect:
            m_terrain.applySelectionBrush(
                m_position.x, m_position.z,
                m_radius, scaledStrength, m_falloff,
                false, m_shapeParams  // select = false
            );
            break;

        case BrushMode::Terrace:
            m_terrain.applyBrush(
                m_position.x, m_position.z,
                m_radius, m_strength, m_falloff,
                m_mode, m_shapeParams
            );
            break;

        case BrushMode::FlattenToY:
            m_terrain.applyBrush(
                m_position.x, m_position.z,
                m_radius, m_strength, m_falloff,
                m_mode, m_shapeParams, m_targetElevation
            );
            break;

        default:
            m_terrain.applyBrush(
                m_position.x, m_position.z,
                m_radius, scaledStrength, m_falloff,
                m_mode, m_shapeParams
            );
            break;
    }
}

void TerrainBrushTool::beginGrab() {
    if (!m_hasValidPosition || m_isGrabbing) {
        return;
    }

    m_grabbedVertices.clear();
    m_grabStartPos = m_position;
    m_isGrabbing = true;

    // Find all vertices within brush shape and store them
    float worldX = m_position.x;
    float worldZ = m_position.z;

    // Calculate max radius for chunk overlap check
    float maxRadius = m_radius * (m_shapeParams.shape == BrushShape::Ellipse ?
                                  std::max(1.0f, 1.0f / m_shapeParams.aspectRatio) : 1.5f);

    // Get all visible chunks and check which vertices are in range
    for (auto& vc : m_terrain.getVisibleChunks()) {
        auto& chunk = vc.chunk;
        glm::vec3 chunkPos = chunk->getWorldPosition();
        float chunkSize = chunk->getChunkWorldSize();

        // Check if brush overlaps this chunk
        if (worldX + maxRadius < chunkPos.x || worldX - maxRadius > chunkPos.x + chunkSize ||
            worldZ + maxRadius < chunkPos.z || worldZ - maxRadius > chunkPos.z + chunkSize) {
            continue;
        }

        int resolution = chunk->getResolution();
        float tileSize = chunk->getTileSize();

        // Check each vertex in the chunk
        for (int z = 0; z < resolution; z++) {
            for (int x = 0; x < resolution; x++) {
                float vertexWorldX = chunkPos.x + x * tileSize;
                float vertexWorldZ = chunkPos.z + z * tileSize;

                float dx = vertexWorldX - worldX;
                float dz = vertexWorldZ - worldZ;
                float t = m_shapeParams.getNormalizedDistance(dx, dz, m_radius);

                if (t <= 1.0f) {
                    // Calculate falloff weight
                    float weight = 1.0f - std::pow(t, 1.0f / (1.0f - m_falloff * 0.9f + 0.1f));

                    GrabbedVertex gv;
                    gv.chunk = chunk;
                    gv.localX = x;
                    gv.localZ = z;
                    gv.originalHeight = chunk->getHeightAtLocal(x, z);
                    gv.weight = weight;

                    m_grabbedVertices.push_back(gv);
                }
            }
        }
    }
}

void TerrainBrushTool::updateGrab(float deltaY) {
    if (!m_isGrabbing) {
        return;
    }

    // Move all grabbed vertices based on deltaY and their weight
    for (auto& gv : m_grabbedVertices) {
        float newHeight = gv.originalHeight + deltaY * gv.weight * m_strength;
        gv.chunk->setHeightAtLocal(gv.localX, gv.localZ, newHeight);
    }

    // Mark chunks as needing mesh rebuild
    for (auto& gv : m_grabbedVertices) {
        gv.chunk->regenerateMesh();
    }
}

void TerrainBrushTool::endGrab() {
    m_isGrabbing = false;
    m_grabbedVertices.clear();
}

} // namespace eden
