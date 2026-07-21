#include <eden/LevelInstantiator.hpp>

#include <eden/LevelSerializer.hpp>   // LevelData
#include <eden/Terrain.hpp>           // Terrain, TerrainConfig
#include "Editor/ChunkManager.hpp"
#include "Renderer/VulkanContext.hpp"

#include <glm/glm.hpp>
#include <climits>
#include <iostream>

namespace eden {

void LevelInstantiator::applyTerrain(const LevelData& data,
                                     Terrain& terrain,
                                     ChunkManager& chunkManager,
                                     VulkanContext& context) {
    // Restore the terrain's world scale + bounds so the level reloads at the
    // size it was authored — instead of dropping its height pixels into
    // whatever (possibly giant default) terrain is currently active. Only
    // levels saved with this field reconfigure; older levels keep old behavior.
    if (data.hasTerrainConfig) {
        TerrainConfig tcfg = terrain.getConfig();
        tcfg.tileSize        = data.terrainTileSize;
        tcfg.chunkResolution = data.terrainChunkResolution;
        tcfg.heightScale     = data.terrainHeightScale;
        tcfg.useFixedBounds  = data.terrainUseFixedBounds;
        tcfg.minChunk        = data.terrainMinChunk;
        tcfg.maxChunk        = data.terrainMaxChunk;
        tcfg.wrapWorld       = data.terrainWrapWorld;
        tcfg.stretchTexToBounds = data.terrainStretchTex;
        context.waitIdle();
        chunkManager.releaseAllChunkBuffers(terrain);
        terrain.reconfigure(tcfg);
        chunkManager.preloadAllChunks(terrain, nullptr);
    } else if (!data.chunks.empty()) {
        // Old levels (saved before hasTerrainConfig existed) carry no bounds.
        // Derive them from the chunk coords in the file, otherwise loading an
        // archive while the tiny worktable terrain (1x1 chunk) is active
        // applies 1 of N chunks and the level comes up as a postage stamp.
        glm::ivec2 mn(INT_MAX), mx(INT_MIN);
        for (const auto& c : data.chunks) {
            mn = glm::min(mn, glm::ivec2(c.coord));
            mx = glm::max(mx, glm::ivec2(c.coord));
        }
        const TerrainConfig& cur = terrain.getConfig();
        bool covers = cur.useFixedBounds &&
                      cur.minChunk.x <= mn.x && cur.minChunk.y <= mn.y &&
                      cur.maxChunk.x >= mx.x && cur.maxChunk.y >= mx.y;
        if (!covers) {
            TerrainConfig tcfg = cur;
            tcfg.useFixedBounds = true;
            tcfg.minChunk = mn;
            tcfg.maxChunk = mx;
            // Classic full-world levels (32x32 at -16..15) were wrap-worlds.
            tcfg.wrapWorld = (mn == glm::ivec2(-16, -16) && mx == glm::ivec2(15, 15));
            std::cout << "[LevelInstantiator] Old level without terrain config — derived bounds ("
                      << mn.x << "," << mn.y << ")..(" << mx.x << "," << mx.y << ")" << std::endl;
            context.waitIdle();
            chunkManager.releaseAllChunkBuffers(terrain);
            terrain.reconfigure(tcfg);
            chunkManager.preloadAllChunks(terrain, nullptr);
        }
    }

    LevelSerializer::applyToTerrain(data, terrain);
}

} // namespace eden
