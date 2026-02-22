#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <eden/SkyParameters.hpp>

namespace eden {

class Terrain;
class ActionSystem;
class SceneObject;
class AINode;

// Level data structure for saving/loading
struct LevelData {
    // Metadata
    std::string name;
    int version = 1;

    // Terrain data per chunk
    struct ChunkData {
        glm::ivec2 coord;
        std::vector<float> heightmap;
        std::vector<glm::vec3> colormap;
        std::vector<float> paintAlphamap;
        std::vector<glm::vec4> texWeightmap;
        std::vector<glm::uvec4> texIndicesmap;
        std::vector<glm::vec3> texHSBmap;
    };
    std::vector<ChunkData> chunks;

    // Shared action data structure (used by behaviors)
    struct ActionData {
        int type;  // ActionType as int
        glm::vec3 vec3Param;
        float floatParam;
        std::string stringParam;
        std::string animationParam;  // Animation to play during action (for skinned models)
        bool boolParam;
        int easing;
        float duration;
    };

    // Shared behavior data structure (used by objects, entities, and AI nodes)
    struct BehaviorData {
        std::string name;
        int trigger;  // TriggerType as int
        std::string triggerParam;
        float triggerRadius;
        bool loop;
        bool enabled;
        std::vector<ActionData> actions;
    };

    // Primitive types for programmatically created objects
    enum class PrimitiveType {
        None = 0,       // GLB model (uses modelPath)
        Cube = 1,
        Cylinder = 2,
        SpawnMarker = 3
    };

    // Scene objects (imported models or primitives)
    struct ObjectData {
        std::string name;           // Object name
        std::string modelPath;      // Path to GLB file (empty for primitives)
        glm::vec3 position;
        glm::vec3 rotation;  // Euler angles in degrees
        glm::vec3 scale;
        float hueShift = 0.0f;
        float saturation = 1.0f;
        float brightness = 1.0f;
        bool visible = true;
        bool aabbCollision = false;    // AABB collision in play mode (default off)
        bool polygonCollision = false; // Polygon collision in play mode (default off)
        int bulletCollisionType = 0;   // BulletCollisionType enum as int (0 = NONE)
        bool kinematicPlatform = false; // Kinematic platform (lift) - moves through Jolt physics

        // Frozen transform - rotation/scale baked into vertices (for correct collision)
        bool frozenTransform = false;
        glm::vec3 frozenRotation{0.0f};
        glm::vec3 frozenScale{1.0f};

        int beingType = 0;  // BeingType enum as int (0 = STATIC)
        std::string groveScript;  // .grove file path for AlgoBot
        bool dailySchedule = false;  // Reset behaviors at midnight for daily routines
        float patrolSpeed = 5.0f;    // Movement speed for FOLLOW_PATH
        std::string description;     // Description visible to AI perception
        std::string buildingType;    // Building catalog type (e.g. "farm")
        std::vector<BehaviorData> behaviors;  // Behaviors for this object

        // Skinned/animated model support
        bool isSkinned = false;
        std::string currentAnimation;

        // Primitive object support
        int primitiveType = 0;      // PrimitiveType as int (0 = None/GLB)
        float primitiveSize = 1.0f; // Size for cube
        float primitiveRadius = 0.5f;   // Radius for cylinder
        float primitiveHeight = 1.0f;   // Height for cylinder
        int primitiveSegments = 16;     // Segments for cylinder
        glm::vec4 primitiveColor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);  // Color for primitive

        // Door properties (for level transitions)
        std::string doorId;
        std::string targetLevel;
        std::string targetDoorId;

    };
    std::vector<ObjectData> objects;

    // Entity data (action system entities)
    struct EntityData {
        std::string name;
        glm::vec3 position;
        glm::vec3 rotation;
        glm::vec3 scale;
        uint32_t flags;
        uint32_t modelHandle;
        std::vector<BehaviorData> behaviors;
        std::vector<std::pair<std::string, float>> properties;
        std::vector<std::string> tags;
    };
    std::vector<EntityData> entities;

    // AI node data
    struct AINodeData {
        uint32_t id;
        std::string name;
        glm::vec3 position;
        int type;           // AINodeType as int
        float radius;
        std::vector<uint32_t> connections;
        std::vector<BehaviorData> behaviors;
        std::vector<std::pair<std::string, float>> properties;
        std::vector<std::string> tags;
        bool visible = true;
    };
    std::vector<AINodeData> aiNodes;

    // Zone data (sparse)
    struct ZoneCellEntry {
        int gridX, gridZ;
        int type;            // ZoneType as int
        int resource;        // ResourceType as int
        uint32_t ownerId;
        float price;
        float resourceDensity;
    };
    struct ZoneData {
        float worldMinX = -2016.0f, worldMinZ = -2016.0f;
        float worldMaxX = 2016.0f, worldMaxZ = 2016.0f;
        float cellSize = 32.0f;
        int gridWidth = 0, gridHeight = 0;
        std::vector<ZoneCellEntry> cells;
        bool hasData = false;
    };
    ZoneData zoneData;

    // Global settings
    float waterLevel = 0.0f;
    bool waterEnabled = false;
    glm::vec3 spawnPosition{0, 0, 0};
    float spawnYaw = -90.0f;  // Camera facing direction
    bool isTestLevel = false;  // Test level mode (no terrain/sky)
    bool isSpaceLevel = false; // Space level mode (no terrain, full-sphere stars)
    int physicsBackend = 0;   // PhysicsBackend enum (0 = Jolt, 1 = Homebrew)

    // Game module to load for play mode
    std::string gameModuleName;

    // Editor camera state (separate from spawn position)
    glm::vec3 editorCameraPos{0, 20, 0};
    float editorCameraYaw = -90.0f;
    float editorCameraPitch = 0.0f;

    // Sky settings
    SkyParameters skyParams;
};

// Binary terrain file format header
struct TerrainFileHeader {
    char magic[4] = {'E', 'D', 'T', 'R'};  // "EDTR" - Eden Terrain
    uint32_t version = 1;
    uint32_t chunkCount = 0;
    uint32_t chunkResolution = 64;
    uint32_t flags = 0;  // Reserved for future use
    uint32_t reserved[3] = {0, 0, 0};
};

// Chunk entry in the binary terrain file
struct TerrainChunkEntry {
    int32_t coordX;
    int32_t coordY;
    uint64_t dataOffset;   // Offset to chunk data in file
    uint64_t dataSize;     // Size of chunk data
};

class LevelSerializer {
public:
    // Save level to .eden file (JSON metadata + binary terrain)
    static bool save(const std::string& filepath,
                     const Terrain& terrain,
                     const std::vector<std::unique_ptr<SceneObject>>& objects,
                     const ActionSystem& actionSystem,
                     const std::vector<std::unique_ptr<AINode>>& aiNodes,
                     float waterLevel,
                     bool waterEnabled,
                     const glm::vec3& spawnPosition,
                     const SkyParameters& skyParams,
                     const glm::vec3& editorCameraPos = glm::vec3(0, 20, 0),
                     float editorCameraYaw = -90.0f,
                     float editorCameraPitch = 0.0f,
                     bool isTestLevel = false,
                     bool isSpaceLevel = false,
                     int physicsBackend = 0,
                     const std::string& gameModuleName = "");

    // Load level from .eden file
    static bool load(const std::string& filepath,
                     LevelData& outData);

    // Apply loaded data to terrain (separate step so we can show progress)
    static void applyToTerrain(const LevelData& data, Terrain& terrain);

    // Get last error message
    static const std::string& getLastError() { return s_lastError; }

private:
    static std::string s_lastError;

    // Binary terrain file operations
    static bool saveTerrainBinary(const std::string& filepath, const Terrain& terrain);
    static bool loadTerrainBinary(const std::string& filepath, LevelData& outData);

    // Base64 encoding for binary data in JSON (kept for backwards compatibility)
    static std::string encodeBase64(const void* data, size_t size);
    static std::vector<uint8_t> decodeBase64(const std::string& encoded);
};

} // namespace eden
