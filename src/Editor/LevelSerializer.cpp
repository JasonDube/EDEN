#include <eden/LevelSerializer.hpp>
#include <eden/Terrain.hpp>
#include <eden/ActionSystem.hpp>
#include "SceneObject.hpp"
#include "AINode.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
#include <filesystem>

using json = nlohmann::json;

namespace eden {

std::string LevelSerializer::s_lastError;

// Base64 encoding table
static const char* BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string LevelSerializer::encodeBase64(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::string result;
    result.reserve((size + 2) / 3 * 4);

    for (size_t i = 0; i < size; i += 3) {
        uint32_t n = bytes[i] << 16;
        if (i + 1 < size) n |= bytes[i + 1] << 8;
        if (i + 2 < size) n |= bytes[i + 2];

        result += BASE64_CHARS[(n >> 18) & 0x3F];
        result += BASE64_CHARS[(n >> 12) & 0x3F];
        result += (i + 1 < size) ? BASE64_CHARS[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < size) ? BASE64_CHARS[n & 0x3F] : '=';
    }
    return result;
}

std::vector<uint8_t> LevelSerializer::decodeBase64(const std::string& encoded) {
    std::vector<uint8_t> result;
    result.reserve(encoded.size() * 3 / 4);

    int val = 0, valb = -8;
    for (char c : encoded) {
        if (c == '=') break;

        const char* p = strchr(BASE64_CHARS, c);
        if (!p) continue;

        val = (val << 6) | (p - BASE64_CHARS);
        valb += 6;
        if (valb >= 0) {
            result.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return result;
}

// Helper to convert vec3 to JSON array
static json vec3ToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

static glm::vec3 jsonToVec3(const json& j) {
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

static json vec4ToJson(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

static glm::vec4 jsonToVec4(const json& j) {
    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
}

static json uvec4ToJson(const glm::uvec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

static glm::uvec4 jsonToUVec4(const json& j) {
    return glm::uvec4(j[0].get<uint32_t>(), j[1].get<uint32_t>(), j[2].get<uint32_t>(), j[3].get<uint32_t>());
}

static json ivec2ToJson(const glm::ivec2& v) {
    return json::array({v.x, v.y});
}

static glm::ivec2 jsonToIVec2(const json& j) {
    return glm::ivec2(j[0].get<int>(), j[1].get<int>());
}

// ActionType string conversion (for robust serialization)
static std::string actionTypeToString(ActionType type) {
    switch (type) {
        case ActionType::ROTATE:       return "ROTATE";
        case ActionType::ROTATE_TO:    return "ROTATE_TO";
        case ActionType::TURN_TO:      return "TURN_TO";
        case ActionType::MOVE:         return "MOVE";
        case ActionType::MOVE_TO:      return "MOVE_TO";
        case ActionType::SCALE:        return "SCALE";
        case ActionType::WAIT:         return "WAIT";
        case ActionType::SEND_SIGNAL:  return "SEND_SIGNAL";
        case ActionType::SPAWN_ENTITY: return "SPAWN_ENTITY";
        case ActionType::DESTROY_SELF: return "DESTROY_SELF";
        case ActionType::SET_VISIBLE:  return "SET_VISIBLE";
        case ActionType::SET_PROPERTY: return "SET_PROPERTY";
        case ActionType::PLAY_SOUND:   return "PLAY_SOUND";
        case ActionType::FOLLOW_PATH:  return "FOLLOW_PATH";
        case ActionType::PICKUP:       return "PICKUP";
        case ActionType::PLACE_VERTICAL: return "PLACE_VERTICAL";
        case ActionType::CUSTOM:       return "CUSTOM";
        default:                       return "WAIT";
    }
}

static ActionType stringToActionType(const std::string& str) {
    if (str == "ROTATE")       return ActionType::ROTATE;
    if (str == "ROTATE_TO")    return ActionType::ROTATE_TO;
    if (str == "TURN_TO")      return ActionType::TURN_TO;
    if (str == "MOVE")         return ActionType::MOVE;
    if (str == "MOVE_TO")      return ActionType::MOVE_TO;
    if (str == "SCALE")        return ActionType::SCALE;
    if (str == "WAIT")         return ActionType::WAIT;
    if (str == "SEND_SIGNAL")  return ActionType::SEND_SIGNAL;
    if (str == "SPAWN_ENTITY") return ActionType::SPAWN_ENTITY;
    if (str == "DESTROY_SELF") return ActionType::DESTROY_SELF;
    if (str == "SET_VISIBLE")  return ActionType::SET_VISIBLE;
    if (str == "SET_PROPERTY") return ActionType::SET_PROPERTY;
    if (str == "PLAY_SOUND")   return ActionType::PLAY_SOUND;
    if (str == "FOLLOW_PATH")  return ActionType::FOLLOW_PATH;
    if (str == "PICKUP")       return ActionType::PICKUP;
    if (str == "PLACE_VERTICAL") return ActionType::PLACE_VERTICAL;
    if (str == "CUSTOM")       return ActionType::CUSTOM;
    return ActionType::WAIT;  // Default fallback
}

// Parse action type from JSON (supports both string and legacy int format)
static int parseActionType(const json& actJson) {
    if (actJson.contains("type")) {
        const auto& typeVal = actJson["type"];
        if (typeVal.is_string()) {
            return static_cast<int>(stringToActionType(typeVal.get<std::string>()));
        } else if (typeVal.is_number_integer()) {
            return typeVal.get<int>();
        }
    }
    return static_cast<int>(ActionType::WAIT);
}

// ============================================================================
// Binary Terrain Save/Load
// ============================================================================

bool LevelSerializer::saveTerrainBinary(const std::string& filepath, const Terrain& terrain) {
    auto startTime = std::chrono::high_resolution_clock::now();

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        s_lastError = "Failed to open terrain file for writing: " + filepath;
        return false;
    }

    const auto& allChunks = terrain.getAllChunks();

    // Prepare header
    TerrainFileHeader header;
    header.chunkCount = static_cast<uint32_t>(allChunks.size());
    header.chunkResolution = 64;  // Default resolution

    // Write header
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Calculate chunk table size and reserve space
    std::vector<TerrainChunkEntry> chunkTable;
    chunkTable.reserve(header.chunkCount);

    // Write placeholder chunk table (we'll update it later)
    size_t chunkTableOffset = file.tellp();
    for (uint32_t i = 0; i < header.chunkCount; i++) {
        TerrainChunkEntry entry{};
        file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    // Write each chunk's data and record offsets
    uint32_t chunkIndex = 0;
    for (const auto& [coord, chunk] : allChunks) {
        if (!chunk) continue;

        TerrainChunkEntry entry;
        entry.coordX = coord.x;
        entry.coordY = coord.y;
        entry.dataOffset = file.tellp();

        const auto& vertices = chunk->getVertices();
        int resolution = chunk->getResolution();
        int count = resolution * resolution;

        // Write resolution first
        file.write(reinterpret_cast<const char*>(&resolution), sizeof(int));

        // Extract and write heightmap
        std::vector<float> heightmap(count);
        std::vector<float> paintAlphas(count);
        std::vector<glm::vec3> colors(count);
        std::vector<glm::vec4> texWeights(count);
        std::vector<glm::uvec4> texIndices(count);
        std::vector<glm::vec3> texHSB(count);

        for (int i = 0; i < count; i++) {
            const auto& v = vertices[i];
            heightmap[i] = v.position.y;
            paintAlphas[i] = v.paintAlpha;
            colors[i] = v.color;
            texWeights[i] = v.texWeights;
            texIndices[i] = v.texIndices;
            texHSB[i] = v.texHSB;
        }

        // Write all arrays directly as binary
        file.write(reinterpret_cast<const char*>(heightmap.data()), count * sizeof(float));
        file.write(reinterpret_cast<const char*>(paintAlphas.data()), count * sizeof(float));
        file.write(reinterpret_cast<const char*>(colors.data()), count * sizeof(glm::vec3));
        file.write(reinterpret_cast<const char*>(texWeights.data()), count * sizeof(glm::vec4));
        file.write(reinterpret_cast<const char*>(texIndices.data()), count * sizeof(glm::uvec4));
        file.write(reinterpret_cast<const char*>(texHSB.data()), count * sizeof(glm::vec3));

        entry.dataSize = static_cast<uint64_t>(file.tellp()) - entry.dataOffset;
        chunkTable.push_back(entry);
        chunkIndex++;
    }

    // Go back and write the actual chunk table
    file.seekp(chunkTableOffset);
    for (const auto& entry : chunkTable) {
        file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    file.close();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "[LevelSerializer] Saved terrain binary: " << filepath << std::endl;
    std::cout << "  - " << chunkTable.size() << " chunks in " << duration.count() << "ms" << std::endl;

    return true;
}

bool LevelSerializer::loadTerrainBinary(const std::string& filepath, LevelData& outData) {
    auto startTime = std::chrono::high_resolution_clock::now();

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        s_lastError = "Failed to open terrain file: " + filepath;
        return false;
    }

    // Read header
    TerrainFileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Validate magic
    if (header.magic[0] != 'E' || header.magic[1] != 'D' ||
        header.magic[2] != 'T' || header.magic[3] != 'R') {
        s_lastError = "Invalid terrain file format (bad magic)";
        return false;
    }

    if (header.version != 1) {
        s_lastError = "Unsupported terrain file version: " + std::to_string(header.version);
        return false;
    }

    // Read chunk table
    std::vector<TerrainChunkEntry> chunkTable(header.chunkCount);
    file.read(reinterpret_cast<char*>(chunkTable.data()), header.chunkCount * sizeof(TerrainChunkEntry));

    // Read each chunk
    outData.chunks.clear();
    outData.chunks.reserve(header.chunkCount);

    for (const auto& entry : chunkTable) {
        file.seekg(entry.dataOffset);

        LevelData::ChunkData chunk;
        chunk.coord = glm::ivec2(entry.coordX, entry.coordY);

        // Read resolution
        int resolution;
        file.read(reinterpret_cast<char*>(&resolution), sizeof(int));
        int count = resolution * resolution;

        // Read all arrays
        chunk.heightmap.resize(count);
        chunk.paintAlphamap.resize(count);
        chunk.colormap.resize(count);
        chunk.texWeightmap.resize(count);
        chunk.texIndicesmap.resize(count);
        chunk.texHSBmap.resize(count);

        file.read(reinterpret_cast<char*>(chunk.heightmap.data()), count * sizeof(float));
        file.read(reinterpret_cast<char*>(chunk.paintAlphamap.data()), count * sizeof(float));
        file.read(reinterpret_cast<char*>(chunk.colormap.data()), count * sizeof(glm::vec3));
        file.read(reinterpret_cast<char*>(chunk.texWeightmap.data()), count * sizeof(glm::vec4));
        file.read(reinterpret_cast<char*>(chunk.texIndicesmap.data()), count * sizeof(glm::uvec4));
        file.read(reinterpret_cast<char*>(chunk.texHSBmap.data()), count * sizeof(glm::vec3));

        outData.chunks.push_back(std::move(chunk));
    }

    file.close();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "[LevelSerializer] Loaded terrain binary: " << filepath << std::endl;
    std::cout << "  - " << outData.chunks.size() << " chunks in " << duration.count() << "ms" << std::endl;

    return true;
}

// ============================================================================
// Main Save/Load (JSON + Binary Terrain)
// ============================================================================

bool LevelSerializer::save(const std::string& filepath,
                           const Terrain& terrain,
                           const std::vector<std::unique_ptr<SceneObject>>& objects,
                           const ActionSystem& actionSystem,
                           const std::vector<std::unique_ptr<AINode>>& aiNodes,
                           float waterLevel,
                           bool waterEnabled,
                           const glm::vec3& spawnPosition,
                           const SkyParameters& skyParams,
                           const glm::vec3& editorCameraPos,
                           float editorCameraYaw,
                           float editorCameraPitch,
                           bool isTestLevel,
                           bool isSpaceLevel,
                           int physicsBackend,
                           const std::string& gameModuleName) {
    try {
        json root;

        // Metadata
        root["version"] = 1;
        root["name"] = filepath;  // Could extract filename

        // Global settings
        root["settings"]["waterLevel"] = waterLevel;
        root["settings"]["waterEnabled"] = waterEnabled;
        root["settings"]["spawnPosition"] = vec3ToJson(spawnPosition);
        root["settings"]["editorCameraPos"] = vec3ToJson(editorCameraPos);
        root["settings"]["editorCameraYaw"] = editorCameraYaw;
        root["settings"]["editorCameraPitch"] = editorCameraPitch;
        root["settings"]["isTestLevel"] = isTestLevel;
        root["settings"]["isSpaceLevel"] = isSpaceLevel;
        root["settings"]["physicsBackend"] = static_cast<int>(physicsBackend);
        root["settings"]["gameModuleName"] = gameModuleName;

        // Sky parameters
        json skyJson;
        skyJson["zenithColor"] = vec3ToJson(skyParams.zenithColor);
        skyJson["horizonColor1"] = vec3ToJson(skyParams.horizonColor1);
        skyJson["horizonColor2"] = vec3ToJson(skyParams.horizonColor2);
        skyJson["horizonColor3"] = vec3ToJson(skyParams.horizonColor3);
        skyJson["horizonColor4"] = vec3ToJson(skyParams.horizonColor4);
        skyJson["midSkyColor"] = vec3ToJson(skyParams.midSkyColor);
        skyJson["belowHorizonColor"] = vec3ToJson(skyParams.belowHorizonColor);
        skyJson["nebulaIntensity"] = skyParams.nebulaIntensity;
        skyJson["nebulaScale"] = skyParams.nebulaScale;
        skyJson["nebulaColor1"] = vec3ToJson(skyParams.nebulaColor1);
        skyJson["nebulaColor2"] = vec3ToJson(skyParams.nebulaColor2);
        skyJson["nebulaColor3"] = vec3ToJson(skyParams.nebulaColor3);
        skyJson["starDensity"] = skyParams.starDensity;
        skyJson["starBrightness"] = skyParams.starBrightness;
        skyJson["starSizeMin"] = skyParams.starSizeMin;
        skyJson["starSizeMax"] = skyParams.starSizeMax;
        skyJson["starTwinkle"] = skyParams.starTwinkle;
        skyJson["starColorIntensity"] = skyParams.starColorIntensity;
        skyJson["starWhitePercent"] = skyParams.starWhitePercent;
        skyJson["starBluePercent"] = skyParams.starBluePercent;
        skyJson["starYellowPercent"] = skyParams.starYellowPercent;
        skyJson["starOrangePercent"] = skyParams.starOrangePercent;
        skyJson["starRedPercent"] = skyParams.starRedPercent;
        skyJson["horizonHeight"] = skyParams.horizonHeight;
        root["settings"]["sky"] = skyJson;

        // Save terrain to separate binary file for fast loading
        std::filesystem::path basePath(filepath);
        std::string terrainFilename = basePath.stem().string() + ".terrain";
        std::string terrainPath = (basePath.parent_path() / terrainFilename).string();

        if (!saveTerrainBinary(terrainPath, terrain)) {
            // Binary save failed, will be reported in s_lastError
            return false;
        }

        // Reference the terrain file in JSON
        root["terrain"]["binaryFile"] = terrainFilename;
        root["terrain"]["format"] = "binary";
        root["terrain"]["version"] = 1;

        // Scene objects
        json objectsJson = json::array();
        for (const auto& obj : objects) {
            if (!obj) continue;
            // Skip objects with no model path AND no primitive type (procedural spawn markers etc.)
            if (obj->getModelPath().empty() && !obj->isPrimitive()) continue;

            json objJson;
            objJson["name"] = obj->getName();
            objJson["modelPath"] = obj->getModelPath();
            objJson["position"] = vec3ToJson(obj->getTransform().getPosition());
            // Use stored euler angles (avoids gimbal lock issues)
            objJson["rotation"] = vec3ToJson(obj->getEulerRotation());
            objJson["scale"] = vec3ToJson(obj->getTransform().getScale());
            objJson["hueShift"] = obj->getHueShift();
            objJson["saturation"] = obj->getSaturation();
            objJson["brightness"] = obj->getBrightness();
            objJson["visible"] = obj->isVisible();
            objJson["aabbCollision"] = obj->hasAABBCollision();
            objJson["polygonCollision"] = obj->hasPolygonCollision();
            objJson["bulletCollisionType"] = static_cast<int>(obj->getBulletCollisionType());
            objJson["kinematicPlatform"] = obj->isKinematicPlatform();

            // Frozen transform - rotation/scale baked into vertices
            if (obj->hasFrozenTransform()) {
                objJson["frozenTransform"] = true;
                objJson["frozenRotation"] = vec3ToJson(obj->getFrozenRotation());
                objJson["frozenScale"] = vec3ToJson(obj->getFrozenScale());
            }

            objJson["beingType"] = static_cast<int>(obj->getBeingType());
            if (!obj->getGroveScriptPath().empty()) {
                objJson["groveScript"] = obj->getGroveScriptPath();
            }
            objJson["dailySchedule"] = obj->hasDailySchedule();
            objJson["patrolSpeed"] = obj->getPatrolSpeed();
            if (!obj->getDescription().empty()) {
                objJson["description"] = obj->getDescription();
            }

            // Primitive object support
            objJson["primitiveType"] = static_cast<int>(obj->getPrimitiveType());
            if (obj->isPrimitive()) {
                objJson["primitiveSize"] = obj->getPrimitiveSize();
                objJson["primitiveRadius"] = obj->getPrimitiveRadius();
                objJson["primitiveHeight"] = obj->getPrimitiveHeight();
                objJson["primitiveSegments"] = obj->getPrimitiveSegments();
                objJson["primitiveColor"] = vec4ToJson(obj->getPrimitiveColor());
            }

            // Door properties
            if (obj->isDoor()) {
                objJson["doorId"] = obj->getDoorId();
                objJson["targetLevel"] = obj->getTargetLevel();
                objJson["targetDoorId"] = obj->getTargetDoorId();
            }

            // Skinned model support
            objJson["isSkinned"] = obj->isSkinned();
            if (obj->isSkinned()) {
                objJson["currentAnimation"] = obj->getCurrentAnimation();
            }

            // Behaviors
            json behaviorsJson = json::array();
            for (const auto& behavior : obj->getBehaviors()) {
                json behJson;
                behJson["name"] = behavior.name;
                behJson["trigger"] = static_cast<int>(behavior.trigger);
                behJson["triggerParam"] = behavior.triggerParam;
                behJson["triggerRadius"] = behavior.triggerRadius;
                behJson["loop"] = behavior.loop;
                behJson["enabled"] = behavior.enabled;

                // Actions
                json actionsJson = json::array();
                for (const auto& action : behavior.actions) {
                    json actJson;
                    actJson["type"] = actionTypeToString(action.type);
                    actJson["vec3Param"] = vec3ToJson(action.vec3Param);
                    actJson["floatParam"] = action.floatParam;
                    actJson["stringParam"] = action.stringParam;
                    actJson["animationParam"] = action.animationParam;
                    actJson["boolParam"] = action.boolParam;
                    actJson["easing"] = static_cast<int>(action.easing);
                    actJson["duration"] = action.duration;
                    actionsJson.push_back(actJson);
                }
                behJson["actions"] = actionsJson;
                behaviorsJson.push_back(behJson);
            }
            objJson["behaviors"] = behaviorsJson;

            objectsJson.push_back(objJson);
        }
        root["objects"] = objectsJson;

        // Entities
        json entitiesJson = json::array();
        for (const auto& entity : actionSystem.getEntities()) {
            if (!entity) continue;

            json entJson;
            entJson["name"] = entity->getName();
            entJson["position"] = vec3ToJson(entity->getTransform().getPosition());
            glm::vec3 euler = glm::degrees(glm::eulerAngles(entity->getTransform().getRotation()));
            entJson["rotation"] = vec3ToJson(euler);
            entJson["scale"] = vec3ToJson(entity->getTransform().getScale());
            entJson["flags"] = static_cast<uint32_t>(entity->getFlags());
            entJson["modelHandle"] = entity->getModelHandle();

            // Behaviors
            json behaviorsJson = json::array();
            for (const auto& behavior : entity->getBehaviors()) {
                json behJson;
                behJson["name"] = behavior.name;
                behJson["trigger"] = static_cast<int>(behavior.trigger);
                behJson["triggerParam"] = behavior.triggerParam;
                behJson["triggerRadius"] = behavior.triggerRadius;
                behJson["loop"] = behavior.loop;
                behJson["enabled"] = behavior.enabled;

                // Actions
                json actionsJson = json::array();
                for (const auto& action : behavior.actions) {
                    json actJson;
                    actJson["type"] = actionTypeToString(action.type);
                    actJson["vec3Param"] = vec3ToJson(action.vec3Param);
                    actJson["floatParam"] = action.floatParam;
                    actJson["stringParam"] = action.stringParam;
                    actJson["animationParam"] = action.animationParam;
                    actJson["boolParam"] = action.boolParam;
                    actJson["easing"] = static_cast<int>(action.easing);
                    actJson["duration"] = action.duration;
                    actionsJson.push_back(actJson);
                }
                behJson["actions"] = actionsJson;
                behaviorsJson.push_back(behJson);
            }
            entJson["behaviors"] = behaviorsJson;

            // Properties (need to iterate - simplified for now)
            entJson["properties"] = json::object();

            // Tags
            entJson["tags"] = entity->getTags();

            entitiesJson.push_back(entJson);
        }
        root["entities"] = entitiesJson;

        // AI Nodes
        json aiNodesJson = json::array();
        for (const auto& node : aiNodes) {
            if (!node) continue;

            json nodeJson;
            nodeJson["id"] = node->getId();
            nodeJson["name"] = node->getName();
            nodeJson["position"] = vec3ToJson(node->getPosition());
            nodeJson["type"] = static_cast<int>(node->getType());
            nodeJson["radius"] = node->getRadius();
            nodeJson["visible"] = node->isVisible();

            // Connections
            nodeJson["connections"] = node->getConnections();

            // Behaviors
            json behaviorsJson = json::array();
            for (const auto& behavior : node->getBehaviors()) {
                json behJson;
                behJson["name"] = behavior.name;
                behJson["trigger"] = static_cast<int>(behavior.trigger);
                behJson["triggerParam"] = behavior.triggerParam;
                behJson["triggerRadius"] = behavior.triggerRadius;
                behJson["loop"] = behavior.loop;
                behJson["enabled"] = behavior.enabled;

                json actionsJson = json::array();
                for (const auto& action : behavior.actions) {
                    json actJson;
                    actJson["type"] = actionTypeToString(action.type);
                    actJson["vec3Param"] = vec3ToJson(action.vec3Param);
                    actJson["floatParam"] = action.floatParam;
                    actJson["stringParam"] = action.stringParam;
                    actJson["animationParam"] = action.animationParam;
                    actJson["boolParam"] = action.boolParam;
                    actJson["easing"] = static_cast<int>(action.easing);
                    actJson["duration"] = action.duration;
                    actionsJson.push_back(actJson);
                }
                behJson["actions"] = actionsJson;
                behaviorsJson.push_back(behJson);
            }
            nodeJson["behaviors"] = behaviorsJson;

            // Properties
            json propsJson = json::object();
            for (const auto& [key, value] : node->getProperties()) {
                propsJson[key] = value;
            }
            nodeJson["properties"] = propsJson;

            // Tags
            nodeJson["tags"] = node->getTags();

            aiNodesJson.push_back(nodeJson);
        }
        root["aiNodes"] = aiNodesJson;

        // Write to file
        std::ofstream file(filepath);
        if (!file.is_open()) {
            s_lastError = "Failed to open file for writing: " + filepath;
            return false;
        }

        file << root.dump(2);  // Pretty print with 2-space indent
        file.close();

        std::cout << "[LevelSerializer] Saved level to: " << filepath << std::endl;
        std::cout << "  - " << terrain.getAllChunks().size() << " terrain chunks (binary)" << std::endl;
        std::cout << "  - " << objectsJson.size() << " scene objects" << std::endl;
        std::cout << "  - " << entitiesJson.size() << " entities" << std::endl;
        std::cout << "  - " << aiNodesJson.size() << " AI nodes" << std::endl;

        return true;

    } catch (const std::exception& e) {
        s_lastError = std::string("Save failed: ") + e.what();
        return false;
    }
}

bool LevelSerializer::load(const std::string& filepath, LevelData& outData) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            s_lastError = "Failed to open file: " + filepath;
            return false;
        }

        json root = json::parse(file);
        file.close();

        // Metadata
        outData.version = root.value("version", 1);
        outData.name = root.value("name", "");

        // Settings
        if (root.contains("settings")) {
            const auto& settings = root["settings"];
            outData.waterLevel = settings.value("waterLevel", 0.0f);
            outData.waterEnabled = settings.value("waterEnabled", false);
            if (settings.contains("spawnPosition")) {
                outData.spawnPosition = jsonToVec3(settings["spawnPosition"]);
            }
            if (settings.contains("editorCameraPos")) {
                outData.editorCameraPos = jsonToVec3(settings["editorCameraPos"]);
            }
            outData.editorCameraYaw = settings.value("editorCameraYaw", -90.0f);
            outData.editorCameraPitch = settings.value("editorCameraPitch", 0.0f);
            outData.isTestLevel = settings.value("isTestLevel", false);
            outData.isSpaceLevel = settings.value("isSpaceLevel", false);
            outData.physicsBackend = settings.value("physicsBackend", 0);
            outData.gameModuleName = settings.value("gameModuleName", "");

            // Sky parameters
            if (settings.contains("sky")) {
                const auto& sky = settings["sky"];
                if (sky.contains("zenithColor")) outData.skyParams.zenithColor = jsonToVec3(sky["zenithColor"]);
                if (sky.contains("horizonColor1")) outData.skyParams.horizonColor1 = jsonToVec3(sky["horizonColor1"]);
                if (sky.contains("horizonColor2")) outData.skyParams.horizonColor2 = jsonToVec3(sky["horizonColor2"]);
                if (sky.contains("horizonColor3")) outData.skyParams.horizonColor3 = jsonToVec3(sky["horizonColor3"]);
                if (sky.contains("horizonColor4")) outData.skyParams.horizonColor4 = jsonToVec3(sky["horizonColor4"]);
                if (sky.contains("midSkyColor")) outData.skyParams.midSkyColor = jsonToVec3(sky["midSkyColor"]);
                if (sky.contains("belowHorizonColor")) outData.skyParams.belowHorizonColor = jsonToVec3(sky["belowHorizonColor"]);
                outData.skyParams.nebulaIntensity = sky.value("nebulaIntensity", 0.25f);
                outData.skyParams.nebulaScale = sky.value("nebulaScale", 2.5f);
                if (sky.contains("nebulaColor1")) outData.skyParams.nebulaColor1 = jsonToVec3(sky["nebulaColor1"]);
                if (sky.contains("nebulaColor2")) outData.skyParams.nebulaColor2 = jsonToVec3(sky["nebulaColor2"]);
                if (sky.contains("nebulaColor3")) outData.skyParams.nebulaColor3 = jsonToVec3(sky["nebulaColor3"]);
                outData.skyParams.starDensity = sky.value("starDensity", 0.08f);
                outData.skyParams.starBrightness = sky.value("starBrightness", 1.0f);
                outData.skyParams.starSizeMin = sky.value("starSizeMin", 0.5f);
                outData.skyParams.starSizeMax = sky.value("starSizeMax", 2.5f);
                outData.skyParams.starTwinkle = sky.value("starTwinkle", 0.0f);
                outData.skyParams.starColorIntensity = sky.value("starColorIntensity", 0.7f);
                outData.skyParams.starWhitePercent = sky.value("starWhitePercent", 40.0f);
                outData.skyParams.starBluePercent = sky.value("starBluePercent", 15.0f);
                outData.skyParams.starYellowPercent = sky.value("starYellowPercent", 25.0f);
                outData.skyParams.starOrangePercent = sky.value("starOrangePercent", 15.0f);
                outData.skyParams.starRedPercent = sky.value("starRedPercent", 5.0f);
                outData.skyParams.horizonHeight = sky.value("horizonHeight", 0.25f);
            }
        }

        // Terrain - check for binary format first, fall back to legacy base64 JSON
        outData.chunks.clear();
        if (root.contains("terrain")) {
            const auto& terrain = root["terrain"];

            // New binary format
            if (terrain.contains("format") && terrain["format"] == "binary" && terrain.contains("binaryFile")) {
                std::filesystem::path basePath(filepath);
                std::string terrainFilename = terrain["binaryFile"].get<std::string>();
                std::string terrainPath = (basePath.parent_path() / terrainFilename).string();

                if (!loadTerrainBinary(terrainPath, outData)) {
                    // Binary load failed, error already set
                    return false;
                }
            }
            // Legacy base64 JSON format (backwards compatibility)
            else if (terrain.contains("chunks")) {
                std::cout << "[LevelSerializer] Loading legacy base64 terrain format..." << std::endl;
                for (const auto& chunkJson : terrain["chunks"]) {
                    LevelData::ChunkData chunk;
                    chunk.coord = jsonToIVec2(chunkJson["coord"]);

                    int resolution = chunkJson.value("resolution", 64);
                    int count = resolution * resolution;

                    // Decode heightmap
                    auto heightBytes = decodeBase64(chunkJson["heightmap"].get<std::string>());
                    chunk.heightmap.resize(count);
                    std::memcpy(chunk.heightmap.data(), heightBytes.data(),
                               std::min(heightBytes.size(), count * sizeof(float)));

                    // Decode paint alphas
                    auto paintBytes = decodeBase64(chunkJson["paintAlphas"].get<std::string>());
                    chunk.paintAlphamap.resize(count);
                    std::memcpy(chunk.paintAlphamap.data(), paintBytes.data(),
                               std::min(paintBytes.size(), count * sizeof(float)));

                    // Decode colors
                    auto colorBytes = decodeBase64(chunkJson["colors"].get<std::string>());
                    chunk.colormap.resize(count);
                    std::memcpy(chunk.colormap.data(), colorBytes.data(),
                               std::min(colorBytes.size(), count * sizeof(glm::vec3)));

                    // Decode tex weights
                    auto texWeightBytes = decodeBase64(chunkJson["texWeights"].get<std::string>());
                    chunk.texWeightmap.resize(count);
                    std::memcpy(chunk.texWeightmap.data(), texWeightBytes.data(),
                               std::min(texWeightBytes.size(), count * sizeof(glm::vec4)));

                    // Decode tex indices
                    auto texIndexBytes = decodeBase64(chunkJson["texIndices"].get<std::string>());
                    chunk.texIndicesmap.resize(count);
                    std::memcpy(chunk.texIndicesmap.data(), texIndexBytes.data(),
                               std::min(texIndexBytes.size(), count * sizeof(glm::uvec4)));

                    // Decode tex HSB
                    auto texHSBBytes = decodeBase64(chunkJson["texHSB"].get<std::string>());
                    chunk.texHSBmap.resize(count);
                    std::memcpy(chunk.texHSBmap.data(), texHSBBytes.data(),
                               std::min(texHSBBytes.size(), count * sizeof(glm::vec3)));

                    outData.chunks.push_back(chunk);
                }
            }
        }

        // Scene objects
        outData.objects.clear();
        if (root.contains("objects")) {
            for (const auto& objJson : root["objects"]) {
                LevelData::ObjectData obj;
                obj.name = objJson.value("name", "");
                obj.modelPath = objJson.value("modelPath", "");
                obj.position = jsonToVec3(objJson["position"]);
                obj.rotation = jsonToVec3(objJson["rotation"]);
                obj.scale = jsonToVec3(objJson["scale"]);
                obj.hueShift = objJson.value("hueShift", 0.0f);
                obj.saturation = objJson.value("saturation", 1.0f);
                obj.brightness = objJson.value("brightness", 1.0f);
                obj.visible = objJson.value("visible", true);
                // Support both new (aabbCollision/polygonCollision) and legacy (hasCollision) formats
                if (objJson.contains("aabbCollision")) {
                    obj.aabbCollision = objJson.value("aabbCollision", true);
                    obj.polygonCollision = objJson.value("polygonCollision", false);
                } else {
                    // Legacy format: hasCollision maps to aabbCollision
                    obj.aabbCollision = objJson.value("hasCollision", true);
                    obj.polygonCollision = false;
                }
                obj.bulletCollisionType = objJson.value("bulletCollisionType", 0);
                obj.kinematicPlatform = objJson.value("kinematicPlatform", false);

                // Frozen transform - rotation/scale baked into vertices
                obj.frozenTransform = objJson.value("frozenTransform", false);
                if (obj.frozenTransform) {
                    obj.frozenRotation = jsonToVec3(objJson["frozenRotation"]);
                    obj.frozenScale = jsonToVec3(objJson["frozenScale"]);
                }

                obj.beingType = objJson.value("beingType", 0);
                obj.groveScript = objJson.value("groveScript", std::string(""));
                obj.dailySchedule = objJson.value("dailySchedule", false);
                obj.patrolSpeed = objJson.value("patrolSpeed", 5.0f);
                obj.description = objJson.value("description", std::string(""));

                // Primitive object support
                obj.primitiveType = objJson.value("primitiveType", 0);
                if (obj.primitiveType != 0) {
                    obj.primitiveSize = objJson.value("primitiveSize", 1.0f);
                    obj.primitiveRadius = objJson.value("primitiveRadius", 0.5f);
                    obj.primitiveHeight = objJson.value("primitiveHeight", 1.0f);
                    obj.primitiveSegments = objJson.value("primitiveSegments", 16);
                    if (objJson.contains("primitiveColor")) {
                        obj.primitiveColor = jsonToVec4(objJson["primitiveColor"]);
                    }
                }

                // Door properties
                obj.doorId = objJson.value("doorId", "");
                obj.targetLevel = objJson.value("targetLevel", "");
                obj.targetDoorId = objJson.value("targetDoorId", "");

                // Skinned model support
                obj.isSkinned = objJson.value("isSkinned", false);
                obj.currentAnimation = objJson.value("currentAnimation", "");

                // Behaviors
                if (objJson.contains("behaviors")) {
                    for (const auto& behJson : objJson["behaviors"]) {
                        LevelData::BehaviorData beh;
                        beh.name = behJson.value("name", "");
                        beh.trigger = behJson.value("trigger", 0);
                        beh.triggerParam = behJson.value("triggerParam", "");
                        beh.triggerRadius = behJson.value("triggerRadius", 5.0f);
                        beh.loop = behJson.value("loop", false);
                        beh.enabled = behJson.value("enabled", true);

                        // Actions
                        if (behJson.contains("actions")) {
                            for (const auto& actJson : behJson["actions"]) {
                                LevelData::ActionData act;
                                act.type = parseActionType(actJson);
                                act.vec3Param = jsonToVec3(actJson["vec3Param"]);
                                act.floatParam = actJson.value("floatParam", 0.0f);
                                act.stringParam = actJson.value("stringParam", "");
                                act.animationParam = actJson.value("animationParam", "");
                                act.boolParam = actJson.value("boolParam", false);
                                act.easing = actJson.value("easing", 0);
                                act.duration = actJson.value("duration", 0.0f);
                                beh.actions.push_back(act);
                            }
                        }
                        obj.behaviors.push_back(beh);
                    }
                }

                outData.objects.push_back(obj);
            }
        }

        // Entities
        outData.entities.clear();
        if (root.contains("entities")) {
            for (const auto& entJson : root["entities"]) {
                LevelData::EntityData ent;
                ent.name = entJson.value("name", "");
                ent.position = jsonToVec3(entJson["position"]);
                ent.rotation = jsonToVec3(entJson["rotation"]);
                ent.scale = jsonToVec3(entJson["scale"]);
                ent.flags = entJson.value("flags", 0u);
                ent.modelHandle = entJson.value("modelHandle", UINT32_MAX);

                // Behaviors
                if (entJson.contains("behaviors")) {
                    for (const auto& behJson : entJson["behaviors"]) {
                        LevelData::BehaviorData beh;
                        beh.name = behJson.value("name", "");
                        beh.trigger = behJson.value("trigger", 0);
                        beh.triggerParam = behJson.value("triggerParam", "");
                        beh.triggerRadius = behJson.value("triggerRadius", 5.0f);
                        beh.loop = behJson.value("loop", false);
                        beh.enabled = behJson.value("enabled", true);

                        // Actions
                        if (behJson.contains("actions")) {
                            for (const auto& actJson : behJson["actions"]) {
                                LevelData::ActionData act;
                                act.type = parseActionType(actJson);
                                act.vec3Param = jsonToVec3(actJson["vec3Param"]);
                                act.floatParam = actJson.value("floatParam", 0.0f);
                                act.stringParam = actJson.value("stringParam", "");
                                act.animationParam = actJson.value("animationParam", "");
                                act.boolParam = actJson.value("boolParam", false);
                                act.easing = actJson.value("easing", 0);
                                act.duration = actJson.value("duration", 0.0f);
                                beh.actions.push_back(act);
                            }
                        }
                        ent.behaviors.push_back(beh);
                    }
                }

                // Tags
                if (entJson.contains("tags")) {
                    for (const auto& tag : entJson["tags"]) {
                        ent.tags.push_back(tag.get<std::string>());
                    }
                }

                outData.entities.push_back(ent);
            }
        }

        // AI Nodes
        outData.aiNodes.clear();
        if (root.contains("aiNodes")) {
            for (const auto& nodeJson : root["aiNodes"]) {
                LevelData::AINodeData node;
                node.id = nodeJson.value("id", 0u);
                node.name = nodeJson.value("name", "");
                node.position = jsonToVec3(nodeJson["position"]);
                node.type = nodeJson.value("type", 0);
                node.radius = nodeJson.value("radius", 5.0f);
                node.visible = nodeJson.value("visible", true);

                // Connections
                if (nodeJson.contains("connections")) {
                    for (const auto& conn : nodeJson["connections"]) {
                        node.connections.push_back(conn.get<uint32_t>());
                    }
                }

                // Behaviors
                if (nodeJson.contains("behaviors")) {
                    for (const auto& behJson : nodeJson["behaviors"]) {
                        LevelData::BehaviorData beh;
                        beh.name = behJson.value("name", "");
                        beh.trigger = behJson.value("trigger", 0);
                        beh.triggerParam = behJson.value("triggerParam", "");
                        beh.triggerRadius = behJson.value("triggerRadius", 5.0f);
                        beh.loop = behJson.value("loop", false);
                        beh.enabled = behJson.value("enabled", true);

                        if (behJson.contains("actions")) {
                            for (const auto& actJson : behJson["actions"]) {
                                LevelData::ActionData act;
                                act.type = parseActionType(actJson);
                                act.vec3Param = jsonToVec3(actJson["vec3Param"]);
                                act.floatParam = actJson.value("floatParam", 0.0f);
                                act.stringParam = actJson.value("stringParam", "");
                                act.animationParam = actJson.value("animationParam", "");
                                act.boolParam = actJson.value("boolParam", false);
                                act.easing = actJson.value("easing", 0);
                                act.duration = actJson.value("duration", 0.0f);
                                beh.actions.push_back(act);
                            }
                        }
                        node.behaviors.push_back(beh);
                    }
                }

                // Properties
                if (nodeJson.contains("properties")) {
                    for (auto& [key, value] : nodeJson["properties"].items()) {
                        node.properties.emplace_back(key, value.get<float>());
                    }
                }

                // Tags
                if (nodeJson.contains("tags")) {
                    for (const auto& tag : nodeJson["tags"]) {
                        node.tags.push_back(tag.get<std::string>());
                    }
                }

                outData.aiNodes.push_back(node);
            }
        }

        std::cout << "[LevelSerializer] Loaded level: " << filepath << std::endl;
        std::cout << "  - " << outData.chunks.size() << " terrain chunks" << std::endl;
        std::cout << "  - " << outData.objects.size() << " scene objects" << std::endl;
        std::cout << "  - " << outData.entities.size() << " entities" << std::endl;
        std::cout << "  - " << outData.aiNodes.size() << " AI nodes" << std::endl;

        // Zone data
        if (root.contains("zones")) {
            const auto& zonesJson = root["zones"];
            outData.zoneData.hasData = true;
            outData.zoneData.worldMinX = zonesJson.value("worldMinX", -2016.0f);
            outData.zoneData.worldMinZ = zonesJson.value("worldMinZ", -2016.0f);
            outData.zoneData.worldMaxX = zonesJson.value("worldMaxX", 2016.0f);
            outData.zoneData.worldMaxZ = zonesJson.value("worldMaxZ", 2016.0f);
            outData.zoneData.cellSize = zonesJson.value("cellSize", 32.0f);
            outData.zoneData.gridWidth = zonesJson.value("gridWidth", 0);
            outData.zoneData.gridHeight = zonesJson.value("gridHeight", 0);

            if (zonesJson.contains("cells")) {
                for (const auto& c : zonesJson["cells"]) {
                    LevelData::ZoneCellEntry entry;
                    entry.gridX = c.value("x", 0);
                    entry.gridZ = c.value("z", 0);
                    entry.type = c.value("type", 0);
                    entry.resource = c.value("resource", 0);
                    entry.ownerId = c.value("owner", 0u);
                    entry.price = c.value("price", 100.0f);
                    entry.resourceDensity = c.value("density", 0.0f);
                    outData.zoneData.cells.push_back(entry);
                }
            }
            std::cout << "  - " << outData.zoneData.cells.size() << " zone cells" << std::endl;
        }

        return true;

    } catch (const std::exception& e) {
        s_lastError = std::string("Load failed: ") + e.what();
        return false;
    }
}

void LevelSerializer::applyToTerrain(const LevelData& data, Terrain& terrain) {
    int appliedCount = 0;

    for (const auto& chunkData : data.chunks) {
        auto chunk = terrain.getChunkByCoord(chunkData.coord);
        if (!chunk) {
            // Chunk doesn't exist yet - this could happen if terrain bounds changed
            std::cout << "[LevelSerializer] Warning: chunk (" << chunkData.coord.x
                      << ", " << chunkData.coord.y << ") not found in terrain" << std::endl;
            continue;
        }

        chunk->setChunkData(
            chunkData.heightmap,
            chunkData.colormap,
            chunkData.paintAlphamap,
            chunkData.texWeightmap,
            chunkData.texIndicesmap,
            chunkData.texHSBmap
        );
        appliedCount++;
    }

    std::cout << "[LevelSerializer] Applied data to " << appliedCount << " terrain chunks" << std::endl;
}

} // namespace eden
