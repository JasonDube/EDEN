#include "ChunkManager.hpp"
#include <chrono>
#include "Renderer/Buffer.hpp"
#include <iostream>

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

    auto tGen0 = std::chrono::steady_clock::now();
    // Generate all chunk data
    terrain.preloadAllChunks([this, &progressCallback](int loaded, int total) {
        m_chunksLoaded = loaded;
        if (progressCallback) {
            progressCallback(loaded, total);
        }
    });
    auto tGen1 = std::chrono::steady_clock::now();

    // Upload all chunks to GPU. Iterate the chunk set ONCE. The old code called
    // terrain.update(chunkCenter) for every chunk position (1024x on a planet),
    // and each update() rebuilt a ~1089-chunk visible list with wrap math — an
    // O(N^2) sweep (~1.1M iterations) that dominated level-load time. The chunks
    // were already generated above, so just walk them directly.
    int uploaded = 0;
    bool vramExhausted = false;
    for (auto& [coord, chunk] : terrain.getAllChunks()) {
        if (vramExhausted || !chunk->needsUpload()) continue;
        try {
            uploadChunk(*chunk);
            uploaded++;
        } catch (const std::runtime_error& e) {
            int64_t usedMB = Buffer::getVramUsedBytes() / (1024 * 1024);
            std::cerr << "[ChunkManager] VRAM exhausted after uploading "
                      << uploaded << " chunks (" << usedMB << " MB used). "
                      << "Remaining chunks will load on-demand." << std::endl;
            vramExhausted = true;
        }
    }

    auto tUp1 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b){ return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
    if (!vramExhausted) {
        int64_t usedMB = Buffer::getVramUsedBytes() / (1024 * 1024);
        std::cout << "[ChunkManager] All " << uploaded << " chunks uploaded ("
                  << usedMB << " MB VRAM used) — generate " << ms(tGen0, tGen1)
                  << "ms, upload " << ms(tGen1, tUp1) << "ms" << std::endl;
    }

    m_isLoading = false;
}

void ChunkManager::uploadPendingChunks(Terrain& terrain) {
    for (auto& vc : terrain.getVisibleChunks()) {
        if (vc.chunk->needsUpload()) {
            try {
                uploadChunk(*vc.chunk);
            } catch (const std::runtime_error&) {
                // VRAM exhausted — skip this chunk, will retry next frame
                // (other chunks may have been freed by then)
                break;
            }
        }
    }
}

void ChunkManager::beginUploadBatch() { m_bufferManager.beginBatch(); }
void ChunkManager::endUploadBatch()   { m_bufferManager.endBatch(); }

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

void ChunkManager::releaseAllChunkBuffers(Terrain& terrain) {
    // The GPU must be idle before calling this. We free each chunk's mesh
    // buffers directly (rather than deferring) since a resize is a heavy,
    // user-initiated operation, then invalidate the handle so nothing double-frees.
    for (auto& [coord, chunk] : terrain.getAllChunks()) {
        uint32_t h = chunk->getBufferHandle();
        if (h != UINT32_MAX) {
            m_bufferManager.destroyMeshBuffers(h);
            chunk->setBufferHandle(UINT32_MAX);
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
