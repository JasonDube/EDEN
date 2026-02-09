#pragma once

#include "BinaryLevelFormat.hpp"
#include "SceneObject.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace eden {

struct ModelVertex;

// Writes binary level files (.edenbin) for fast loading
class BinaryLevelWriter {
public:
    BinaryLevelWriter() = default;

    // Add a mesh and return its ID (deduplicates by hash)
    // Returns -1 on failure
    int32_t addMesh(const std::vector<ModelVertex>& vertices,
                    const std::vector<uint32_t>& indices,
                    const AABB& bounds,
                    const unsigned char* textureData = nullptr,
                    int texWidth = 0,
                    int texHeight = 0);

    // Add an object referencing a mesh
    void addObject(const SceneObject& obj, int32_t meshId, const std::string& modelPath = "");

    // Add a string to the string table, returns index
    int32_t addString(const std::string& str);

    // Write the binary file
    bool write(const std::string& filepath);

    // Clear all data for reuse
    void clear();

    // Get statistics
    size_t getMeshCount() const { return m_meshEntries.size(); }
    size_t getTextureCount() const { return m_textureEntries.size(); }
    size_t getObjectCount() const { return m_objectEntries.size(); }
    size_t getTotalMeshDataSize() const { return m_meshDataBlob.size(); }
    size_t getTotalTextureDataSize() const { return m_textureDataBlob.size(); }

private:
    // Compute hash for mesh deduplication
    uint64_t computeMeshHash(const std::vector<ModelVertex>& vertices,
                              const std::vector<uint32_t>& indices) const;

    // Mesh entries and data
    std::vector<BinaryMeshEntry> m_meshEntries;
    std::vector<uint8_t> m_meshDataBlob;

    // Texture entries and data
    std::vector<BinaryTextureEntry> m_textureEntries;
    std::vector<uint8_t> m_textureDataBlob;

    // Object entries
    std::vector<BinaryObjectEntry> m_objectEntries;

    // String table
    std::vector<std::string> m_strings;
    std::unordered_map<std::string, int32_t> m_stringIndex;

    // Mesh deduplication map (hash -> mesh ID)
    std::unordered_map<uint64_t, int32_t> m_meshHashMap;

    // Texture deduplication (hash -> texture ID)
    std::unordered_map<uint64_t, int32_t> m_textureHashMap;
};

} // namespace eden
