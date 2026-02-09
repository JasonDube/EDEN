#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace eden {

// Binary level format (.edenbin) for fast loading
// All structures are packed for direct file I/O

#pragma pack(push, 1)

// File header - 128 bytes total
struct BinaryLevelHeader {
    char magic[4] = {'E', 'B', 'I', 'N'};  // "EBIN" - Eden Binary
    uint32_t version = 1;

    // Section counts
    uint32_t meshCount = 0;
    uint32_t textureCount = 0;
    uint32_t objectCount = 0;

    // Section offsets (from start of file)
    uint64_t meshTableOffset = 0;      // Array of BinaryMeshEntry
    uint64_t textureTableOffset = 0;   // Array of BinaryTextureEntry
    uint64_t objectTableOffset = 0;    // Array of BinaryObjectEntry
    uint64_t meshDataOffset = 0;       // Raw vertex + index data blob
    uint64_t textureDataOffset = 0;    // Raw RGBA pixel data blob

    // Total sizes
    uint64_t meshDataSize = 0;
    uint64_t textureDataSize = 0;

    // Reserved for future use
    uint8_t reserved[52] = {0};
};
static_assert(sizeof(BinaryLevelHeader) == 128, "BinaryLevelHeader must be 128 bytes");

// Mesh entry - describes one mesh in the mesh data blob
// 64 bytes total
struct BinaryMeshEntry {
    // Vertex data location
    uint64_t vertexOffset = 0;    // Offset within mesh data blob
    uint32_t vertexCount = 0;
    uint32_t vertexStride = 48;   // sizeof(ModelVertex) - position(12) + normal(12) + texCoord(8) + color(16)

    // Index data location
    uint64_t indexOffset = 0;     // Offset within mesh data blob
    uint32_t indexCount = 0;
    uint32_t indexType = 4;       // sizeof(uint32_t)

    // Bounding box (local space)
    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};

    // Associated texture (-1 if none)
    int32_t textureId = -1;

    // Reserved
    uint8_t reserved[4] = {0};
};
static_assert(sizeof(BinaryMeshEntry) == 64, "BinaryMeshEntry must be 64 bytes");

// Texture entry - describes one texture in the texture data blob
// 32 bytes total
struct BinaryTextureEntry {
    uint64_t dataOffset = 0;      // Offset within texture data blob
    uint32_t dataSize = 0;        // Size in bytes (width * height * 4 for RGBA)
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;          // 0 = RGBA8, reserved for future formats
    uint8_t reserved[8] = {0};
};
static_assert(sizeof(BinaryTextureEntry) == 32, "BinaryTextureEntry must be 32 bytes");

// Object flags (bitfield)
enum BinaryObjectFlags : uint32_t {
    BOF_NONE             = 0,
    BOF_VISIBLE          = 1 << 0,
    BOF_AABB_COLLISION   = 1 << 1,
    BOF_POLY_COLLISION   = 1 << 2,
    BOF_KINEMATIC        = 1 << 3,
    BOF_FROZEN_TRANSFORM = 1 << 4,
    BOF_DAILY_SCHEDULE   = 1 << 5,
    BOF_IS_SKINNED       = 1 << 6,
    BOF_IS_PRIMITIVE     = 1 << 7,
    BOF_IS_DOOR          = 1 << 8,
};

// Object entry - describes one scene object
// 256 bytes total to accommodate all properties
struct BinaryObjectEntry {
    // Mesh reference
    int32_t meshId = -1;          // Index into mesh table (-1 = skinned model, use JSON)

    // Name (null-terminated, max 63 chars)
    char name[64] = {0};

    // Transform
    glm::vec3 position{0};
    glm::vec3 rotation{0};        // Euler angles in degrees
    glm::vec3 scale{1};

    // Color adjustments
    float hueShift = 0.0f;
    float saturation = 1.0f;
    float brightness = 1.0f;

    // Collision
    int32_t bulletCollisionType = 0;  // BulletCollisionType enum
    int32_t beingType = 0;            // BeingType enum
    float patrolSpeed = 5.0f;

    // Flags
    uint32_t flags = BOF_VISIBLE;

    // Frozen transform (if BOF_FROZEN_TRANSFORM set)
    glm::vec3 frozenRotation{0};
    glm::vec3 frozenScale{1};

    // Primitive properties (if BOF_IS_PRIMITIVE set)
    int32_t primitiveType = 0;
    float primitiveSize = 1.0f;
    float primitiveRadius = 0.5f;
    float primitiveHeight = 1.0f;
    int32_t primitiveSegments = 16;
    glm::vec4 primitiveColor{0.7f, 0.7f, 0.7f, 1.0f};

    // Door properties (if BOF_IS_DOOR set)
    char doorId[32] = {0};
    char targetDoorId[32] = {0};

    // Model path index for GLB models (-1 if primitive or uses mesh data)
    int32_t modelPathIndex = -1;

    // Target level index in string table for doors (-1 if not a door)
    int32_t targetLevelIndex = -1;

    // Description string table index (-1 if none)
    int32_t descriptionIndex = -1;

    // Reserved (padded to nice boundary)
    uint8_t reserved[4] = {0};
};
// Note: Size may vary slightly by platform due to alignment, but packed pragma ensures consistent layout

// String table entry (for variable-length strings like model paths, target levels)
struct BinaryStringEntry {
    uint64_t offset = 0;          // Offset within string data blob
    uint32_t length = 0;          // Length excluding null terminator
    uint32_t reserved = 0;
};

#pragma pack(pop)

// Magic number validation
inline bool validateBinaryLevelMagic(const char* magic) {
    return magic[0] == 'E' && magic[1] == 'B' && magic[2] == 'I' && magic[3] == 'N';
}

// Current format version
constexpr uint32_t BINARY_LEVEL_VERSION = 1;

} // namespace eden
