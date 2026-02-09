#include <eden/Terrain.hpp>
#include "Noise.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iomanip>

namespace eden {

TerrainChunk::TerrainChunk(glm::ivec2 coord, const TerrainConfig& config)
    : m_coord(coord)
    , m_resolution(config.chunkResolution)
    , m_tileSize(config.tileSize)
    , m_heightScale(config.heightScale)
{
    m_chunkWorldSize = (config.chunkResolution - 1) * config.tileSize;
    generate(config);
}

glm::vec3 TerrainChunk::getWorldPosition() const {
    return glm::vec3(m_coord.x * m_chunkWorldSize, 0, m_coord.y * m_chunkWorldSize);
}

void TerrainChunk::generate(const TerrainConfig& config) {
    int resolution = config.chunkResolution;
    float tileSize = config.tileSize;
    float worldOffsetX = m_coord.x * (resolution - 1) * tileSize;
    float worldOffsetZ = m_coord.y * (resolution - 1) * tileSize;

    // Calculate world bounds for edge blending
    float chunkWorldSize = (resolution - 1) * tileSize;
    float worldMinX = config.minChunk.x * chunkWorldSize;
    float worldMinZ = config.minChunk.y * chunkWorldSize;
    float worldMaxX = (config.maxChunk.x + 1) * chunkWorldSize;
    float worldMaxZ = (config.maxChunk.y + 1) * chunkWorldSize;
    float worldSizeX = worldMaxX - worldMinX;
    float worldSizeZ = worldMaxZ - worldMinZ;

    // Blend zone is 10% of world size on each edge
    float blendZone = std::min(worldSizeX, worldSizeZ) * 0.1f;

    // Generate and store heights
    m_heightmap.resize(resolution * resolution);
    m_colormap.resize(resolution * resolution, glm::vec3(-1.0f));  // -1 = use height-based color
    m_paintAlphamap.resize(resolution * resolution, 0.0f);  // No paint by default
    m_texWeightmap.resize(resolution * resolution, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));  // Default to texture 0
    m_texIndicesmap.resize(resolution * resolution, glm::uvec4(0, 1, 2, 3));  // Default texture indices 0,1,2,3
    m_selectionmap.resize(resolution * resolution, 0.0f);  // No selection by default
    m_texHSBmap.resize(resolution * resolution, glm::vec3(0.0f, 1.0f, 1.0f));  // Default: no hue shift, normal sat/bright
    for (int z = 0; z < resolution; z++) {
        for (int x = 0; x < resolution; x++) {
            float worldX = worldOffsetX + x * tileSize;
            float worldZ = worldOffsetZ + z * tileSize;

            // Start with flat terrain - use Ridged brush to paint spiky areas
            float height = 0.0f;

            // Edge flattening for seamless wrapping
            if (config.wrapWorld && config.useFixedBounds) {
                // Calculate distance from nearest edge
                float distFromMinX = worldX - worldMinX;
                float distFromMaxX = worldMaxX - worldX;
                float distFromMinZ = worldZ - worldMinZ;
                float distFromMaxZ = worldMaxZ - worldZ;

                float nearestEdgeDist = std::min({distFromMinX, distFromMaxX, distFromMinZ, distFromMaxZ});

                // Flatten to a consistent height near edges
                if (nearestEdgeDist < blendZone) {
                    // Target height is moderate (30% of max) - creates gentle plains at edges
                    float flatHeight = config.heightScale * 0.3f;

                    // Blend factor: 0 at edge (fully flat), 1 at blend boundary (normal terrain)
                    float blend = nearestEdgeDist / blendZone;
                    blend = blend * blend * (3.0f - 2.0f * blend); // Smoothstep for smooth transition

                    height = glm::mix(flatHeight, height, blend);
                }
            }

            m_heightmap[z * resolution + x] = height;
        }
    }

    rebuildVerticesFromHeightmap();
}

void TerrainChunk::rebuildVerticesFromHeightmap() {
    float worldOffsetX = m_coord.x * (m_resolution - 1) * m_tileSize;
    float worldOffsetZ = m_coord.y * (m_resolution - 1) * m_tileSize;

    // Generate vertices with normals
    m_vertices.resize(m_resolution * m_resolution);
    for (int z = 0; z < m_resolution; z++) {
        for (int x = 0; x < m_resolution; x++) {
            int idx = z * m_resolution + x;
            float height = m_heightmap[idx];

            Vertex3D& vertex = m_vertices[idx];
            vertex.position = glm::vec3(
                worldOffsetX + x * m_tileSize,
                height,
                worldOffsetZ + z * m_tileSize
            );
            // Use custom color if painted, otherwise height-based
            if (m_colormap[idx].x >= 0.0f) {
                vertex.color = m_colormap[idx];
            } else {
                vertex.color = getTerrainColor(height / m_heightScale);
            }
            vertex.normal = calculateNormal(x, z);

            // UV coordinates based on world position (1 UV unit = 10 world units for tiling)
            float worldX = worldOffsetX + x * m_tileSize;
            float worldZ = worldOffsetZ + z * m_tileSize;
            vertex.uv = glm::vec2(worldX / 10.0f, worldZ / 10.0f);

            // Texture weights from splatmap
            vertex.texWeights = m_texWeightmap[idx];

            // Texture indices (which 4 textures this vertex blends)
            vertex.texIndices = m_texIndicesmap[idx];

            // Selection weight
            vertex.selection = m_selectionmap[idx];

            // Paint intensity
            vertex.paintAlpha = m_paintAlphamap[idx];

            // Texture color adjustment (HSB)
            vertex.texHSB = m_texHSBmap[idx];
        }
    }

    // Generate indices for triangle strip (only if not already generated)
    if (m_indices.empty()) {
        m_indices.reserve((m_resolution - 1) * (m_resolution - 1) * 6);

        for (int z = 0; z < m_resolution - 1; z++) {
            for (int x = 0; x < m_resolution - 1; x++) {
                int topLeft = z * m_resolution + x;
                int topRight = topLeft + 1;
                int bottomLeft = (z + 1) * m_resolution + x;
                int bottomRight = bottomLeft + 1;

                // First triangle
                m_indices.push_back(topLeft);
                m_indices.push_back(bottomLeft);
                m_indices.push_back(topRight);

                // Second triangle
                m_indices.push_back(topRight);
                m_indices.push_back(bottomLeft);
                m_indices.push_back(bottomRight);
            }
        }
    }

    m_needsUpload = true;
}

void TerrainChunk::regenerateMesh() {
    rebuildVerticesFromHeightmap();
}

float TerrainChunk::getHeightAtLocal(int x, int z) const {
    if (x < 0 || x >= m_resolution || z < 0 || z >= m_resolution) {
        return 0.0f;
    }
    return m_heightmap[z * m_resolution + x];
}

void TerrainChunk::setHeightAtLocal(int x, int z, float height) {
    if (x < 0 || x >= m_resolution || z < 0 || z >= m_resolution) {
        return;
    }
    m_heightmap[z * m_resolution + x] = height;
}

bool TerrainChunk::containsWorldPos(float worldX, float worldZ) const {
    glm::vec3 pos = getWorldPosition();
    return worldX >= pos.x && worldX < pos.x + m_chunkWorldSize &&
           worldZ >= pos.z && worldZ < pos.z + m_chunkWorldSize;
}

void TerrainChunk::setChunkData(const std::vector<float>& heightmap,
                                 const std::vector<glm::vec3>& colormap,
                                 const std::vector<float>& paintAlphamap,
                                 const std::vector<glm::vec4>& texWeightmap,
                                 const std::vector<glm::uvec4>& texIndicesmap,
                                 const std::vector<glm::vec3>& texHSBmap) {
    size_t expected = static_cast<size_t>(m_resolution * m_resolution);

    if (heightmap.size() == expected) m_heightmap = heightmap;
    if (colormap.size() == expected) m_colormap = colormap;
    if (paintAlphamap.size() == expected) m_paintAlphamap = paintAlphamap;
    if (texWeightmap.size() == expected) m_texWeightmap = texWeightmap;
    if (texIndicesmap.size() == expected) m_texIndicesmap = texIndicesmap;
    if (texHSBmap.size() == expected) m_texHSBmap = texHSBmap;

    // Rebuild mesh from loaded data
    rebuildVerticesFromHeightmap();
    m_needsUpload = true;
}

void TerrainChunk::applyBrush(float worldX, float worldZ, float radius, float strength, float falloff, BrushMode mode,
                              float targetHeight, HeightLookup heightLookup, const BrushShapeParams& shapeParams) {
    glm::vec3 chunkPos = getWorldPosition();

    // Calculate local position within chunk
    float localX = worldX - chunkPos.x;
    float localZ = worldZ - chunkPos.z;

    // Calculate affected vertex range (use larger range for rotated shapes)
    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    int minX = std::max(0, static_cast<int>((localX - maxRadius) / m_tileSize));
    int maxX = std::min(m_resolution - 1, static_cast<int>((localX + maxRadius) / m_tileSize) + 1);
    int minZ = std::max(0, static_cast<int>((localZ - maxRadius) / m_tileSize));
    int maxZ = std::min(m_resolution - 1, static_cast<int>((localZ + maxRadius) / m_tileSize) + 1);

    bool modified = false;

    for (int z = minZ; z <= maxZ; z++) {
        for (int x = minX; x <= maxX; x++) {
            float vertexWorldX = chunkPos.x + x * m_tileSize;
            float vertexWorldZ = chunkPos.z + z * m_tileSize;

            float dx = vertexWorldX - worldX;
            float dz = vertexWorldZ - worldZ;

            // Use shape-aware distance calculation
            float t = shapeParams.getNormalizedDistance(dx, dz, radius);

            // Trench brush uses square bounds
            if (mode == BrushMode::Trench) {
                if (t <= 1.0f) {
                    float currentHeight = m_heightmap[z * m_resolution + x];
                    // Lower to target height (flat bottom) - targetHeight is calculated from center
                    float newHeight = std::min(currentHeight, targetHeight - strength);
                    m_heightmap[z * m_resolution + x] = newHeight;
                    modified = true;
                }
                continue;
            }

            if (t <= 1.0f) {
                // Calculate falloff - smoother curve
                float falloffMult = 1.0f - std::pow(t, 1.0f / (1.0f - falloff * 0.9f + 0.1f));

                float currentHeight = m_heightmap[z * m_resolution + x];
                float newHeight = currentHeight;

                switch (mode) {
                    case BrushMode::Raise:
                        newHeight += strength * falloffMult;
                        break;
                    case BrushMode::Lower:
                        newHeight -= strength * falloffMult;
                        break;
                    case BrushMode::Smooth: {
                        // Use world-space height lookup to average across chunk boundaries
                        float avg = 0.0f;
                        int count = 0;
                        int range = 2;  // Look at 5x5 neighborhood in world space
                        for (int nz = -range; nz <= range; nz++) {
                            for (int nx = -range; nx <= range; nx++) {
                                float sampleX = vertexWorldX + nx * m_tileSize;
                                float sampleZ = vertexWorldZ + nz * m_tileSize;
                                if (heightLookup) {
                                    avg += heightLookup(sampleX, sampleZ);
                                } else {
                                    // Fallback to local lookup
                                    int lx = x + nx;
                                    int lz = z + nz;
                                    if (lx >= 0 && lx < m_resolution && lz >= 0 && lz < m_resolution) {
                                        avg += m_heightmap[lz * m_resolution + lx];
                                    } else {
                                        avg += currentHeight;
                                    }
                                }
                                count++;
                            }
                        }
                        avg /= count;
                        newHeight = currentHeight + (avg - currentHeight) * strength * 0.15f * falloffMult;
                        break;
                    }
                    case BrushMode::Flatten:
                        // Use the target height passed from Terrain class (consistent across chunks)
                        newHeight = currentHeight + (targetHeight - currentHeight) * strength * 0.05f * falloffMult;
                        break;
                    case BrushMode::Crack: {
                        // Sharp V-shaped cut - very steep walls, deep center
                        // Use exponential falloff for sharp edges
                        float sharpT = 1.0f - std::pow(1.0f - t, 4.0f);  // Inverted sharp curve
                        float crackDepth = (1.0f - sharpT) * strength * 2.0f;  // Deep in center
                        newHeight -= crackDepth;
                        break;
                    }
                    case BrushMode::Plateau:
                        // Set all vertices within radius to exact target height (flat top)
                        // No falloff - creates a hard-edged cylinder shape
                        newHeight = targetHeight;
                        break;
                    case BrushMode::LevelMin:
                        // Set all vertices to the minimum height found in the circle
                        // Creates sharp cliffs by cutting down to lowest point
                        newHeight = targetHeight;
                        break;
                    case BrushMode::Spire: {
                        // Creates a cone/spike shape - sharp peak at center
                        // Linear falloff from center creates a pointed spire
                        float spireT = 1.0f - t;  // 1 at center, 0 at edge
                        // Apply a power curve to make the peak sharper
                        float spireShape = std::pow(spireT, 0.5f);  // Square root makes it pointier
                        newHeight += strength * spireShape;
                        break;
                    }
                    case BrushMode::Ridged: {
                        // Creates spiky ridged terrain using ridged multifractal noise
                        float ridgedHeight = 0.0f;
                        float amplitude = 1.0f;
                        float frequency = 0.1f;  // Base frequency for spikes
                        float maxValue = 0.0f;

                        for (int oct = 0; oct < 8; oct++) {
                            float noiseVal = Noise::perlin(vertexWorldX * frequency, vertexWorldZ * frequency);
                            float ridged = 1.0f - std::abs(noiseVal);
                            ridged = ridged * ridged * ridged;  // Cube for sharp peaks

                            ridgedHeight += ridged * amplitude;
                            maxValue += amplitude;

                            amplitude *= 0.6f;
                            frequency *= 2.2f;
                        }

                        ridgedHeight = (ridgedHeight / maxValue);  // Normalized 0-1
                        // Apply brush falloff and strength
                        float brushEffect = (1.0f - t) * strength * ridgedHeight;
                        newHeight += brushEffect;
                        break;
                    }
                    case BrushMode::Trench:
                        // Handled in square section below
                        break;
                    case BrushMode::Paint:
                    case BrushMode::Texture:
                        // Handled separately
                        break;
                }

                m_heightmap[z * m_resolution + x] = newHeight;
                modified = true;
            }
        }
    }

    if (modified) {
        m_needsUpload = true;
    }
}

void TerrainChunk::applyColorBrush(float worldX, float worldZ, float radius, float strength, float falloff, const glm::vec3& color,
                                   const BrushShapeParams& shapeParams) {
    glm::vec3 chunkPos = getWorldPosition();

    float localX = worldX - chunkPos.x;
    float localZ = worldZ - chunkPos.z;

    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    int minX = std::max(0, static_cast<int>((localX - maxRadius) / m_tileSize));
    int maxX = std::min(m_resolution - 1, static_cast<int>((localX + maxRadius) / m_tileSize) + 1);
    int minZ = std::max(0, static_cast<int>((localZ - maxRadius) / m_tileSize));
    int maxZ = std::min(m_resolution - 1, static_cast<int>((localZ + maxRadius) / m_tileSize) + 1);

    bool modified = false;

    for (int z = minZ; z <= maxZ; z++) {
        for (int x = minX; x <= maxX; x++) {
            float vertexWorldX = chunkPos.x + x * m_tileSize;
            float vertexWorldZ = chunkPos.z + z * m_tileSize;

            float dx = vertexWorldX - worldX;
            float dz = vertexWorldZ - worldZ;
            float t = shapeParams.getNormalizedDistance(dx, dz, radius);

            if (t <= 1.0f) {
                float falloffMult = 1.0f - std::pow(t, 1.0f / (1.0f - falloff * 0.9f + 0.1f));

                int idx = z * m_resolution + x;

                // Blend the color
                glm::vec3 currentColor = m_colormap[idx].x >= 0.0f ? m_colormap[idx] : color;
                float blendFactor = strength * 0.1f * falloffMult;
                m_colormap[idx] = glm::mix(currentColor, color, blendFactor);

                // Increase paint alpha (how much color shows through texture)
                float currentAlpha = m_paintAlphamap[idx];
                m_paintAlphamap[idx] = std::min(1.0f, currentAlpha + blendFactor);

                modified = true;
            }
        }
    }

    if (modified) {
        m_needsUpload = true;
    }
}

void TerrainChunk::applyTextureBrush(float worldX, float worldZ, float radius, float strength, float falloff, int textureIndex,
                                     float hue, float saturation, float brightness, const BrushShapeParams& shapeParams) {
    if (textureIndex < 0) return;

    glm::vec3 chunkPos = getWorldPosition();

    float localX = worldX - chunkPos.x;
    float localZ = worldZ - chunkPos.z;

    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    int minX = std::max(0, static_cast<int>((localX - maxRadius) / m_tileSize));
    int maxX = std::min(m_resolution - 1, static_cast<int>((localX + maxRadius) / m_tileSize) + 1);
    int minZ = std::max(0, static_cast<int>((localZ - maxRadius) / m_tileSize));
    int maxZ = std::min(m_resolution - 1, static_cast<int>((localZ + maxRadius) / m_tileSize) + 1);

    bool modified = false;

    for (int z = minZ; z <= maxZ; z++) {
        for (int x = minX; x <= maxX; x++) {
            float vertexWorldX = chunkPos.x + x * m_tileSize;
            float vertexWorldZ = chunkPos.z + z * m_tileSize;

            float dx = vertexWorldX - worldX;
            float dz = vertexWorldZ - worldZ;
            float t = shapeParams.getNormalizedDistance(dx, dz, radius);

            if (t <= 1.0f) {
                float falloffMult = 1.0f - std::pow(t, 1.0f / (1.0f - falloff * 0.9f + 0.1f));

                int idx = z * m_resolution + x;
                glm::vec4& weights = m_texWeightmap[idx];
                glm::uvec4& indices = m_texIndicesmap[idx];

                // Find if this texture index is already in our 4 slots
                int slot = -1;
                for (int i = 0; i < 4; i++) {
                    if (indices[i] == static_cast<unsigned int>(textureIndex)) {
                        slot = i;
                        break;
                    }
                }

                // If not found, replace the slot with lowest weight
                if (slot == -1) {
                    float minWeight = weights[0];
                    slot = 0;
                    for (int i = 1; i < 4; i++) {
                        if (weights[i] < minWeight) {
                            minWeight = weights[i];
                            slot = i;
                        }
                    }
                    // Replace that slot with the new texture
                    indices[slot] = static_cast<unsigned int>(textureIndex);
                    weights[slot] = 0.0f;  // Start fresh
                }

                // Increase selected texture weight
                float addAmount = strength * 0.1f * falloffMult;
                weights[slot] += addAmount;

                // Normalize weights so they sum to 1
                float sum = weights.x + weights.y + weights.z + weights.w;
                if (sum > 0.0f) {
                    weights /= sum;
                }

                // Blend HSB values towards the brush settings
                glm::vec3& hsb = m_texHSBmap[idx];
                glm::vec3 targetHSB(hue, saturation, brightness);
                float blendFactor = addAmount;  // Same as texture weight change
                hsb = glm::mix(hsb, targetHSB, blendFactor);

                modified = true;
            }
        }
    }

    if (modified) {
        m_needsUpload = true;
    }
}

void TerrainChunk::applySelectionBrush(float worldX, float worldZ, float radius, float strength, float falloff, bool select,
                                       const BrushShapeParams& shapeParams) {
    glm::vec3 chunkPos = getWorldPosition();

    float localX = worldX - chunkPos.x;
    float localZ = worldZ - chunkPos.z;

    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    int minX = std::max(0, static_cast<int>((localX - maxRadius) / m_tileSize));
    int maxX = std::min(m_resolution - 1, static_cast<int>((localX + maxRadius) / m_tileSize) + 1);
    int minZ = std::max(0, static_cast<int>((localZ - maxRadius) / m_tileSize));
    int maxZ = std::min(m_resolution - 1, static_cast<int>((localZ + maxRadius) / m_tileSize) + 1);

    bool modified = false;

    for (int z = minZ; z <= maxZ; z++) {
        for (int x = minX; x <= maxX; x++) {
            float vertexWorldX = chunkPos.x + x * m_tileSize;
            float vertexWorldZ = chunkPos.z + z * m_tileSize;

            float dx = vertexWorldX - worldX;
            float dz = vertexWorldZ - worldZ;
            float t = shapeParams.getNormalizedDistance(dx, dz, radius);

            if (t <= 1.0f) {
                float falloffMult = 1.0f - std::pow(t, 1.0f / (1.0f - falloff * 0.9f + 0.1f));

                int idx = z * m_resolution + x;
                float current = m_selectionmap[idx];

                if (select) {
                    // Add selection with falloff
                    float addAmount = strength * 0.2f * falloffMult;
                    m_selectionmap[idx] = std::min(1.0f, current + addAmount);
                } else {
                    // Remove selection with falloff
                    float removeAmount = strength * 0.2f * falloffMult;
                    m_selectionmap[idx] = std::max(0.0f, current - removeAmount);
                }

                modified = true;
            }
        }
    }

    if (modified) {
        m_needsUpload = true;
    }
}

void TerrainChunk::clearSelection() {
    bool hadSelection = false;
    for (size_t i = 0; i < m_selectionmap.size(); i++) {
        if (m_selectionmap[i] > 0.0f) {
            m_selectionmap[i] = 0.0f;
            hadSelection = true;
        }
    }
    if (hadSelection) {
        m_needsUpload = true;
    }
}

float TerrainChunk::getSelectionAtLocal(int x, int z) const {
    if (x < 0 || x >= m_resolution || z < 0 || z >= m_resolution) {
        return 0.0f;
    }
    return m_selectionmap[z * m_resolution + x];
}

void TerrainChunk::setSelectionAtLocal(int x, int z, float value) {
    if (x < 0 || x >= m_resolution || z < 0 || z >= m_resolution) {
        return;
    }
    m_selectionmap[z * m_resolution + x] = std::clamp(value, 0.0f, 1.0f);
}

bool TerrainChunk::hasSelection() const {
    for (float sel : m_selectionmap) {
        if (sel > 0.0f) {
            return true;
        }
    }
    return false;
}

glm::vec3 TerrainChunk::getTerrainColor(float normalizedHeight) {
    // Water
    if (normalizedHeight < 0.3f) {
        float t = normalizedHeight / 0.3f;
        return glm::mix(glm::vec3(0.1f, 0.2f, 0.6f), glm::vec3(0.2f, 0.4f, 0.7f), t);
    }
    // Sand/Beach
    if (normalizedHeight < 0.35f) {
        return glm::vec3(0.76f, 0.7f, 0.5f);
    }
    // Grass
    if (normalizedHeight < 0.55f) {
        float t = (normalizedHeight - 0.35f) / 0.2f;
        return glm::mix(glm::vec3(0.2f, 0.5f, 0.15f), glm::vec3(0.15f, 0.4f, 0.1f), t);
    }
    // Rock
    if (normalizedHeight < 0.75f) {
        float t = (normalizedHeight - 0.55f) / 0.2f;
        return glm::mix(glm::vec3(0.4f, 0.35f, 0.3f), glm::vec3(0.5f, 0.45f, 0.4f), t);
    }
    // Snow
    float t = std::min((normalizedHeight - 0.75f) / 0.25f, 1.0f);
    return glm::mix(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.95f, 0.95f, 0.98f), t);
}

glm::vec3 TerrainChunk::calculateNormal(int x, int z) {
    float hL = getHeightAtLocal(x - 1, z);
    float hR = getHeightAtLocal(x + 1, z);
    float hD = getHeightAtLocal(x, z - 1);
    float hU = getHeightAtLocal(x, z + 1);

    // Use current height for edges
    if (x == 0) hL = getHeightAtLocal(x, z);
    if (x == m_resolution - 1) hR = getHeightAtLocal(x, z);
    if (z == 0) hD = getHeightAtLocal(x, z);
    if (z == m_resolution - 1) hU = getHeightAtLocal(x, z);

    return glm::normalize(glm::vec3(hL - hR, 2.0f * m_tileSize, hD - hU));
}

// Terrain class

Terrain::Terrain(const TerrainConfig& config)
    : m_config(config)
{
}

int Terrain::getTotalChunkCount() const {
    if (!m_config.useFixedBounds) {
        return -1;  // Infinite terrain
    }
    int width = m_config.maxChunk.x - m_config.minChunk.x + 1;
    int height = m_config.maxChunk.y - m_config.minChunk.y + 1;
    return width * height;
}

int Terrain::preloadAllChunks(TerrainLoadCallback progressCallback) {
    if (!m_config.useFixedBounds) {
        return 0;  // Can't preload infinite terrain
    }

    int totalChunks = getTotalChunkCount();
    int loaded = 0;

    for (int z = m_config.minChunk.y; z <= m_config.maxChunk.y; z++) {
        for (int x = m_config.minChunk.x; x <= m_config.maxChunk.x; x++) {
            glm::ivec2 coord{x, z};

            if (m_chunks.find(coord) == m_chunks.end()) {
                auto chunk = std::make_shared<TerrainChunk>(coord, m_config);
                m_chunks[coord] = chunk;
            }

            loaded++;
            if (progressCallback) {
                progressCallback(loaded, totalChunks);
            }
        }
    }

    m_fullyLoaded = true;
    return loaded;
}

void Terrain::update(const glm::vec3& cameraPosition) {
    // Wrap camera position if world wrapping is enabled
    glm::vec3 wrappedCamPos = wrapWorldPosition(cameraPosition);
    glm::ivec2 currentChunk = worldToChunkCoord(wrappedCamPos);

    // Only update if camera moved to a new chunk
    if (currentChunk == m_lastCameraChunk) {
        return;
    }
    m_lastCameraChunk = currentChunk;

    m_visibleChunks.clear();

    float chunkWorldSize = (m_config.chunkResolution - 1) * m_config.tileSize;
    glm::vec2 worldSize = getWorldSize();

    int viewDist = m_config.viewDistance;
    for (int z = -viewDist; z <= viewDist; z++) {
        for (int x = -viewDist; x <= viewDist; x++) {
            glm::ivec2 desiredCoord = currentChunk + glm::ivec2(x, z);
            glm::ivec2 actualCoord = desiredCoord;
            glm::vec3 renderOffset{0};

            // If world wrapping is enabled, wrap the chunk coordinate
            if (m_config.wrapWorld && m_config.useFixedBounds) {
                actualCoord = wrapChunkCoord(desiredCoord);

                // Calculate render offset - where should this chunk appear?
                // The chunk's actual position is at actualCoord, but we want it at desiredCoord
                float offsetX = (desiredCoord.x - actualCoord.x) * chunkWorldSize;
                float offsetZ = (desiredCoord.y - actualCoord.y) * chunkWorldSize;
                renderOffset = glm::vec3(offsetX, 0, offsetZ);
            }
            // If using fixed bounds without wrapping, skip out-of-bounds chunks
            else if (m_config.useFixedBounds) {
                if (desiredCoord.x < m_config.minChunk.x || desiredCoord.x > m_config.maxChunk.x ||
                    desiredCoord.y < m_config.minChunk.y || desiredCoord.y > m_config.maxChunk.y) {
                    continue;  // Skip chunks outside bounds
                }
            }

            auto it = m_chunks.find(actualCoord);
            if (it == m_chunks.end()) {
                // For fixed bounds with preloading, chunk should already exist
                // Only create dynamically if not using fixed bounds
                if (!m_config.useFixedBounds) {
                    auto chunk = std::make_shared<TerrainChunk>(actualCoord, m_config);
                    m_chunks[actualCoord] = chunk;
                    m_visibleChunks.push_back({chunk, renderOffset});
                }
            } else {
                m_visibleChunks.push_back({it->second, renderOffset});
            }
        }
    }
}

glm::ivec2 Terrain::worldToChunkCoord(const glm::vec3& worldPos) const {
    float chunkWorldSize = (m_config.chunkResolution - 1) * m_config.tileSize;
    return glm::ivec2(
        static_cast<int>(std::floor(worldPos.x / chunkWorldSize)),
        static_cast<int>(std::floor(worldPos.z / chunkWorldSize))
    );
}

glm::ivec2 Terrain::wrapChunkCoord(glm::ivec2 coord) const {
    if (!m_config.wrapWorld || !m_config.useFixedBounds) {
        return coord;
    }

    int width = m_config.maxChunk.x - m_config.minChunk.x + 1;
    int height = m_config.maxChunk.y - m_config.minChunk.y + 1;

    // Wrap X
    int x = coord.x - m_config.minChunk.x;
    x = ((x % width) + width) % width;  // Proper modulo for negatives
    coord.x = x + m_config.minChunk.x;

    // Wrap Z
    int z = coord.y - m_config.minChunk.y;
    z = ((z % height) + height) % height;
    coord.y = z + m_config.minChunk.y;

    return coord;
}

glm::vec2 Terrain::getWorldSize() const {
    if (!m_config.useFixedBounds) {
        return glm::vec2(0);  // Infinite
    }
    float chunkWorldSize = (m_config.chunkResolution - 1) * m_config.tileSize;
    int width = m_config.maxChunk.x - m_config.minChunk.x + 1;
    int height = m_config.maxChunk.y - m_config.minChunk.y + 1;
    return glm::vec2(width * chunkWorldSize, height * chunkWorldSize);
}

glm::vec3 Terrain::wrapWorldPosition(const glm::vec3& pos) const {
    if (!m_config.wrapWorld || !m_config.useFixedBounds) {
        return pos;
    }

    float chunkWorldSize = (m_config.chunkResolution - 1) * m_config.tileSize;
    float minX = m_config.minChunk.x * chunkWorldSize;
    float minZ = m_config.minChunk.y * chunkWorldSize;
    glm::vec2 worldSize = getWorldSize();

    glm::vec3 wrapped = pos;

    // Wrap X
    wrapped.x = wrapped.x - minX;
    wrapped.x = std::fmod(std::fmod(wrapped.x, worldSize.x) + worldSize.x, worldSize.x);
    wrapped.x = wrapped.x + minX;

    // Wrap Z
    wrapped.z = wrapped.z - minZ;
    wrapped.z = std::fmod(std::fmod(wrapped.z, worldSize.y) + worldSize.y, worldSize.y);
    wrapped.z = wrapped.z + minZ;

    return wrapped;
}

float Terrain::getHeightAt(float worldX, float worldZ) const {
    // Wrap coordinates if world wrapping is enabled
    if (m_config.wrapWorld && m_config.useFixedBounds) {
        glm::vec3 wrapped = wrapWorldPosition(glm::vec3(worldX, 0, worldZ));
        worldX = wrapped.x;
        worldZ = wrapped.z;
    }

    // Try to get from loaded chunk first
    glm::ivec2 chunkCoord = worldToChunkCoord(glm::vec3(worldX, 0, worldZ));
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        auto& chunk = it->second;
        glm::vec3 chunkPos = chunk->getWorldPosition();
        int localX = static_cast<int>((worldX - chunkPos.x) / chunk->getTileSize());
        int localZ = static_cast<int>((worldZ - chunkPos.z) / chunk->getTileSize());
        return chunk->getHeightAtLocal(localX, localZ);
    }

    // Fall back to noise
    float height = Noise::fbmNormalized(
        worldX * m_config.noiseScale,
        worldZ * m_config.noiseScale,
        m_config.noiseOctaves,
        m_config.noisePersistence
    );
    return height * m_config.heightScale;
}

bool Terrain::raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir, glm::vec3& outHitPos) const {
    // Simple raymarching - step along ray and check height
    const float maxDistance = 1000.0f;
    const float stepSize = 1.0f;

    glm::vec3 pos = rayOrigin;
    glm::vec3 dir = glm::normalize(rayDir);

    for (float t = 0; t < maxDistance; t += stepSize) {
        pos = rayOrigin + dir * t;

        float terrainHeight = getHeightAt(pos.x, pos.z);

        if (pos.y <= terrainHeight) {
            // Binary search for more precise hit point
            float lo = t - stepSize;
            float hi = t;
            for (int i = 0; i < 8; i++) {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 midPos = rayOrigin + dir * mid;
                float midHeight = getHeightAt(midPos.x, midPos.z);
                if (midPos.y <= midHeight) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            outHitPos = rayOrigin + dir * hi;
            outHitPos.y = getHeightAt(outHitPos.x, outHitPos.z);
            return true;
        }
    }

    return false;
}

void Terrain::applyBrush(float worldX, float worldZ, float radius, float strength, float falloff, BrushMode mode,
                         const BrushShapeParams& shapeParams) {
    // For flatten mode, get the target height ONCE at the brush center
    // This ensures all chunks flatten to the same height (no seams)
    float targetHeight = 0.0f;
    if (mode == BrushMode::Flatten) {
        targetHeight = getHeightAt(worldX, worldZ);
    } else if (mode == BrushMode::Plateau) {
        // Plateau mode: extrude the center point upward by strength amount
        targetHeight = getHeightAt(worldX, worldZ) + strength;
    } else if (mode == BrushMode::Trench) {
        // Trench mode: get center height as reference for flat bottom
        targetHeight = getHeightAt(worldX, worldZ);
    } else if (mode == BrushMode::LevelMin) {
        // Find the minimum height within the brush radius
        float minHeight = std::numeric_limits<float>::max();
        float sampleStep = 1.0f;  // Sample every 1 unit
        for (float sz = -radius; sz <= radius; sz += sampleStep) {
            for (float sx = -radius; sx <= radius; sx += sampleStep) {
                if (shapeParams.getNormalizedDistance(sx, sz, radius) <= 1.0f) {
                    float h = getHeightAt(worldX + sx, worldZ + sz);
                    if (h < minHeight) {
                        minHeight = h;
                    }
                }
            }
        }
        targetHeight = minHeight;
    }

    // For smooth mode, provide a height lookup function that works across chunks
    TerrainChunk::HeightLookup heightLookup = nullptr;
    if (mode == BrushMode::Smooth) {
        heightLookup = [this](float x, float z) {
            return getHeightAt(x, z);
        };
    }

    // Calculate max radius for chunk overlap check (account for ellipse/rotation)
    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);

    // Apply to all chunks that might be affected
    for (auto& vc : m_visibleChunks) {
        glm::vec3 chunkPos = vc.chunk->getWorldPosition() + vc.renderOffset;
        float chunkSize = vc.chunk->getChunkWorldSize();

        // Check if brush overlaps chunk bounds
        if (worldX + maxRadius >= chunkPos.x && worldX - maxRadius < chunkPos.x + chunkSize &&
            worldZ + maxRadius >= chunkPos.z && worldZ - maxRadius < chunkPos.z + chunkSize) {
            // Apply brush at the offset position
            float localX = worldX - vc.renderOffset.x;
            float localZ = worldZ - vc.renderOffset.z;
            vc.chunk->applyBrush(localX, localZ, radius, strength, falloff, mode, targetHeight, heightLookup, shapeParams);
        }
    }
}

void Terrain::applyColorBrush(float worldX, float worldZ, float radius, float strength, float falloff, const glm::vec3& color,
                              const BrushShapeParams& shapeParams) {
    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    for (auto& vc : m_visibleChunks) {
        glm::vec3 chunkPos = vc.chunk->getWorldPosition() + vc.renderOffset;
        float chunkSize = vc.chunk->getChunkWorldSize();

        if (worldX + maxRadius >= chunkPos.x && worldX - maxRadius < chunkPos.x + chunkSize &&
            worldZ + maxRadius >= chunkPos.z && worldZ - maxRadius < chunkPos.z + chunkSize) {
            float localX = worldX - vc.renderOffset.x;
            float localZ = worldZ - vc.renderOffset.z;
            vc.chunk->applyColorBrush(localX, localZ, radius, strength, falloff, color, shapeParams);
        }
    }
}

void Terrain::applyTextureBrush(float worldX, float worldZ, float radius, float strength, float falloff, int textureIndex,
                                float hue, float saturation, float brightness, const BrushShapeParams& shapeParams) {
    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    for (auto& vc : m_visibleChunks) {
        glm::vec3 chunkPos = vc.chunk->getWorldPosition() + vc.renderOffset;
        float chunkSize = vc.chunk->getChunkWorldSize();

        if (worldX + maxRadius >= chunkPos.x && worldX - maxRadius < chunkPos.x + chunkSize &&
            worldZ + maxRadius >= chunkPos.z && worldZ - maxRadius < chunkPos.z + chunkSize) {
            float localX = worldX - vc.renderOffset.x;
            float localZ = worldZ - vc.renderOffset.z;
            vc.chunk->applyTextureBrush(localX, localZ, radius, strength, falloff, textureIndex, hue, saturation, brightness, shapeParams);
        }
    }
}

std::shared_ptr<TerrainChunk> Terrain::getChunkAt(float worldX, float worldZ) {
    glm::ivec2 coord = worldToChunkCoord(glm::vec3(worldX, 0, worldZ));
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<TerrainChunk> Terrain::getChunkByCoord(glm::ivec2 coord) {
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return it->second;
    }
    return nullptr;
}

void Terrain::applySelectionBrush(float worldX, float worldZ, float radius, float strength, float falloff, bool select,
                                  const BrushShapeParams& shapeParams) {
    float maxRadius = radius * (shapeParams.shape == BrushShape::Ellipse ? std::max(1.0f, 1.0f / shapeParams.aspectRatio) : 1.5f);
    bool anyModified = false;
    for (auto& vc : m_visibleChunks) {
        glm::vec3 chunkPos = vc.chunk->getWorldPosition() + vc.renderOffset;
        float chunkSize = vc.chunk->getChunkWorldSize();

        if (worldX + maxRadius >= chunkPos.x && worldX - maxRadius < chunkPos.x + chunkSize &&
            worldZ + maxRadius >= chunkPos.z && worldZ - maxRadius < chunkPos.z + chunkSize) {
            float localX = worldX - vc.renderOffset.x;
            float localZ = worldZ - vc.renderOffset.z;
            vc.chunk->applySelectionBrush(localX, localZ, radius, strength, falloff, select, shapeParams);
            anyModified = true;
        }
    }
    if (anyModified) {
        updateSelectionCache();
    }
}

void Terrain::clearAllSelection() {
    for (auto& pair : m_chunks) {
        pair.second->clearSelection();
    }
    m_hasSelection = false;
    m_selectionCenter = glm::vec3{0};
}

void Terrain::updateSelectionCache() {
    glm::vec3 sum{0};
    float totalWeight = 0.0f;

    // Only check visible chunks for performance
    for (const auto& vc : m_visibleChunks) {
        auto& chunk = vc.chunk;
        if (!chunk->hasSelection()) continue;

        glm::vec3 chunkPos = chunk->getWorldPosition();
        int resolution = chunk->getResolution();
        float tileSize = chunk->getTileSize();

        for (int z = 0; z < resolution; z++) {
            for (int x = 0; x < resolution; x++) {
                float sel = chunk->getSelectionAtLocal(x, z);
                if (sel > 0.0f) {
                    float worldX = chunkPos.x + x * tileSize;
                    float worldZ = chunkPos.z + z * tileSize;
                    float height = chunk->getHeightAtLocal(x, z);

                    sum += glm::vec3(worldX, height, worldZ) * sel;
                    totalWeight += sel;
                }
            }
        }
    }

    m_hasSelection = (totalWeight > 0.0f);
    if (m_hasSelection) {
        m_selectionCenter = sum / totalWeight;
    } else {
        m_selectionCenter = glm::vec3{0};
    }
}

void Terrain::moveSelection(const glm::vec3& delta) {
    bool anyModified = false;
    for (auto& pair : m_chunks) {
        auto& chunk = pair.second;
        if (!chunk->hasSelection()) continue;

        int resolution = chunk->getResolution();
        bool modified = false;

        for (int z = 0; z < resolution; z++) {
            for (int x = 0; x < resolution; x++) {
                float sel = chunk->getSelectionAtLocal(x, z);
                if (sel > 0.0f) {
                    // Move vertex by delta * selection weight (for smooth falloff)
                    float currentHeight = chunk->getHeightAtLocal(x, z);
                    chunk->setHeightAtLocal(x, z, currentHeight + delta.y * sel);
                    modified = true;
                }
            }
        }

        if (modified) {
            chunk->regenerateMesh();
            anyModified = true;
        }
    }
    if (anyModified) {
        updateSelectionCache();
    }
}

void Terrain::tiltSelection(float tiltX, float tiltZ) {
    if (!m_hasSelection) return;

    // Convert degrees to slope (tan of angle)
    // For small angles, tan(angle) ≈ angle in radians
    float slopeX = std::tan(tiltX * 3.14159f / 180.0f);  // Tilt around X axis (height changes with Z)
    float slopeZ = std::tan(tiltZ * 3.14159f / 180.0f);  // Tilt around Z axis (height changes with X)

    glm::vec3 center = m_selectionCenter;

    bool anyModified = false;
    for (auto& pair : m_chunks) {
        auto& chunk = pair.second;
        if (!chunk->hasSelection()) continue;

        glm::vec3 chunkPos = chunk->getWorldPosition();
        int resolution = chunk->getResolution();
        float tileSize = chunk->getTileSize();
        bool modified = false;

        for (int z = 0; z < resolution; z++) {
            for (int x = 0; x < resolution; x++) {
                float sel = chunk->getSelectionAtLocal(x, z);
                if (sel > 0.0f) {
                    float worldX = chunkPos.x + x * tileSize;
                    float worldZ = chunkPos.z + z * tileSize;

                    // Calculate height change based on distance from center
                    float dz = worldZ - center.z;
                    float dx = worldX - center.x;

                    // Height change from tilting
                    float heightChange = (dz * slopeX + dx * slopeZ) * sel;

                    float currentHeight = chunk->getHeightAtLocal(x, z);
                    chunk->setHeightAtLocal(x, z, currentHeight + heightChange);
                    modified = true;
                }
            }
        }

        if (modified) {
            chunk->regenerateMesh();
            anyModified = true;
        }
    }
    if (anyModified) {
        updateSelectionCache();
    }
}

bool Terrain::exportToOBJ(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << "# EDEN Terrain Export\n";
    file << "# Chunks: " << m_chunks.size() << "\n\n";

    // We need to track global vertex index since OBJ indices are 1-based and global
    uint32_t globalVertexIndex = 1;

    // First pass: write all vertices
    for (const auto& [coord, chunk] : m_chunks) {
        if (!chunk) continue;

        glm::vec3 chunkOffset = chunk->getWorldPosition();
        const auto& vertices = chunk->getVertices();

        file << "# Chunk (" << coord.x << ", " << coord.y << ") - " << vertices.size() << " vertices\n";

        for (const auto& v : vertices) {
            glm::vec3 worldPos = v.position + chunkOffset;
            file << "v " << std::fixed << std::setprecision(4)
                 << worldPos.x << " " << worldPos.y << " " << worldPos.z << "\n";
        }
    }

    file << "\n# Normals\n";

    // Second pass: write all normals
    for (const auto& [coord, chunk] : m_chunks) {
        if (!chunk) continue;

        const auto& vertices = chunk->getVertices();
        for (const auto& v : vertices) {
            file << "vn " << std::fixed << std::setprecision(4)
                 << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
        }
    }

    file << "\n# Texture coordinates\n";

    // Third pass: write all UVs
    for (const auto& [coord, chunk] : m_chunks) {
        if (!chunk) continue;

        const auto& vertices = chunk->getVertices();
        for (const auto& v : vertices) {
            file << "vt " << std::fixed << std::setprecision(4)
                 << v.uv.x << " " << v.uv.y << "\n";
        }
    }

    file << "\n# Faces\n";

    // Fourth pass: write all faces with correct global indices
    globalVertexIndex = 1;
    for (const auto& [coord, chunk] : m_chunks) {
        if (!chunk) continue;

        const auto& indices = chunk->getIndices();
        uint32_t chunkBaseIndex = globalVertexIndex;

        file << "# Chunk (" << coord.x << ", " << coord.y << ") faces\n";
        file << "g chunk_" << coord.x << "_" << coord.y << "\n";

        // Process triangles (every 3 indices)
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t i0 = chunkBaseIndex + indices[i];
            uint32_t i1 = chunkBaseIndex + indices[i + 1];
            uint32_t i2 = chunkBaseIndex + indices[i + 2];

            // OBJ format: f v/vt/vn v/vt/vn v/vt/vn
            file << "f " << i0 << "/" << i0 << "/" << i0
                 << " " << i1 << "/" << i1 << "/" << i1
                 << " " << i2 << "/" << i2 << "/" << i2 << "\n";
        }

        globalVertexIndex += static_cast<uint32_t>(chunk->getVertices().size());
    }

    file.close();
    return true;
}

} // namespace eden
