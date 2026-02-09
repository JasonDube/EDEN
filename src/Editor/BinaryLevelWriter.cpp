#include "BinaryLevelWriter.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <fstream>
#include <cstring>
#include <iostream>
#include <functional>

namespace eden {

int32_t BinaryLevelWriter::addMesh(const std::vector<ModelVertex>& vertices,
                                    const std::vector<uint32_t>& indices,
                                    const AABB& bounds,
                                    const unsigned char* textureData,
                                    int texWidth,
                                    int texHeight) {
    if (vertices.empty() || indices.empty()) {
        return -1;
    }

    // Compute hash for deduplication (includes texture so painted objects stay unique)
    uint64_t hash = computeMeshHash(vertices, indices);
    if (textureData && texWidth > 0 && texHeight > 0) {
        size_t texSize = texWidth * texHeight * 4;
        uint64_t texHash = std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(textureData), texSize));
        hash ^= texHash * 1099511628211ULL;
    }

    // Check if we already have this exact mesh + texture combination
    auto it = m_meshHashMap.find(hash);
    if (it != m_meshHashMap.end()) {
        return it->second;
    }

    // Create new mesh entry
    BinaryMeshEntry entry;
    entry.vertexOffset = m_meshDataBlob.size();
    entry.vertexCount = static_cast<uint32_t>(vertices.size());
    entry.vertexStride = sizeof(ModelVertex);

    // Append vertex data
    size_t vertexDataSize = vertices.size() * sizeof(ModelVertex);
    size_t oldSize = m_meshDataBlob.size();
    m_meshDataBlob.resize(oldSize + vertexDataSize);
    std::memcpy(m_meshDataBlob.data() + oldSize, vertices.data(), vertexDataSize);

    // Append index data (aligned to 4 bytes)
    while (m_meshDataBlob.size() % 4 != 0) {
        m_meshDataBlob.push_back(0);
    }

    entry.indexOffset = m_meshDataBlob.size();
    entry.indexCount = static_cast<uint32_t>(indices.size());
    entry.indexType = sizeof(uint32_t);

    size_t indexDataSize = indices.size() * sizeof(uint32_t);
    oldSize = m_meshDataBlob.size();
    m_meshDataBlob.resize(oldSize + indexDataSize);
    std::memcpy(m_meshDataBlob.data() + oldSize, indices.data(), indexDataSize);

    // Store bounds
    entry.boundsMin = bounds.min;
    entry.boundsMax = bounds.max;

    // Handle texture
    if (textureData && texWidth > 0 && texHeight > 0) {
        // Compute texture hash for deduplication
        size_t texSize = texWidth * texHeight * 4;
        uint64_t texHash = std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(textureData), texSize));

        auto texIt = m_textureHashMap.find(texHash);
        if (texIt != m_textureHashMap.end()) {
            entry.textureId = texIt->second;
        } else {
            // Create new texture entry
            BinaryTextureEntry texEntry;
            texEntry.dataOffset = m_textureDataBlob.size();
            texEntry.dataSize = static_cast<uint32_t>(texSize);
            texEntry.width = texWidth;
            texEntry.height = texHeight;
            texEntry.format = 0;  // RGBA8

            // Append texture data
            size_t oldTexSize = m_textureDataBlob.size();
            m_textureDataBlob.resize(oldTexSize + texSize);
            std::memcpy(m_textureDataBlob.data() + oldTexSize, textureData, texSize);

            entry.textureId = static_cast<int32_t>(m_textureEntries.size());
            m_textureHashMap[texHash] = entry.textureId;
            m_textureEntries.push_back(texEntry);
        }
    } else {
        entry.textureId = -1;
    }

    int32_t meshId = static_cast<int32_t>(m_meshEntries.size());
    m_meshHashMap[hash] = meshId;
    m_meshEntries.push_back(entry);

    return meshId;
}

void BinaryLevelWriter::addObject(const SceneObject& obj, int32_t meshId, const std::string& modelPath) {
    BinaryObjectEntry entry;

    entry.meshId = meshId;

    // Copy name (truncate if necessary)
    const std::string& name = obj.getName();
    size_t copyLen = std::min(name.size(), sizeof(entry.name) - 1);
    std::memcpy(entry.name, name.c_str(), copyLen);
    entry.name[copyLen] = '\0';

    // Transform
    entry.position = obj.getTransform().getPosition();
    entry.rotation = obj.getEulerRotation();
    entry.scale = obj.getTransform().getScale();

    // Color adjustments
    entry.hueShift = obj.getHueShift();
    entry.saturation = obj.getSaturation();
    entry.brightness = obj.getBrightness();

    // Collision
    entry.bulletCollisionType = static_cast<int32_t>(obj.getBulletCollisionType());
    entry.beingType = static_cast<int32_t>(obj.getBeingType());
    entry.patrolSpeed = obj.getPatrolSpeed();

    // Flags
    entry.flags = BOF_NONE;
    if (obj.isVisible()) entry.flags |= BOF_VISIBLE;
    if (obj.hasAABBCollision()) entry.flags |= BOF_AABB_COLLISION;
    if (obj.hasPolygonCollision()) entry.flags |= BOF_POLY_COLLISION;
    if (obj.isKinematicPlatform()) entry.flags |= BOF_KINEMATIC;
    if (obj.hasFrozenTransform()) entry.flags |= BOF_FROZEN_TRANSFORM;
    if (obj.hasDailySchedule()) entry.flags |= BOF_DAILY_SCHEDULE;
    if (obj.isSkinned()) entry.flags |= BOF_IS_SKINNED;
    if (obj.isPrimitive()) entry.flags |= BOF_IS_PRIMITIVE;
    if (obj.isDoor()) entry.flags |= BOF_IS_DOOR;

    // Frozen transform
    if (obj.hasFrozenTransform()) {
        entry.frozenRotation = obj.getFrozenRotation();
        entry.frozenScale = obj.getFrozenScale();
    }

    // Primitive properties
    if (obj.isPrimitive()) {
        entry.primitiveType = static_cast<int32_t>(obj.getPrimitiveType());
        entry.primitiveSize = obj.getPrimitiveSize();
        entry.primitiveRadius = obj.getPrimitiveRadius();
        entry.primitiveHeight = obj.getPrimitiveHeight();
        entry.primitiveSegments = obj.getPrimitiveSegments();
        entry.primitiveColor = obj.getPrimitiveColor();
    }

    // Door properties
    if (obj.isDoor()) {
        const std::string& doorId = obj.getDoorId();
        copyLen = std::min(doorId.size(), sizeof(entry.doorId) - 1);
        std::memcpy(entry.doorId, doorId.c_str(), copyLen);
        entry.doorId[copyLen] = '\0';

        const std::string& targetDoorId = obj.getTargetDoorId();
        copyLen = std::min(targetDoorId.size(), sizeof(entry.targetDoorId) - 1);
        std::memcpy(entry.targetDoorId, targetDoorId.c_str(), copyLen);
        entry.targetDoorId[copyLen] = '\0';

        // Store target level in string table
        const std::string& targetLevel = obj.getTargetLevel();
        if (!targetLevel.empty()) {
            entry.targetLevelIndex = addString(targetLevel);
        } else {
            entry.targetLevelIndex = -1;
        }
    } else {
        entry.targetLevelIndex = -1;
    }

    // Description (for AI perception)
    const std::string& description = obj.getDescription();
    if (!description.empty()) {
        entry.descriptionIndex = addString(description);
    } else {
        entry.descriptionIndex = -1;
    }

    // Model path (for GLB models that aren't fully baked into the binary)
    if (!modelPath.empty()) {
        entry.modelPathIndex = addString(modelPath);
    } else if (!obj.getModelPath().empty()) {
        entry.modelPathIndex = addString(obj.getModelPath());
    } else {
        entry.modelPathIndex = -1;
    }

    m_objectEntries.push_back(entry);
}

int32_t BinaryLevelWriter::addString(const std::string& str) {
    auto it = m_stringIndex.find(str);
    if (it != m_stringIndex.end()) {
        return it->second;
    }

    int32_t index = static_cast<int32_t>(m_strings.size());
    m_strings.push_back(str);
    m_stringIndex[str] = index;
    return index;
}

bool BinaryLevelWriter::write(const std::string& filepath) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "BinaryLevelWriter: Failed to open file for writing: " << filepath << std::endl;
        return false;
    }

    // Build string data blob
    std::vector<BinaryStringEntry> stringEntries;
    std::vector<uint8_t> stringDataBlob;
    for (const auto& str : m_strings) {
        BinaryStringEntry entry;
        entry.offset = stringDataBlob.size();
        entry.length = static_cast<uint32_t>(str.size());
        stringEntries.push_back(entry);

        // Append string with null terminator
        size_t oldSize = stringDataBlob.size();
        stringDataBlob.resize(oldSize + str.size() + 1);
        std::memcpy(stringDataBlob.data() + oldSize, str.c_str(), str.size() + 1);
    }

    // Calculate offsets
    BinaryLevelHeader header;
    header.meshCount = static_cast<uint32_t>(m_meshEntries.size());
    header.textureCount = static_cast<uint32_t>(m_textureEntries.size());
    header.objectCount = static_cast<uint32_t>(m_objectEntries.size());

    uint64_t currentOffset = sizeof(BinaryLevelHeader);

    // Mesh table
    header.meshTableOffset = currentOffset;
    currentOffset += m_meshEntries.size() * sizeof(BinaryMeshEntry);

    // Texture table
    header.textureTableOffset = currentOffset;
    currentOffset += m_textureEntries.size() * sizeof(BinaryTextureEntry);

    // Object table
    header.objectTableOffset = currentOffset;
    currentOffset += m_objectEntries.size() * sizeof(BinaryObjectEntry);

    // String table (count + entries)
    uint64_t stringTableOffset = currentOffset;
    uint32_t stringCount = static_cast<uint32_t>(stringEntries.size());
    currentOffset += sizeof(uint32_t);  // String count
    currentOffset += stringEntries.size() * sizeof(BinaryStringEntry);

    // String data
    uint64_t stringDataOffset = currentOffset;
    currentOffset += stringDataBlob.size();

    // Align to 16 bytes for mesh data
    while (currentOffset % 16 != 0) currentOffset++;

    // Mesh data blob
    header.meshDataOffset = currentOffset;
    header.meshDataSize = m_meshDataBlob.size();
    currentOffset += m_meshDataBlob.size();

    // Align to 16 bytes for texture data
    while (currentOffset % 16 != 0) currentOffset++;

    // Texture data blob
    header.textureDataOffset = currentOffset;
    header.textureDataSize = m_textureDataBlob.size();

    // Write header
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write mesh table
    if (!m_meshEntries.empty()) {
        file.write(reinterpret_cast<const char*>(m_meshEntries.data()),
                   m_meshEntries.size() * sizeof(BinaryMeshEntry));
    }

    // Write texture table
    if (!m_textureEntries.empty()) {
        file.write(reinterpret_cast<const char*>(m_textureEntries.data()),
                   m_textureEntries.size() * sizeof(BinaryTextureEntry));
    }

    // Write object table
    if (!m_objectEntries.empty()) {
        file.write(reinterpret_cast<const char*>(m_objectEntries.data()),
                   m_objectEntries.size() * sizeof(BinaryObjectEntry));
    }

    // Write string table
    file.write(reinterpret_cast<const char*>(&stringCount), sizeof(stringCount));
    if (!stringEntries.empty()) {
        file.write(reinterpret_cast<const char*>(stringEntries.data()),
                   stringEntries.size() * sizeof(BinaryStringEntry));
    }

    // Write string data
    if (!stringDataBlob.empty()) {
        file.write(reinterpret_cast<const char*>(stringDataBlob.data()), stringDataBlob.size());
    }

    // Align to 16 bytes
    while (file.tellp() % 16 != 0) {
        char zero = 0;
        file.write(&zero, 1);
    }

    // Write mesh data blob
    if (!m_meshDataBlob.empty()) {
        file.write(reinterpret_cast<const char*>(m_meshDataBlob.data()), m_meshDataBlob.size());
    }

    // Align to 16 bytes
    while (file.tellp() % 16 != 0) {
        char zero = 0;
        file.write(&zero, 1);
    }

    // Write texture data blob
    if (!m_textureDataBlob.empty()) {
        file.write(reinterpret_cast<const char*>(m_textureDataBlob.data()), m_textureDataBlob.size());
    }

    file.close();

    std::cout << "BinaryLevelWriter: Wrote " << filepath << std::endl;
    std::cout << "  Meshes: " << m_meshEntries.size()
              << ", Textures: " << m_textureEntries.size()
              << ", Objects: " << m_objectEntries.size() << std::endl;
    std::cout << "  Mesh data: " << (m_meshDataBlob.size() / 1024.0) << " KB"
              << ", Texture data: " << (m_textureDataBlob.size() / 1024.0) << " KB" << std::endl;

    return true;
}

void BinaryLevelWriter::clear() {
    m_meshEntries.clear();
    m_meshDataBlob.clear();
    m_textureEntries.clear();
    m_textureDataBlob.clear();
    m_objectEntries.clear();
    m_strings.clear();
    m_stringIndex.clear();
    m_meshHashMap.clear();
    m_textureHashMap.clear();
}

uint64_t BinaryLevelWriter::computeMeshHash(const std::vector<ModelVertex>& vertices,
                                             const std::vector<uint32_t>& indices) const {
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;

    // Hash vertices
    const uint8_t* vertexData = reinterpret_cast<const uint8_t*>(vertices.data());
    size_t vertexSize = vertices.size() * sizeof(ModelVertex);
    for (size_t i = 0; i < vertexSize; ++i) {
        hash ^= vertexData[i];
        hash *= prime;
    }

    // Hash indices
    const uint8_t* indexData = reinterpret_cast<const uint8_t*>(indices.data());
    size_t indexSize = indices.size() * sizeof(uint32_t);
    for (size_t i = 0; i < indexSize; ++i) {
        hash ^= indexData[i];
        hash *= prime;
    }

    return hash;
}

} // namespace eden
