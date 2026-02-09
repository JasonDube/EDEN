#pragma once

#include "BinaryLevelFormat.hpp"
#include "SceneObject.hpp"
#include <string>
#include <vector>
#include <memory>

namespace eden {

struct ModelVertex;

// Loaded mesh data ready for GPU upload
struct BinaryMeshData {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
    int32_t textureId = -1;  // Index into textures array, -1 if none
};

// Loaded texture data
struct BinaryTextureData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

// Loaded object data (properties + mesh reference)
struct BinaryObjectData {
    std::string name;

    // Transform
    glm::vec3 position{0};
    glm::vec3 rotation{0};
    glm::vec3 scale{1};

    // Color adjustments
    float hueShift = 0.0f;
    float saturation = 1.0f;
    float brightness = 1.0f;

    // Collision
    int bulletCollisionType = 0;
    int beingType = 0;
    float patrolSpeed = 5.0f;

    // Flags
    bool visible = true;
    bool aabbCollision = false;
    bool polygonCollision = false;
    bool kinematicPlatform = false;
    bool dailySchedule = false;
    bool isSkinned = false;
    bool isPrimitive = false;
    bool isDoor = false;

    // Frozen transform
    bool hasFrozenTransform = false;
    glm::vec3 frozenRotation{0};
    glm::vec3 frozenScale{1};

    // Primitive properties
    int primitiveType = 0;
    float primitiveSize = 1.0f;
    float primitiveRadius = 0.5f;
    float primitiveHeight = 1.0f;
    int primitiveSegments = 16;
    glm::vec4 primitiveColor{0.7f, 0.7f, 0.7f, 1.0f};

    // Door properties
    std::string doorId;
    std::string targetLevel;
    std::string targetDoorId;

    // Description (visible to AI perception)
    std::string description;

    // References
    int32_t meshId = -1;        // Index into meshes array
    std::string modelPath;      // Original GLB path (for skinned models or fallback)
};

// Result of loading a binary level
struct BinaryLevelData {
    bool success = false;
    std::string error;

    std::vector<BinaryMeshData> meshes;
    std::vector<BinaryTextureData> textures;
    std::vector<BinaryObjectData> objects;

    // Statistics
    size_t totalMeshDataSize = 0;
    size_t totalTextureDataSize = 0;
};

// Reads binary level files (.edenbin) for fast loading
class BinaryLevelReader {
public:
    BinaryLevelReader() = default;

    // Load a binary level file
    // Returns BinaryLevelData with success=false on error
    BinaryLevelData load(const std::string& filepath);

    // Check if a binary level file exists and is valid (quick header check)
    static bool exists(const std::string& filepath);

    // Get the binary path for a given .eden file
    static std::string getBinaryPath(const std::string& edenPath);

private:
    bool readHeader(std::ifstream& file, BinaryLevelHeader& header);
    bool readMeshes(std::ifstream& file, const BinaryLevelHeader& header, BinaryLevelData& data);
    bool readTextures(std::ifstream& file, const BinaryLevelHeader& header, BinaryLevelData& data);
    bool readObjects(std::ifstream& file, const BinaryLevelHeader& header, BinaryLevelData& data);
    bool readStrings(std::ifstream& file, uint64_t stringTableOffset);

    std::vector<std::string> m_strings;
};

} // namespace eden
