#include "ChunkManager.hpp"
#include "Renderer/Buffer.hpp"

namespace eden {

ChunkManager::ChunkManager(BufferManager& bufferManager)
    : m_bufferManager(bufferManager)
{
}

void ChunkManager::preloadAllChunks(Terrain& terrain, LoadProgressCallback progressCallback) {
    const auto& config = terrain.getConfig();
    if (!config.useFixedBounds) {
        return;  // Can't preload infinite terrain
    }

    m_isLoading = true;
    m_totalChunks = terrain.getTotalChunkCount();
    m_chunksLoaded = 0;

    // Generate all chunk data
    terrain.preloadAllChunks([this, &progressCallback](int loaded, int total) {
        m_chunksLoaded = loaded;
        if (progressCallback) {
            progressCallback(loaded, total);
        }
    });

    // Upload all chunks to GPU
    float chunkSize = (config.chunkResolution - 1) * config.tileSize;

    for (int z = config.minChunk.y; z <= config.maxChunk.y; z++) {
        for (int x = config.minChunk.x; x <= config.maxChunk.x; x++) {
            glm::vec3 chunkCenter((x + 0.5f) * chunkSize, 0, (z + 0.5f) * chunkSize);
            terrain.update(chunkCenter);

            for (auto& vc : terrain.getVisibleChunks()) {
                if (vc.chunk->needsUpload()) {
                    uploadChunk(*vc.chunk);
                }
            }
        }
    }

    m_isLoading = false;
}

void ChunkManager::uploadPendingChunks(Terrain& terrain) {
    for (auto& vc : terrain.getVisibleChunks()) {
        if (vc.chunk->needsUpload()) {
            uploadChunk(*vc.chunk);
        }
    }
}

void ChunkManager::uploadChunk(TerrainChunk& chunk) {
    const auto& vertices = chunk.getVertices();
    const auto& indices = chunk.getIndices();

    // Queue old buffer for deletion
    uint32_t oldHandle = chunk.getBufferHandle();
    if (oldHandle != UINT32_MAX) {
        m_pendingDeletes.push_back({oldHandle, MAX_FRAMES_IN_FLIGHT});
    }

    // Create new buffer
    uint32_t handle = m_bufferManager.createMeshBuffers(
        vertices.data(),
        static_cast<uint32_t>(vertices.size()),
        sizeof(Vertex3D),
        indices.data(),
        static_cast<uint32_t>(indices.size())
    );

    chunk.setBufferHandle(handle);
    chunk.markUploaded();
}

void ChunkManager::updateModifiedChunks(Terrain& terrain) {
    for (auto& vc : terrain.getVisibleChunks()) {
        if (vc.chunk->needsUpload()) {
            vc.chunk->regenerateMesh();
            uploadChunk(*vc.chunk);
        }
    }
}

void ChunkManager::processPendingDeletes() {
    for (auto it = m_pendingDeletes.begin(); it != m_pendingDeletes.end(); ) {
        it->framesRemaining--;
        if (it->framesRemaining <= 0) {
            m_bufferManager.destroyMeshBuffers(it->handle);
            it = m_pendingDeletes.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace eden
