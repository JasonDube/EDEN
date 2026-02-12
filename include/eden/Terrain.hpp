#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>

namespace eden {

struct Vertex3D {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 texWeights;  // Blend weights for 4 texture layers
    glm::uvec4 texIndices; // Which 4 textures this vertex uses (indices into texture array)
    float selection;       // Selection weight (0 = not selected, 1 = selected)
    float paintAlpha;      // Paint intensity (0 = texture only, 1 = painted color only)
    glm::vec3 texHSB;      // Per-vertex texture color adjustment (hue, saturation, brightness)
};

enum class BrushMode { Raise, Lower, Smooth, Flatten, Paint, Crack, Texture, Plateau, LevelMin, Grab, Select, Deselect, MoveObject, Spire, Ridged, Trench, PathMode, Terrace, FlattenToY };

enum class BrushShape { Circle, Ellipse, Square };

enum class TriangulationMode { Default, Alternating, Adaptive };

struct BrushShapeParams {
    BrushShape shape = BrushShape::Circle;
    float aspectRatio = 0.3f;   // For ellipse: height/width ratio (0.3 = thin ellipse)
    float rotation = 0.0f;      // Rotation angle in radians

    // Calculate normalized distance (0 = center, 1 = edge) for a point relative to brush center
    // Returns value > 1 if point is outside the brush
    float getNormalizedDistance(float dx, float dz, float radius) const {
        // Apply inverse rotation to get local coordinates
        float cosRot = std::cos(-rotation);
        float sinRot = std::sin(-rotation);
        float localX = dx * cosRot - dz * sinRot;
        float localZ = dx * sinRot + dz * cosRot;

        switch (shape) {
            case BrushShape::Circle:
                return std::sqrt(localX * localX + localZ * localZ) / radius;
            case BrushShape::Ellipse: {
                // Ellipse: x²/a² + y²/b² = 1, where a = radius, b = radius * aspectRatio
                float nx = localX / radius;
                float nz = localZ / (radius * aspectRatio);
                return std::sqrt(nx * nx + nz * nz);
            }
            case BrushShape::Square:
                return std::max(std::abs(localX), std::abs(localZ)) / radius;
            default:
                return std::sqrt(localX * localX + localZ * localZ) / radius;
        }
    }
};

struct TerrainConfig {
    int chunkResolution = 64;       // Vertices per chunk side
    float tileSize = 1.0f;          // World units per vertex
    int viewDistance = 4;           // Chunks visible in each direction
    float heightScale = 30.0f;      // Maximum terrain height
    float noiseScale = 0.02f;       // Noise frequency
    int noiseOctaves = 6;
    float noisePersistence = 0.5f;

    // Fixed terrain bounds (for pre-loading)
    bool useFixedBounds = false;    // If true, terrain has fixed size
    glm::ivec2 minChunk{-16, -16};  // Minimum chunk coordinate
    glm::ivec2 maxChunk{15, 15};    // Maximum chunk coordinate (32x32 = 1024 chunks)
    bool wrapWorld = false;         // If true, world wraps at edges (planet mode)
};

// Progress callback for pre-loading: (chunksLoaded, totalChunks)
using TerrainLoadCallback = std::function<void(int, int)>;

class TerrainChunk {
public:
    TerrainChunk(glm::ivec2 coord, const TerrainConfig& config);

    const std::vector<Vertex3D>& getVertices() const { return m_vertices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }
    glm::ivec2 getCoord() const { return m_coord; }
    glm::vec3 getWorldPosition() const;

    void setBufferHandle(uint32_t handle) { m_bufferHandle = handle; }
    uint32_t getBufferHandle() const { return m_bufferHandle; }
    bool needsUpload() const { return m_needsUpload; }
    void markUploaded() { m_needsUpload = false; }

    // Editing support
    using HeightLookup = std::function<float(float, float)>;
    void applyBrush(float worldX, float worldZ, float radius, float strength, float falloff, BrushMode mode,
                    float targetHeight = 0.0f, HeightLookup heightLookup = nullptr,
                    const BrushShapeParams& shapeParams = BrushShapeParams{});
    void applyColorBrush(float worldX, float worldZ, float radius, float strength, float falloff, const glm::vec3& color,
                         const BrushShapeParams& shapeParams = BrushShapeParams{});
    void applyTextureBrush(float worldX, float worldZ, float radius, float strength, float falloff, int textureIndex,
                           float hue = 0.0f, float saturation = 1.0f, float brightness = 1.0f,
                           const BrushShapeParams& shapeParams = BrushShapeParams{});
    void applySelectionBrush(float worldX, float worldZ, float radius, float strength, float falloff, bool select,
                             const BrushShapeParams& shapeParams = BrushShapeParams{});
    void clearSelection();
    void regenerateMesh();
    void setTriangulationMode(TriangulationMode mode);
    float getHeightAtLocal(int x, int z) const;
    void setHeightAtLocal(int x, int z, float height);
    float getSelectionAtLocal(int x, int z) const;
    void setSelectionAtLocal(int x, int z, float value);
    int getResolution() const { return m_resolution; }
    float getTileSize() const { return m_tileSize; }
    float getChunkWorldSize() const { return m_chunkWorldSize; }
    bool hasSelection() const;

    // Check if world position is within this chunk
    bool containsWorldPos(float worldX, float worldZ) const;

    // Level loading - set chunk data directly
    void setChunkData(const std::vector<float>& heightmap,
                      const std::vector<glm::vec3>& colormap,
                      const std::vector<float>& paintAlphamap,
                      const std::vector<glm::vec4>& texWeightmap,
                      const std::vector<glm::uvec4>& texIndicesmap,
                      const std::vector<glm::vec3>& texHSBmap);

private:
    void generate(const TerrainConfig& config);
    void rebuildVerticesFromHeightmap();
    void rebuildIndices();
    glm::vec3 getTerrainColor(float normalizedHeight);
    glm::vec3 calculateNormal(int x, int z);

    glm::ivec2 m_coord;
    int m_resolution;
    float m_tileSize;
    float m_chunkWorldSize;
    float m_heightScale;
    std::vector<float> m_heightmap;
    std::vector<glm::vec3> m_colormap;  // Per-vertex color override (-1 = use height-based)
    std::vector<float> m_paintAlphamap;  // Per-vertex paint intensity (0-1)
    std::vector<glm::vec4> m_texWeightmap;  // Per-vertex texture blend weights
    std::vector<glm::uvec4> m_texIndicesmap;  // Per-vertex texture indices (which 4 textures to blend)
    std::vector<float> m_selectionmap;  // Per-vertex selection weight (0-1)
    std::vector<glm::vec3> m_texHSBmap;  // Per-vertex texture color adjustment (hue, saturation, brightness)
    std::vector<Vertex3D> m_vertices;
    std::vector<uint32_t> m_indices;
    uint32_t m_bufferHandle = UINT32_MAX;
    bool m_needsUpload = true;
    TriangulationMode m_triMode = TriangulationMode::Default;
};

// Hash function for glm::ivec2
struct IVec2Hash {
    size_t operator()(const glm::ivec2& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 16);
    }
};

class Terrain {
public:
    explicit Terrain(const TerrainConfig& config = {});

    // Pre-load all chunks within fixed bounds (call once at startup)
    // Returns total number of chunks loaded
    int preloadAllChunks(TerrainLoadCallback progressCallback = nullptr);

    // Check if pre-loading is complete
    bool isFullyLoaded() const { return m_fullyLoaded; }

    // Get total chunk count for fixed bounds terrain
    int getTotalChunkCount() const;

    void update(const glm::vec3& cameraPosition);

    // Visible chunk with optional render offset (for world wrapping)
    struct VisibleChunk {
        std::shared_ptr<TerrainChunk> chunk;
        glm::vec3 renderOffset{0};  // Add this to chunk position when rendering
    };

    const std::vector<VisibleChunk>& getVisibleChunks() const { return m_visibleChunks; }
    const TerrainConfig& getConfig() const { return m_config; }

    float getHeightAt(float worldX, float worldZ) const;

    // World wrapping (planet mode)
    glm::vec3 wrapWorldPosition(const glm::vec3& pos) const;
    glm::vec2 getWorldSize() const;  // Returns world dimensions in units

    // Raycasting for mouse picking
    bool raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir, glm::vec3& outHitPos) const;

    // Apply brush to terrain at world position
    void applyBrush(float worldX, float worldZ, float radius, float strength, float falloff, BrushMode mode,
                    const BrushShapeParams& shapeParams = BrushShapeParams{},
                    float targetHeightOverride = 0.0f);
    void applyColorBrush(float worldX, float worldZ, float radius, float strength, float falloff, const glm::vec3& color,
                         const BrushShapeParams& shapeParams = BrushShapeParams{});
    void applyTextureBrush(float worldX, float worldZ, float radius, float strength, float falloff, int textureIndex,
                           float hue = 0.0f, float saturation = 1.0f, float brightness = 1.0f,
                           const BrushShapeParams& shapeParams = BrushShapeParams{});
    void applySelectionBrush(float worldX, float worldZ, float radius, float strength, float falloff, bool select,
                             const BrushShapeParams& shapeParams = BrushShapeParams{});

    // Selection methods
    void clearAllSelection();
    bool hasAnySelection() const { return m_hasSelection; }
    glm::vec3 getSelectionCenter() const { return m_selectionCenter; }
    void moveSelection(const glm::vec3& delta);
    void tiltSelection(float tiltX, float tiltZ);  // Tilt selection around center (degrees)
    void updateSelectionCache();  // Call after selection changes

    // Get chunk at world position
    std::shared_ptr<TerrainChunk> getChunkAt(float worldX, float worldZ);

    // Get chunk by coordinate (for level loading)
    std::shared_ptr<TerrainChunk> getChunkByCoord(glm::ivec2 coord);

    // Get all chunks (for saving)
    const std::unordered_map<glm::ivec2, std::shared_ptr<TerrainChunk>, IVec2Hash>& getAllChunks() const {
        return m_chunks;
    }

    // Triangulation mode (affects cliff visual quality)
    void setTriangulationMode(TriangulationMode mode);

    // Export terrain to OBJ file
    bool exportToOBJ(const std::string& filepath) const;

private:
    glm::ivec2 worldToChunkCoord(const glm::vec3& worldPos) const;
    glm::ivec2 wrapChunkCoord(glm::ivec2 coord) const;

    TerrainConfig m_config;
    std::unordered_map<glm::ivec2, std::shared_ptr<TerrainChunk>, IVec2Hash> m_chunks;
    std::vector<VisibleChunk> m_visibleChunks;
    glm::ivec2 m_lastCameraChunk{INT_MAX, INT_MAX};
    bool m_fullyLoaded = false;

    // Cached selection state (updated via updateSelectionCache)
    bool m_hasSelection = false;
    glm::vec3 m_selectionCenter{0};
};

} // namespace eden
