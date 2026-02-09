#include "BinaryLevelReader.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <fstream>
#include <cstring>
#include <iostream>
#include <filesystem>

namespace eden {

BinaryLevelData BinaryLevelReader::load(const std::string& filepath) {
    BinaryLevelData result;

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + filepath;
        return result;
    }

    // Read header
    BinaryLevelHeader header;
    if (!readHeader(file, header)) {
        result.error = "Invalid header in: " + filepath;
        return result;
    }

    // Calculate string table offset (after objects, before mesh data)
    uint64_t stringTableOffset = header.objectTableOffset +
                                  header.objectCount * sizeof(BinaryObjectEntry);

    // Read string table
    if (!readStrings(file, stringTableOffset)) {
        result.error = "Failed to read string table";
        return result;
    }

    // Read meshes
    if (!readMeshes(file, header, result)) {
        return result;
    }

    // Read textures
    if (!readTextures(file, header, result)) {
        return result;
    }

    // Read objects
    if (!readObjects(file, header, result)) {
        return result;
    }

    result.success = true;
    result.totalMeshDataSize = header.meshDataSize;
    result.totalTextureDataSize = header.textureDataSize;

    std::cout << "BinaryLevelReader: Loaded " << filepath << std::endl;
    std::cout << "  Meshes: " << result.meshes.size()
              << ", Textures: " << result.textures.size()
              << ", Objects: " << result.objects.size() << std::endl;
    std::cout << "  Mesh data: " << (result.totalMeshDataSize / 1024.0) << " KB"
              << ", Texture data: " << (result.totalTextureDataSize / 1024.0) << " KB" << std::endl;

    return result;
}

bool BinaryLevelReader::exists(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    BinaryLevelHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good()) {
        return false;
    }

    // Validate magic and version
    if (!validateBinaryLevelMagic(header.magic)) {
        return false;
    }

    if (header.version > BINARY_LEVEL_VERSION) {
        return false;
    }

    return true;
}

std::string BinaryLevelReader::getBinaryPath(const std::string& edenPath) {
    // Replace .eden with .edenbin
    std::filesystem::path path(edenPath);
    path.replace_extension(".edenbin");
    return path.string();
}

bool BinaryLevelReader::readHeader(std::ifstream& file, BinaryLevelHeader& header) {
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good()) {
        return false;
    }

    // Validate magic
    if (!validateBinaryLevelMagic(header.magic)) {
        std::cerr << "BinaryLevelReader: Invalid magic number" << std::endl;
        return false;
    }

    // Check version
    if (header.version > BINARY_LEVEL_VERSION) {
        std::cerr << "BinaryLevelReader: Unsupported version " << header.version
                  << " (max supported: " << BINARY_LEVEL_VERSION << ")" << std::endl;
        return false;
    }

    return true;
}

bool BinaryLevelReader::readMeshes(std::ifstream& file, const BinaryLevelHeader& header, BinaryLevelData& data) {
    if (header.meshCount == 0) {
        return true;
    }

    // Read mesh entries
    file.seekg(header.meshTableOffset);
    std::vector<BinaryMeshEntry> meshEntries(header.meshCount);
    file.read(reinterpret_cast<char*>(meshEntries.data()),
              header.meshCount * sizeof(BinaryMeshEntry));
    if (!file.good()) {
        data.error = "Failed to read mesh table";
        return false;
    }

    // Read mesh data for each entry
    data.meshes.resize(header.meshCount);
    for (size_t i = 0; i < header.meshCount; ++i) {
        const auto& entry = meshEntries[i];
        auto& mesh = data.meshes[i];

        // Read vertices
        mesh.vertices.resize(entry.vertexCount);
        file.seekg(header.meshDataOffset + entry.vertexOffset);
        file.read(reinterpret_cast<char*>(mesh.vertices.data()),
                  entry.vertexCount * sizeof(ModelVertex));
        if (!file.good()) {
            data.error = "Failed to read vertex data for mesh " + std::to_string(i);
            return false;
        }

        // Read indices
        mesh.indices.resize(entry.indexCount);
        file.seekg(header.meshDataOffset + entry.indexOffset);
        file.read(reinterpret_cast<char*>(mesh.indices.data()),
                  entry.indexCount * sizeof(uint32_t));
        if (!file.good()) {
            data.error = "Failed to read index data for mesh " + std::to_string(i);
            return false;
        }

        // Copy bounds
        mesh.bounds.min = entry.boundsMin;
        mesh.bounds.max = entry.boundsMax;
        mesh.textureId = entry.textureId;
    }

    return true;
}

bool BinaryLevelReader::readTextures(std::ifstream& file, const BinaryLevelHeader& header, BinaryLevelData& data) {
    if (header.textureCount == 0) {
        return true;
    }

    // Read texture entries
    file.seekg(header.textureTableOffset);
    std::vector<BinaryTextureEntry> texEntries(header.textureCount);
    file.read(reinterpret_cast<char*>(texEntries.data()),
              header.textureCount * sizeof(BinaryTextureEntry));
    if (!file.good()) {
        data.error = "Failed to read texture table";
        return false;
    }

    // Read texture data for each entry
    data.textures.resize(header.textureCount);
    for (size_t i = 0; i < header.textureCount; ++i) {
        const auto& entry = texEntries[i];
        auto& tex = data.textures[i];

        tex.width = entry.width;
        tex.height = entry.height;
        tex.pixels.resize(entry.dataSize);

        file.seekg(header.textureDataOffset + entry.dataOffset);
        file.read(reinterpret_cast<char*>(tex.pixels.data()), entry.dataSize);
        if (!file.good()) {
            data.error = "Failed to read texture data for texture " + std::to_string(i);
            return false;
        }
    }

    return true;
}

bool BinaryLevelReader::readObjects(std::ifstream& file, const BinaryLevelHeader& header, BinaryLevelData& data) {
    if (header.objectCount == 0) {
        return true;
    }

    // Read object entries
    file.seekg(header.objectTableOffset);
    std::vector<BinaryObjectEntry> objEntries(header.objectCount);
    file.read(reinterpret_cast<char*>(objEntries.data()),
              header.objectCount * sizeof(BinaryObjectEntry));
    if (!file.good()) {
        data.error = "Failed to read object table";
        return false;
    }

    // Convert entries to object data
    data.objects.resize(header.objectCount);
    for (size_t i = 0; i < header.objectCount; ++i) {
        const auto& entry = objEntries[i];
        auto& obj = data.objects[i];

        obj.name = std::string(entry.name);
        obj.meshId = entry.meshId;

        // Transform
        obj.position = entry.position;
        obj.rotation = entry.rotation;
        obj.scale = entry.scale;

        // Color adjustments
        obj.hueShift = entry.hueShift;
        obj.saturation = entry.saturation;
        obj.brightness = entry.brightness;

        // Collision
        obj.bulletCollisionType = entry.bulletCollisionType;
        obj.beingType = entry.beingType;
        obj.patrolSpeed = entry.patrolSpeed;

        // Decode flags
        obj.visible = (entry.flags & BOF_VISIBLE) != 0;
        obj.aabbCollision = (entry.flags & BOF_AABB_COLLISION) != 0;
        obj.polygonCollision = (entry.flags & BOF_POLY_COLLISION) != 0;
        obj.kinematicPlatform = (entry.flags & BOF_KINEMATIC) != 0;
        obj.hasFrozenTransform = (entry.flags & BOF_FROZEN_TRANSFORM) != 0;
        obj.dailySchedule = (entry.flags & BOF_DAILY_SCHEDULE) != 0;
        obj.isSkinned = (entry.flags & BOF_IS_SKINNED) != 0;
        obj.isPrimitive = (entry.flags & BOF_IS_PRIMITIVE) != 0;
        obj.isDoor = (entry.flags & BOF_IS_DOOR) != 0;

        // Frozen transform
        if (obj.hasFrozenTransform) {
            obj.frozenRotation = entry.frozenRotation;
            obj.frozenScale = entry.frozenScale;
        }

        // Primitive properties
        if (obj.isPrimitive) {
            obj.primitiveType = entry.primitiveType;
            obj.primitiveSize = entry.primitiveSize;
            obj.primitiveRadius = entry.primitiveRadius;
            obj.primitiveHeight = entry.primitiveHeight;
            obj.primitiveSegments = entry.primitiveSegments;
            obj.primitiveColor = entry.primitiveColor;
        }

        // Door properties
        if (obj.isDoor) {
            obj.doorId = std::string(entry.doorId);
            obj.targetDoorId = std::string(entry.targetDoorId);
            // Target level from string table
            if (entry.targetLevelIndex >= 0 && entry.targetLevelIndex < static_cast<int32_t>(m_strings.size())) {
                obj.targetLevel = m_strings[entry.targetLevelIndex];
            }
        }

        // Description from string table
        if (entry.descriptionIndex >= 0 && entry.descriptionIndex < static_cast<int32_t>(m_strings.size())) {
            obj.description = m_strings[entry.descriptionIndex];
        }

        // Model path from string table
        if (entry.modelPathIndex >= 0 && entry.modelPathIndex < static_cast<int32_t>(m_strings.size())) {
            obj.modelPath = m_strings[entry.modelPathIndex];
        }
    }

    return true;
}

bool BinaryLevelReader::readStrings(std::ifstream& file, uint64_t stringTableOffset) {
    file.seekg(stringTableOffset);

    // Read string count
    uint32_t stringCount = 0;
    file.read(reinterpret_cast<char*>(&stringCount), sizeof(stringCount));
    if (!file.good()) {
        return true;  // No strings is OK
    }

    if (stringCount == 0) {
        return true;
    }

    // Read string entries
    std::vector<BinaryStringEntry> entries(stringCount);
    file.read(reinterpret_cast<char*>(entries.data()),
              stringCount * sizeof(BinaryStringEntry));
    if (!file.good()) {
        return false;
    }

    // Record position after entries (string data starts here)
    uint64_t stringDataOffset = file.tellg();

    // Read each string
    m_strings.resize(stringCount);
    for (size_t i = 0; i < stringCount; ++i) {
        const auto& entry = entries[i];
        m_strings[i].resize(entry.length);

        file.seekg(stringDataOffset + entry.offset);
        file.read(m_strings[i].data(), entry.length);
        if (!file.good()) {
            return false;
        }
    }

    return true;
}

} // namespace eden
