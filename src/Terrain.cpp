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
    m_splatmap0.resize(resolution * resolution, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));  // Default: 100% texture 0
    m_splatmap1.resize(resolution * resolution, glm::vec4(0.0f));
    m_splatmap2.resize(resolution * resolution, glm::vec4(0.0f));
    m_splatmap3.resize(resolution * resolution, glm::vec4(0.0f));
    m_selectionmap.resize(resolution * resolution, 0.0f);  // No selection by default
    m_texHSBmap.resize(resolution * resolution, glm::vec3(0.0f, 1.0f, 1.0f));  // Default: no hue shift, normal sat/bright
    m_holemap.resize(resolution * resolution, 0.0f);  // No holes by default
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

void TerrainChunk::resetToDefaults() {
    int count = m_resolution * m_resolution;
    std::fill(m_heightmap.begin(), m_heightmap.end(), 0.0f);
    std::fill(m_colormap.begin(), m_colormap.end(), glm::vec3(-1.0f));
    std::fill(m_paintAlphamap.begin(), m_paintAlphamap.end(), 0.0f);
    std::fill(m_splatmap0.begin(), m_splatmap0.end(), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    std::fill(m_splatmap1.begin(), m_splatmap1.end(), glm::vec4(0.0f));
    std::fill(m_splatmap2.begin(), m_splatmap2.end(), glm::vec4(0.0f));
    std::fill(m_splatmap3.begin(), m_splatmap3.end(), glm::vec4(0.0f));
    std::fill(m_texHSBmap.begin(), m_texHSBmap.end(), glm::vec3(0.0f, 1.0f, 1.0f));
    std::fill(m_selectionmap.begin(), m_selectionmap.end(), 0.0f);
    std::fill(m_holemap.begin(), m_holemap.end(), 0.0f);
    regenerateMesh();
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

            // Splatmap weights (smoothly interpolated, no flat)
            vertex.texSplat0 = m_splatmap0[idx];
            vertex.texSplat1 = m_splatmap1[idx];
            vertex.texSplat2 = m_splatmap2[idx];
            vertex.texSplat3 = m_splatmap3[idx];

            // Selection weight
            vertex.selection = m_selectionmap[idx];

            // Paint intensity
            vertex.paintAlpha = m_paintAlphamap[idx];

            // Texture color adjustment (HSB)
            vertex.texHSB = m_texHSBmap[idx];

            // Hole mask
            vertex.holeMask = m_holemap[idx];
        }
    }

    // Generate indices (only if not already generated)
    if (m_indices.empty()) {
        rebuildIndices();
    }

    m_needsUpload = true;
}

void TerrainChunk::regenerateMesh() {
    rebuildVerticesFromHeightmap();
    if (m_triMode == TriangulationMode::Adaptive) {
        rebuildIndices();
    }
}

void TerrainChunk::rebuildIndices() {
    m_indices.clear();
    m_indices.reserve((m_resolution - 1) * (m_resolution - 1) * 6);

    for (int z = 0; z < m_resolution - 1; z++) {
        for (int x = 0; x < m_resolution - 1; x++) {
            int topLeft = z * m_resolution + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * m_resolution + x;
            int bottomRight = bottomLeft + 1;

            bool flipDiag = false;

            if (m_triMode == TriangulationMode::Alternating) {
                flipDiag = ((x + z) % 2 == 1);
            } else if (m_triMode == TriangulationMode::Adaptive) {
                // Choose diagonal that minimizes height difference
                float diagA = std::abs(m_heightmap[topLeft] - m_heightmap[bottomRight]);
                float diagB = std::abs(m_heightmap[topRight] - m_heightmap[bottomLeft]);
                flipDiag = (diagB < diagA);
            }

            if (flipDiag) {
                // Diagonal: topRight → bottomLeft
                m_indices.push_back(topLeft);
                m_indices.push_back(bottomLeft);
                m_indices.push_back(bottomRight);

                m_indices.push_back(topLeft);
                m_indices.push_back(bottomRight);
                m_indices.push_back(topRight);
            } else {
                // Diagonal: topLeft → bottomRight (default)
                m_indices.push_back(topLeft);
                m_indices.push_back(bottomLeft);
                m_indices.push_back(topRight);

                m_indices.push_back(topRight);
                m_indices.push_back(bottomLeft);
                m_indices.push_back(bottomRight);
            }
        }
    }

    m_needsUpload = true;
}

void TerrainChunk::setTriangulationMode(TriangulationMode mode) {
    if (m_triMode == mode) return;
    m_triMode = mode;
    rebuildIndices();
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
                                 const std::vector<glm::vec4>& splatmap0,
                                 const std::vector<glm::vec4>& splatmap1,
                                 const std::vector<glm::vec4>& splatmap2,
                                 const std::vector<glm::vec4>& splatmap3,
                                 const std::vector<glm::vec3>& texHSBmap) {
    size_t expected = static_cast<size_t>(m_resolution * m_resolution);

    if (heightmap.size() == expected) m_heightmap = heightmap;
    if (colormap.size() == expected) m_colormap = colormap;
    if (paintAlphamap.size() == expected) m_paintAlphamap = paintAlphamap;
    if (splatmap0.size() == expected) m_splatmap0 = splatmap0;
    if (splatmap1.size() == expected) m_splatmap1 = splatmap1;
    if (splatmap2.size() == expected) m_splatmap2 = splatmap2;
    if (splatmap3.size() == expected) m_splatmap3 = splatmap3;
    if (texHSBmap.size() == expected) m_texHSBmap = texHSBmap;

    rebuildVerticesFromHeightmap();
    m_needsUpload = true;
}

void TerrainChunk::setChunkDataLegacy(const std::vector<float>& heightmap,
                                       const std::vector<glm::vec3>& colormap,
                                       const std::vector<float>& paintAlphamap,
                                       const std::vector<glm::vec4>& texWeightmap,
                                       const std::vector<glm::uvec4>& texIndicesmap,
                                       const std::vector<glm::vec3>& texHSBmap) {
    size_t expected = static_cast<size_t>(m_resolution * m_resolution);

    if (heightmap.size() == expected) m_heightmap = heightmap;
    if (colormap.size() == expected) m_colormap = colormap;
    if (paintAlphamap.size() == expected) m_paintAlphamap = paintAlphamap;
    if (texHSBmap.size() == expected) m_texHSBmap = texHSBmap;

    // Convert old indices+weights to splatmap format
    if (texWeightmap.size() == expected && texIndicesmap.size() == expected) {
        m_splatmap0.resize(expected, glm::vec4(0.0f));
        m_splatmap1.resize(expected, glm::vec4(0.0f));
        m_splatmap2.resize(expected, glm::vec4(0.0f));
        m_splatmap3.resize(expected, glm::vec4(0.0f));
        for (size_t i = 0; i < expected; i++) {
            m_splatmap0[i] = glm::vec4(0.0f);
            m_splatmap1[i] = glm::vec4(0.0f);
            m_splatmap2[i] = glm::vec4(0.0f);
            m_splatmap3[i] = glm::vec4(0.0f);
            // Scatter weights into splatmap by index
            for (int s = 0; s < 4; s++) {
                unsigned int texIdx = texIndicesmap[i][s];
                float weight = texWeightmap[i][s];
                if (texIdx < 4) {
                    m_splatmap0[i][texIdx] += weight;
                } else if (texIdx < 8) {
                    m_splatmap1[i][texIdx - 4] += weight;
                } else if (texIdx < 12) {
                    m_splatmap2[i][texIdx - 8] += weight;
                } else if (texIdx < 16) {
                    m_splatmap3[i][texIdx - 12] += weight;
                }
            }
        }
    }

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

            // Shovel: lower nearest vertex to exactly 1m below original, capped
            if (mode == BrushMode::Shovel) {
                if (t <= 1.0f) {
                    float currentHeight = m_heightmap[z * m_resolution + x];
                    if (currentHeight > targetHeight) {
                        m_heightmap[z * m_resolution + x] = targetHeight;
                        modified = true;
                    }
                }
                continue;
            }

            // Furrow brush: dig center, raise edges
            if (mode == BrushMode::Furrow) {
                float currentHeight = m_heightmap[z * m_resolution + x];
                if (t <= 0.4f) {
                    // Center: dig down by strength
                    float newHeight = std::min(currentHeight, targetHeight - strength);
                    m_heightmap[z * m_resolution + x] = newHeight;
                    modified = true;
                } else if (t <= 1.0f) {
                    // Edges: raise up by half strength (berms)
                    float edgeFactor = 1.0f - (t - 0.4f) / 0.6f;  // 1.0 at inner edge, 0.0 at outer
                    float newHeight = currentHeight + strength * 0.5f * edgeFactor;
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
                    case BrushMode::FlattenToY:
                        // Flatten to a user-specified target height
                        newHeight = currentHeight + (targetHeight - currentHeight) * falloffMult;
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
                    case BrushMode::Terrace: {
                        float stepHeight = std::max(0.5f, strength);
                        float steppedHeight = std::floor(currentHeight / stepHeight) * stepHeight;
                        newHeight = currentHeight + (steppedHeight - currentHeight) * falloffMult;
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

                // Paint alpha = how much solid color overrides texture
                // At full strength + falloff, this goes to 1.0 (pure solid color, no texture)
                float paintIntensity = strength * falloffMult;

                // Blend toward target color
                glm::vec3 currentColor = m_colormap[idx].x >= 0.0f ? m_colormap[idx] : color;
                m_colormap[idx] = glm::mix(currentColor, color, paintIntensity);

                // Set paint alpha directly — full strength = fully opaque color
                float currentAlpha = m_paintAlphamap[idx];
                m_paintAlphamap[idx] = std::max(currentAlpha, paintIntensity);

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
    if (textureIndex < 0 || textureIndex >= 16) return;

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

                // Get all 16 weights as a flat array for easy manipulation
                float w[16] = {
                    m_splatmap0[idx].x, m_splatmap0[idx].y, m_splatmap0[idx].z, m_splatmap0[idx].w,
                    m_splatmap1[idx].x, m_splatmap1[idx].y, m_splatmap1[idx].z, m_splatmap1[idx].w,
                    m_splatmap2[idx].x, m_splatmap2[idx].y, m_splatmap2[idx].z, m_splatmap2[idx].w,
                    m_splatmap3[idx].x, m_splatmap3[idx].y, m_splatmap3[idx].z, m_splatmap3[idx].w
                };

                // Increase target texture weight
                float addAmount = strength * 0.1f * falloffMult;
                w[textureIndex] += addAmount;

                // Normalize so all 16 weights sum to 1
                float sum = 0.0f;
                for (int i = 0; i < 16; i++) sum += w[i];
                if (sum > 0.0f) {
                    for (int i = 0; i < 16; i++) w[i] /= sum;
                }

                // Write back
                m_splatmap0[idx] = glm::vec4(w[0], w[1], w[2], w[3]);
                m_splatmap1[idx] = glm::vec4(w[4], w[5], w[6], w[7]);
                m_splatmap2[idx] = glm::vec4(w[8], w[9], w[10], w[11]);
                m_splatmap3[idx] = glm::vec4(w[12], w[13], w[14], w[15]);

                // Blend HSB values towards the brush settings
                glm::vec3& hsb = m_texHSBmap[idx];
                glm::vec3 targetHSB(hue, saturation, brightness);
                hsb = glm::mix(hsb, targetHSB, addAmount);

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

        // Check if this vertex is a hole — return very low height so player falls through
        int res = chunk->getResolution();
        localX = std::clamp(localX, 0, res - 1);
        localZ = std::clamp(localZ, 0, res - 1);
        int idx = localZ * res + localX;
        if (idx < static_cast<int>(chunk->m_holemap.size()) && chunk->m_holemap[idx] > 0.5f) {
            return -100000.0f;
        }

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
                         const BrushShapeParams& shapeParams, float targetHeightOverride) {
    // For flatten mode, get the target height ONCE at the brush center
    // This ensures all chunks flatten to the same height (no seams)
    float targetHeight = 0.0f;
    if (mode == BrushMode::FlattenToY) {
        targetHeight = targetHeightOverride;
    } else if (mode == BrushMode::Flatten) {
        targetHeight = getHeightAt(worldX, worldZ);
    } else if (mode == BrushMode::Plateau) {
        // Plateau mode: extrude the center point upward by strength amount
        targetHeight = getHeightAt(worldX, worldZ) + strength;
    } else if (mode == BrushMode::Trench || mode == BrushMode::Furrow) {
        // Trench/Furrow: get center height as reference
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

    // Shovel: lower vertices to target height, capped (won't go below target)
    // If targetHeightOverride is non-zero, use it as absolute target and keep caller's radius (gameplay dig)
    // Otherwise default to 1m below current surface with tiny radius (editor single-vertex brush)
    if (mode == BrushMode::Shovel) {
        if (targetHeightOverride != 0.0f) {
            targetHeight = targetHeightOverride;
            // Caller controls radius for gameplay dig
        } else {
            targetHeight = getHeightAt(worldX, worldZ) - 1.0f;
            radius = m_config.tileSize * 0.5f;  // Single vertex for editor brush
        }
        strength = 100.0f;  // Large strength so it snaps to target in one click
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

void Terrain::syncEdgeTextureData() {
    // Collect modified set first to avoid cascade from marking neighbors
    std::vector<glm::ivec2> modified;
    for (auto& [coord, chunk] : m_chunks) {
        if (chunk->needsUpload()) modified.push_back(coord);
    }
    for (auto& coord : modified) {
        auto& chunk = m_chunks[coord];
        int res = chunk->getResolution();

        // Right neighbor
        auto rightIt = m_chunks.find(glm::ivec2(coord.x + 1, coord.y));
        if (rightIt != m_chunks.end()) {
            auto& right = rightIt->second;
            for (int z = 0; z < res; z++) {
                int srcIdx = z * res + (res - 1);
                int dstIdx = z * res + 0;
                right->m_splatmap0[dstIdx] = chunk->m_splatmap0[srcIdx];
                right->m_splatmap1[dstIdx] = chunk->m_splatmap1[srcIdx];
                right->m_splatmap2[dstIdx] = chunk->m_splatmap2[srcIdx];
                right->m_splatmap3[dstIdx] = chunk->m_splatmap3[srcIdx];
                right->m_texHSBmap[dstIdx] = chunk->m_texHSBmap[srcIdx];
                right->m_colormap[dstIdx] = chunk->m_colormap[srcIdx];
                right->m_paintAlphamap[dstIdx] = chunk->m_paintAlphamap[srcIdx];
            }
            right->m_needsUpload = true;
        }

        // Bottom neighbor
        auto bottomIt = m_chunks.find(glm::ivec2(coord.x, coord.y + 1));
        if (bottomIt != m_chunks.end()) {
            auto& bottom = bottomIt->second;
            for (int x = 0; x < res; x++) {
                int srcIdx = (res - 1) * res + x;
                int dstIdx = x;
                bottom->m_splatmap0[dstIdx] = chunk->m_splatmap0[srcIdx];
                bottom->m_splatmap1[dstIdx] = chunk->m_splatmap1[srcIdx];
                bottom->m_splatmap2[dstIdx] = chunk->m_splatmap2[srcIdx];
                bottom->m_splatmap3[dstIdx] = chunk->m_splatmap3[srcIdx];
                bottom->m_texHSBmap[dstIdx] = chunk->m_texHSBmap[srcIdx];
                bottom->m_colormap[dstIdx] = chunk->m_colormap[srcIdx];
                bottom->m_paintAlphamap[dstIdx] = chunk->m_paintAlphamap[srcIdx];
            }
            bottom->m_needsUpload = true;
        }

        // Left neighbor
        auto leftIt = m_chunks.find(glm::ivec2(coord.x - 1, coord.y));
        if (leftIt != m_chunks.end()) {
            auto& left = leftIt->second;
            for (int z = 0; z < res; z++) {
                int srcIdx = z * res;
                int dstIdx = z * res + (res - 1);
                left->m_splatmap0[dstIdx] = chunk->m_splatmap0[srcIdx];
                left->m_splatmap1[dstIdx] = chunk->m_splatmap1[srcIdx];
                left->m_splatmap2[dstIdx] = chunk->m_splatmap2[srcIdx];
                left->m_splatmap3[dstIdx] = chunk->m_splatmap3[srcIdx];
                left->m_texHSBmap[dstIdx] = chunk->m_texHSBmap[srcIdx];
                left->m_colormap[dstIdx] = chunk->m_colormap[srcIdx];
                left->m_paintAlphamap[dstIdx] = chunk->m_paintAlphamap[srcIdx];
            }
            left->m_needsUpload = true;
        }

        // Top neighbor
        auto topIt = m_chunks.find(glm::ivec2(coord.x, coord.y - 1));
        if (topIt != m_chunks.end()) {
            auto& top = topIt->second;
            for (int x = 0; x < res; x++) {
                int srcIdx = x;
                int dstIdx = (res - 1) * res + x;
                top->m_splatmap0[dstIdx] = chunk->m_splatmap0[srcIdx];
                top->m_splatmap1[dstIdx] = chunk->m_splatmap1[srcIdx];
                top->m_splatmap2[dstIdx] = chunk->m_splatmap2[srcIdx];
                top->m_splatmap3[dstIdx] = chunk->m_splatmap3[srcIdx];
                top->m_texHSBmap[dstIdx] = chunk->m_texHSBmap[srcIdx];
                top->m_colormap[dstIdx] = chunk->m_colormap[srcIdx];
                top->m_paintAlphamap[dstIdx] = chunk->m_paintAlphamap[srcIdx];
            }
            top->m_needsUpload = true;
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

void Terrain::setTriangulationMode(TriangulationMode mode) {
    for (auto& pair : m_chunks) {
        pair.second->setTriangulationMode(mode);
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

void Terrain::stitchChunkEdges() {
    // For each chunk, ensure shared edge vertices are identical between neighbors.
    // Average heights and texture data so both sides match perfectly.
    for (auto& [coord, chunk] : m_chunks) {
        int res = chunk->getResolution();

        // Stitch right edge: chunk's x=res-1 ↔ right neighbor's x=0
        auto rightIt = m_chunks.find(glm::ivec2(coord.x + 1, coord.y));
        if (rightIt != m_chunks.end()) {
            auto& right = rightIt->second;
            for (int z = 0; z < res; z++) {
                int idxL = z * res + (res - 1);
                int idxR = z * res + 0;

                // Average heights
                float avgHeight = (chunk->m_heightmap[idxL] + right->m_heightmap[idxR]) * 0.5f;
                chunk->m_heightmap[idxL] = avgHeight;
                right->m_heightmap[idxR] = avgHeight;

                // Average all per-vertex data
                chunk->m_colormap[idxL] = right->m_colormap[idxR] =
                    (chunk->m_colormap[idxL] + right->m_colormap[idxR]) * 0.5f;
                chunk->m_paintAlphamap[idxL] = right->m_paintAlphamap[idxR] =
                    (chunk->m_paintAlphamap[idxL] + right->m_paintAlphamap[idxR]) * 0.5f;
                chunk->m_splatmap0[idxL] = right->m_splatmap0[idxR] =
                    (chunk->m_splatmap0[idxL] + right->m_splatmap0[idxR]) * 0.5f;
                chunk->m_splatmap1[idxL] = right->m_splatmap1[idxR] =
                    (chunk->m_splatmap1[idxL] + right->m_splatmap1[idxR]) * 0.5f;
                chunk->m_splatmap2[idxL] = right->m_splatmap2[idxR] =
                    (chunk->m_splatmap2[idxL] + right->m_splatmap2[idxR]) * 0.5f;
                chunk->m_splatmap3[idxL] = right->m_splatmap3[idxR] =
                    (chunk->m_splatmap3[idxL] + right->m_splatmap3[idxR]) * 0.5f;
                chunk->m_texHSBmap[idxL] = right->m_texHSBmap[idxR] =
                    (chunk->m_texHSBmap[idxL] + right->m_texHSBmap[idxR]) * 0.5f;
            }
        }

        // Stitch bottom edge: chunk's z=res-1 ↔ bottom neighbor's z=0
        auto bottomIt = m_chunks.find(glm::ivec2(coord.x, coord.y + 1));
        if (bottomIt != m_chunks.end()) {
            auto& bottom = bottomIt->second;
            for (int x = 0; x < res; x++) {
                int idxT = (res - 1) * res + x;
                int idxB = 0 * res + x;

                float avgHeight = (chunk->m_heightmap[idxT] + bottom->m_heightmap[idxB]) * 0.5f;
                chunk->m_heightmap[idxT] = avgHeight;
                bottom->m_heightmap[idxB] = avgHeight;

                chunk->m_colormap[idxT] = bottom->m_colormap[idxB] =
                    (chunk->m_colormap[idxT] + bottom->m_colormap[idxB]) * 0.5f;
                chunk->m_paintAlphamap[idxT] = bottom->m_paintAlphamap[idxB] =
                    (chunk->m_paintAlphamap[idxT] + bottom->m_paintAlphamap[idxB]) * 0.5f;
                chunk->m_splatmap0[idxT] = bottom->m_splatmap0[idxB] =
                    (chunk->m_splatmap0[idxT] + bottom->m_splatmap0[idxB]) * 0.5f;
                chunk->m_splatmap1[idxT] = bottom->m_splatmap1[idxB] =
                    (chunk->m_splatmap1[idxT] + bottom->m_splatmap1[idxB]) * 0.5f;
                chunk->m_splatmap2[idxT] = bottom->m_splatmap2[idxB] =
                    (chunk->m_splatmap2[idxT] + bottom->m_splatmap2[idxB]) * 0.5f;
                chunk->m_splatmap3[idxT] = bottom->m_splatmap3[idxB] =
                    (chunk->m_splatmap3[idxT] + bottom->m_splatmap3[idxB]) * 0.5f;
                chunk->m_texHSBmap[idxT] = bottom->m_texHSBmap[idxB] =
                    (chunk->m_texHSBmap[idxT] + bottom->m_texHSBmap[idxB]) * 0.5f;
            }
        }
    }

    // Rebuild all chunks — rebuildVerticesFromHeightmap recalculates normals,
    // but edge normals are still wrong because calculateNormal clamps at boundaries.
    // Fix: after rebuilding, copy edge normals from neighbors to ensure smooth lighting.
    for (auto& [coord, chunk] : m_chunks) {
        chunk->rebuildVerticesFromHeightmap();
    }

    // Second pass: fix edge normals using neighbor height data
    for (auto& [coord, chunk] : m_chunks) {
        int res = chunk->getResolution();

        // Fix right-edge normals (x = res-1): need height from right neighbor at x=1
        auto rightIt = m_chunks.find(glm::ivec2(coord.x + 1, coord.y));
        if (rightIt != m_chunks.end()) {
            auto& right = rightIt->second;
            for (int z = 0; z < res; z++) {
                int idx = z * res + (res - 1);
                float hL = chunk->getHeightAtLocal(res - 2, z);
                float hR = right->getHeightAtLocal(1, z);  // Use neighbor's data
                float hD = chunk->getHeightAtLocal(res - 1, std::max(0, z - 1));
                float hU = chunk->getHeightAtLocal(res - 1, std::min(res - 1, z + 1));
                chunk->m_vertices[idx].normal = glm::normalize(glm::vec3(hL - hR, 2.0f * chunk->getTileSize(), hD - hU));
            }
        }

        // Fix left-edge normals (x = 0): need height from left neighbor at x=res-2
        auto leftIt = m_chunks.find(glm::ivec2(coord.x - 1, coord.y));
        if (leftIt != m_chunks.end()) {
            auto& left = leftIt->second;
            for (int z = 0; z < res; z++) {
                int idx = z * res + 0;
                float hL = left->getHeightAtLocal(res - 2, z);  // Use neighbor's data
                float hR = chunk->getHeightAtLocal(1, z);
                float hD = chunk->getHeightAtLocal(0, std::max(0, z - 1));
                float hU = chunk->getHeightAtLocal(0, std::min(res - 1, z + 1));
                chunk->m_vertices[idx].normal = glm::normalize(glm::vec3(hL - hR, 2.0f * chunk->getTileSize(), hD - hU));
            }
        }

        // Fix bottom-edge normals (z = res-1): need height from bottom neighbor at z=1
        auto bottomIt = m_chunks.find(glm::ivec2(coord.x, coord.y + 1));
        if (bottomIt != m_chunks.end()) {
            auto& bottom = bottomIt->second;
            for (int x = 0; x < res; x++) {
                int idx = (res - 1) * res + x;
                float hL = chunk->getHeightAtLocal(std::max(0, x - 1), res - 1);
                float hR = chunk->getHeightAtLocal(std::min(res - 1, x + 1), res - 1);
                float hD = chunk->getHeightAtLocal(x, res - 2);
                float hU = bottom->getHeightAtLocal(x, 1);  // Use neighbor's data
                chunk->m_vertices[idx].normal = glm::normalize(glm::vec3(hL - hR, 2.0f * chunk->getTileSize(), hD - hU));
            }
        }

        // Fix top-edge normals (z = 0): need height from top neighbor at z=res-2
        auto topIt = m_chunks.find(glm::ivec2(coord.x, coord.y - 1));
        if (topIt != m_chunks.end()) {
            auto& top = topIt->second;
            for (int x = 0; x < res; x++) {
                int idx = 0 * res + x;
                float hL = chunk->getHeightAtLocal(std::max(0, x - 1), 0);
                float hR = chunk->getHeightAtLocal(std::min(res - 1, x + 1), 0);
                float hD = top->getHeightAtLocal(x, res - 2);  // Use neighbor's data
                float hU = chunk->getHeightAtLocal(x, 1);
                chunk->m_vertices[idx].normal = glm::normalize(glm::vec3(hL - hR, 2.0f * chunk->getTileSize(), hD - hU));
            }
        }

        chunk->m_needsUpload = true;
    }
}

void Terrain::stitchModifiedChunkEdges() {
    // Collect modified chunks first to avoid cascade
    std::vector<glm::ivec2> modifiedCoords;
    for (auto& [coord, chunk] : m_chunks) {
        if (chunk->needsUpload()) modifiedCoords.push_back(coord);
    }

    // Helper: copy ALL per-vertex data from src to dst at given indices
    auto copyEdgeVertex = [](std::shared_ptr<TerrainChunk>& src, int srcIdx,
                             std::shared_ptr<TerrainChunk>& dst, int dstIdx) {
        // Average heights and colors (these blend smoothly)
        dst->m_heightmap[dstIdx] = src->m_heightmap[srcIdx] =
            (src->m_heightmap[srcIdx] + dst->m_heightmap[dstIdx]) * 0.5f;
        dst->m_colormap[dstIdx] = src->m_colormap[srcIdx] =
            (src->m_colormap[srcIdx] + dst->m_colormap[dstIdx]) * 0.5f;
        dst->m_paintAlphamap[dstIdx] = src->m_paintAlphamap[srcIdx] =
            (src->m_paintAlphamap[srcIdx] + dst->m_paintAlphamap[dstIdx]) * 0.5f;

        // For texture data: copy splatmap from modified chunk (src) to neighbor (dst)
        dst->m_splatmap0[dstIdx] = src->m_splatmap0[srcIdx];
        dst->m_splatmap1[dstIdx] = src->m_splatmap1[srcIdx];
        dst->m_splatmap2[dstIdx] = src->m_splatmap2[srcIdx];
        dst->m_splatmap3[dstIdx] = src->m_splatmap3[srcIdx];
        dst->m_texHSBmap[dstIdx] = src->m_texHSBmap[srcIdx];
    };

    for (auto& coord : modifiedCoords) {
        auto& chunk = m_chunks[coord];
        int res = chunk->getResolution();

        // Stitch right edge: copy from this chunk to right neighbor
        auto rightIt = m_chunks.find(glm::ivec2(coord.x + 1, coord.y));
        if (rightIt != m_chunks.end()) {
            auto& right = rightIt->second;
            for (int z = 0; z < res; z++) {
                copyEdgeVertex(chunk, z * res + (res - 1), right, z * res + 0);
            }
            right->rebuildVerticesFromHeightmap();
            right->m_needsUpload = true;
        }

        // Stitch bottom edge
        auto bottomIt = m_chunks.find(glm::ivec2(coord.x, coord.y + 1));
        if (bottomIt != m_chunks.end()) {
            auto& bottom = bottomIt->second;
            for (int x = 0; x < res; x++) {
                copyEdgeVertex(chunk, (res - 1) * res + x, bottom, x);
            }
            bottom->rebuildVerticesFromHeightmap();
            bottom->m_needsUpload = true;
        }

        // Stitch left edge
        auto leftIt = m_chunks.find(glm::ivec2(coord.x - 1, coord.y));
        if (leftIt != m_chunks.end()) {
            auto& left = leftIt->second;
            for (int z = 0; z < res; z++) {
                copyEdgeVertex(chunk, z * res, left, z * res + (res - 1));
            }
            left->rebuildVerticesFromHeightmap();
            left->m_needsUpload = true;
        }

        // Stitch top edge
        auto topIt = m_chunks.find(glm::ivec2(coord.x, coord.y - 1));
        if (topIt != m_chunks.end()) {
            auto& top = topIt->second;
            for (int x = 0; x < res; x++) {
                copyEdgeVertex(chunk, x, top, (res - 1) * res + x);
            }
            top->rebuildVerticesFromHeightmap();
            top->m_needsUpload = true;
        }

        chunk->rebuildVerticesFromHeightmap();
    }
}

void Terrain::setHoleRect(float worldMinX, float worldMinZ, float worldMaxX, float worldMaxZ, bool isHole) {
    float holeVal = isHole ? 1.0f : 0.0f;
    for (auto& [coord, chunk] : m_chunks) {
        float chunkWorldX = coord.x * (chunk->getResolution() - 1) * chunk->getTileSize();
        float chunkWorldZ = coord.y * (chunk->getResolution() - 1) * chunk->getTileSize();
        float chunkSize = (chunk->getResolution() - 1) * chunk->getTileSize();

        // Skip chunks that don't overlap the rectangle
        if (chunkWorldX + chunkSize < worldMinX || chunkWorldX > worldMaxX) continue;
        if (chunkWorldZ + chunkSize < worldMinZ || chunkWorldZ > worldMaxZ) continue;

        int res = chunk->getResolution();
        float tileSize = chunk->getTileSize();
        bool modified = false;

        for (int z = 0; z < res; z++) {
            for (int x = 0; x < res; x++) {
                float wx = chunkWorldX + x * tileSize;
                float wz = chunkWorldZ + z * tileSize;
                if (wx >= worldMinX && wx <= worldMaxX && wz >= worldMinZ && wz <= worldMaxZ) {
                    int idx = z * res + x;
                    chunk->m_holemap[idx] = holeVal;
                    modified = true;
                }
            }
        }
        if (modified) {
            chunk->rebuildVerticesFromHeightmap();
            chunk->m_needsUpload = true;
        }
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
