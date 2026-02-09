#pragma once

#include <eden/Terrain.hpp>
#include <vector>
#include <functional>

namespace eden {

class BufferManager;

// Callback for progress updates: (loaded, total)
using LoadProgressCallback = std::function<void(int, int)>;

class ChunkManager {
public:
    ChunkManager(BufferManager& bufferManager);

    // Pre-load all terrain chunks with progress reporting
    void preloadAllChunks(Terrain& terrain, LoadProgressCallback progressCallback = nullptr);

    // Upload any chunks that need uploading
    void uploadPendingChunks(Terrain& terrain);

    // Upload a single chunk
    void uploadChunk(TerrainChunk& chunk);

    // Regenerate and upload modified chunks
    void updateModifiedChunks(Terrain& terrain);

    // Process deferred buffer deletions (call each frame)
    void processPendingDeletes();

    // Get loading state
    bool isLoading() const { return m_isLoading; }
    int getChunksLoaded() const { return m_chunksLoaded; }
    int getTotalChunks() const { return m_totalChunks; }

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

private:
    BufferManager& m_bufferManager;

    // Loading state
    bool m_isLoading = false;
    int m_chunksLoaded = 0;
    int m_totalChunks = 0;

    // Deferred buffer deletion
    struct PendingDelete {
        uint32_t handle;
        int framesRemaining;
    };
    std::vector<PendingDelete> m_pendingDeletes;
};

} // namespace eden
