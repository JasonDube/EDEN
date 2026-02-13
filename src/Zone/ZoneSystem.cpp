#include "Zone/ZoneSystem.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

using json = nlohmann::json;

namespace eden {

ZoneSystem::ZoneSystem(float worldMinX, float worldMinZ, float worldMaxX, float worldMaxZ, float cellSize)
    : m_worldMinX(worldMinX), m_worldMinZ(worldMinZ)
    , m_worldMaxX(worldMaxX), m_worldMaxZ(worldMaxZ)
    , m_cellSize(cellSize)
{
    m_gridWidth = static_cast<int>(std::ceil((worldMaxX - worldMinX) / cellSize));
    m_gridHeight = static_cast<int>(std::ceil((worldMaxZ - worldMinZ) / cellSize));
    m_grid.resize(m_gridWidth * m_gridHeight);

    m_spawnCenter = {m_gridWidth / 2, m_gridHeight / 2};
}

// --- Coordinate conversion ---

glm::ivec2 ZoneSystem::worldToGrid(float worldX, float worldZ) const {
    int gx = static_cast<int>(std::floor((worldX - m_worldMinX) / m_cellSize));
    int gz = static_cast<int>(std::floor((worldZ - m_worldMinZ) / m_cellSize));
    return {gx, gz};
}

glm::vec2 ZoneSystem::gridToWorld(int gridX, int gridZ) const {
    float wx = m_worldMinX + (gridX + 0.5f) * m_cellSize;
    float wz = m_worldMinZ + (gridZ + 0.5f) * m_cellSize;
    return {wx, wz};
}

int ZoneSystem::cellIndex(int gridX, int gridZ) const {
    return gridZ * m_gridWidth + gridX;
}

bool ZoneSystem::inBounds(int gridX, int gridZ) const {
    return gridX >= 0 && gridX < m_gridWidth && gridZ >= 0 && gridZ < m_gridHeight;
}

// --- Core queries ---

ZoneType ZoneSystem::getZoneType(float worldX, float worldZ) const {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return ZoneType::Wilderness;
    return m_grid[cellIndex(g.x, g.y)].type;
}

ResourceType ZoneSystem::getResource(float worldX, float worldZ) const {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return ResourceType::None;
    return m_grid[cellIndex(g.x, g.y)].resource;
}

uint32_t ZoneSystem::getOwner(float worldX, float worldZ) const {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return 0;
    return m_grid[cellIndex(g.x, g.y)].ownerPlayerId;
}

bool ZoneSystem::canBuild(float worldX, float worldZ, uint32_t playerId) const {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return false;
    const auto& cell = m_grid[cellIndex(g.x, g.y)];

    // Can't build in battlefield or spawn zones
    if (cell.type == ZoneType::Battlefield || cell.type == ZoneType::SpawnSafe)
        return false;

    // If owned, only owner can build
    if (cell.ownerPlayerId != 0)
        return cell.ownerPlayerId == playerId;

    // Unowned wilderness/resource — can build if player purchases first
    return false;
}

bool ZoneSystem::canEnter(float worldX, float worldZ, uint32_t /*playerId*/) const {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return true;
    // All zones are enterable for now (battlefield is dangerous but accessible)
    return true;
}

const ZoneCell* ZoneSystem::getCell(float worldX, float worldZ) const {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return nullptr;
    return &m_grid[cellIndex(g.x, g.y)];
}

ZoneCell* ZoneSystem::getCellMutable(float worldX, float worldZ) {
    auto g = worldToGrid(worldX, worldZ);
    if (!inBounds(g.x, g.y)) return nullptr;
    return &m_grid[cellIndex(g.x, g.y)];
}

// --- Zone painting ---

void ZoneSystem::setZoneType(int gridX, int gridZ, ZoneType type) {
    if (!inBounds(gridX, gridZ)) return;
    m_grid[cellIndex(gridX, gridZ)].type = type;
}

void ZoneSystem::setResource(int gridX, int gridZ, ResourceType resource, float density) {
    if (!inBounds(gridX, gridZ)) return;
    auto& cell = m_grid[cellIndex(gridX, gridZ)];
    cell.resource = resource;
    cell.resourceDensity = density;
    if (resource != ResourceType::None)
        cell.type = ZoneType::Resource;
}

void ZoneSystem::setOwner(int gridX, int gridZ, uint32_t playerId) {
    if (!inBounds(gridX, gridZ)) return;
    m_grid[cellIndex(gridX, gridZ)].ownerPlayerId = playerId;
}

void ZoneSystem::fillRect(int x1, int z1, int x2, int z2, ZoneType type) {
    int minX = std::max(0, std::min(x1, x2));
    int maxX = std::min(m_gridWidth - 1, std::max(x1, x2));
    int minZ = std::max(0, std::min(z1, z2));
    int maxZ = std::min(m_gridHeight - 1, std::max(z1, z2));

    for (int z = minZ; z <= maxZ; z++) {
        for (int x = minX; x <= maxX; x++) {
            m_grid[cellIndex(x, z)].type = type;
        }
    }
}

void ZoneSystem::fillCircle(int centerX, int centerZ, int radius, ZoneType type) {
    int r2 = radius * radius;
    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dz * dz <= r2) {
                int gx = centerX + dx;
                int gz = centerZ + dz;
                if (inBounds(gx, gz)) {
                    m_grid[cellIndex(gx, gz)].type = type;
                }
            }
        }
    }
}

void ZoneSystem::fillCircleResource(int centerX, int centerZ, int radius, ResourceType resource, float density) {
    int r2 = radius * radius;
    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dz * dz <= r2) {
                int gx = centerX + dx;
                int gz = centerZ + dz;
                if (inBounds(gx, gz)) {
                    auto& cell = m_grid[cellIndex(gx, gz)];
                    cell.type = ZoneType::Resource;
                    cell.resource = resource;
                    // Density falls off from center
                    float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                    float falloff = 1.0f - (dist / (radius + 1.0f));
                    cell.resourceDensity = density * falloff;
                }
            }
        }
    }
}

// --- Price calculation ---

float ZoneSystem::getPlotPrice(int gridX, int gridZ) const {
    if (!inBounds(gridX, gridZ)) return 0.0f;
    const auto& cell = m_grid[cellIndex(gridX, gridZ)];

    // Base price by zone type
    float base = 100.0f;
    switch (cell.type) {
        case ZoneType::Residential: base = 100.0f; break;
        case ZoneType::Commercial:  base = 200.0f; break;
        case ZoneType::Industrial:  base = 300.0f; break;
        case ZoneType::Resource:    base = 300.0f; break;
        case ZoneType::Wilderness:  base = 100.0f; break;
        default: return 0.0f; // Battlefield/SpawnSafe not purchasable
    }

    // Distance from spawn center
    float dx = static_cast<float>(gridX - m_spawnCenter.x);
    float dz = static_cast<float>(gridZ - m_spawnCenter.y);
    float distFromSpawn = std::sqrt(dx * dx + dz * dz);

    // Near spawn multiplier (within 10 plots)
    if (distFromSpawn < 10.0f)
        base *= 2.0f;
    // Near battlefield multiplier — check if any adjacent cell is battlefield
    else {
        bool nearBattlefield = false;
        for (int nz = gridZ - 2; nz <= gridZ + 2 && !nearBattlefield; nz++) {
            for (int nx = gridX - 2; nx <= gridX + 2 && !nearBattlefield; nx++) {
                if (inBounds(nx, nz) && m_grid[cellIndex(nx, nz)].type == ZoneType::Battlefield)
                    nearBattlefield = true;
            }
        }
        if (nearBattlefield) base *= 0.5f;
    }

    // Resource zone premium
    if (cell.type == ZoneType::Resource)
        base *= (1.0f + cell.resourceDensity * 2.0f);

    return base;
}

// --- Default layout ---

void ZoneSystem::generateDefaultLayout() {
    // Reset all to wilderness
    for (auto& cell : m_grid) {
        cell = ZoneCell{};
    }

    int cx = m_gridWidth / 2;
    int cz = m_gridHeight / 2;
    m_spawnCenter = {cx, cz};

    // Spawn/Safe zone: 5x5 plots at center
    fillRect(cx - 2, cz - 2, cx + 2, cz + 2, ZoneType::SpawnSafe);

    // Residential zones: neighborhoods around spawn (north and south)
    fillRect(cx - 8, cz - 12, cx + 8, cz - 4, ZoneType::Residential);  // North
    fillRect(cx - 8, cz + 4,  cx + 8, cz + 12, ZoneType::Residential); // South

    // Commercial zones: east and west strips near center
    fillRect(cx + 4, cz - 3, cx + 10, cz + 3, ZoneType::Commercial);   // East
    fillRect(cx - 10, cz - 3, cx - 4, cz + 3, ZoneType::Commercial);   // West

    // Industrial zones: further out, between residential and resources
    fillRect(cx - 18, cz - 6, cx - 12, cz + 6, ZoneType::Industrial);  // Far west
    fillRect(cx + 12, cz - 6, cx + 18, cz + 6, ZoneType::Industrial);  // Far east

    // Battlefield: horizontal strip through middle, 20 plots wide, further out
    int bfHalfWidth = 10;
    int bfTop = cz - bfHalfWidth;
    int bfBot = cz + bfHalfWidth;
    // Left side of battlefield (beyond industrial)
    fillRect(cx - 40, bfTop, cx - 20, bfBot, ZoneType::Battlefield);
    // Right side of battlefield (beyond industrial)
    fillRect(cx + 20, bfTop, cx + 40, bfBot, ZoneType::Battlefield);

    // Resource clusters (8 deposits scattered in wilderness)
    // Wood clusters
    fillCircleResource(cx - 30, cz - 25, 4, ResourceType::Wood, 0.8f);
    fillCircleResource(cx + 25, cz + 30, 3, ResourceType::Wood, 0.7f);

    // Iron clusters
    fillCircleResource(cx - 25, cz + 20, 3, ResourceType::Iron, 0.9f);
    fillCircleResource(cx + 30, cz - 20, 4, ResourceType::Iron, 0.6f);

    // Limestone clusters
    fillCircleResource(cx + 35, cz + 5, 3, ResourceType::Limestone, 0.8f);
    fillCircleResource(cx - 20, cz - 35, 3, ResourceType::Limestone, 0.7f);

    // Oil deposits (rarer, smaller)
    fillCircleResource(cx - 35, cz + 5, 2, ResourceType::Oil, 1.0f);
    fillCircleResource(cx + 20, cz - 35, 2, ResourceType::Oil, 0.9f);

    // Update prices for all cells
    for (int z = 0; z < m_gridHeight; z++) {
        for (int x = 0; x < m_gridWidth; x++) {
            m_grid[cellIndex(x, z)].purchasePrice = getPlotPrice(x, z);
        }
    }

    std::cout << "[ZoneSystem] Default layout generated (" << m_gridWidth << "x" << m_gridHeight << " grid)\n";
}

// --- Persistence ---

void ZoneSystem::save(nlohmann::json& root) const {
    json zonesJson;
    zonesJson["worldMinX"] = m_worldMinX;
    zonesJson["worldMinZ"] = m_worldMinZ;
    zonesJson["worldMaxX"] = m_worldMaxX;
    zonesJson["worldMaxZ"] = m_worldMaxZ;
    zonesJson["cellSize"] = m_cellSize;
    zonesJson["gridWidth"] = m_gridWidth;
    zonesJson["gridHeight"] = m_gridHeight;

    // Sparse: only save non-default cells
    json cellsJson = json::array();
    for (int z = 0; z < m_gridHeight; z++) {
        for (int x = 0; x < m_gridWidth; x++) {
            const auto& cell = m_grid[cellIndex(x, z)];
            if (cell.type == ZoneType::Wilderness && cell.resource == ResourceType::None &&
                cell.ownerPlayerId == 0) {
                continue;
            }
            json c;
            c["x"] = x;
            c["z"] = z;
            c["type"] = static_cast<int>(cell.type);
            if (cell.resource != ResourceType::None) {
                c["resource"] = static_cast<int>(cell.resource);
                c["density"] = cell.resourceDensity;
            }
            if (cell.ownerPlayerId != 0) {
                c["owner"] = cell.ownerPlayerId;
            }
            c["price"] = cell.purchasePrice;
            cellsJson.push_back(c);
        }
    }
    zonesJson["cells"] = cellsJson;
    root["zones"] = zonesJson;
}

void ZoneSystem::load(const nlohmann::json& root) {
    if (!root.contains("zones")) return;
    const auto& zonesJson = root["zones"];

    // Reset grid
    for (auto& cell : m_grid) {
        cell = ZoneCell{};
    }

    // Load cells
    if (zonesJson.contains("cells")) {
        for (const auto& c : zonesJson["cells"]) {
            int x = c.value("x", 0);
            int z = c.value("z", 0);
            if (!inBounds(x, z)) continue;

            auto& cell = m_grid[cellIndex(x, z)];
            cell.type = static_cast<ZoneType>(c.value("type", 0));
            cell.resource = static_cast<ResourceType>(c.value("resource", 0));
            cell.resourceDensity = c.value("density", 0.0f);
            cell.ownerPlayerId = c.value("owner", 0u);
            cell.purchasePrice = c.value("price", 100.0f);
        }
    }

    std::cout << "[ZoneSystem] Loaded zone data\n";
}

// --- String helpers ---

const char* ZoneSystem::zoneTypeName(ZoneType type) {
    switch (type) {
        case ZoneType::Wilderness:  return "wilderness";
        case ZoneType::Battlefield: return "battlefield";
        case ZoneType::SpawnSafe:   return "spawn_safe";
        case ZoneType::Residential: return "residential";
        case ZoneType::Commercial:  return "commercial";
        case ZoneType::Industrial:  return "industrial";
        case ZoneType::Resource:    return "resource";
    }
    return "unknown";
}

const char* ZoneSystem::resourceTypeName(ResourceType type) {
    switch (type) {
        case ResourceType::None:      return "none";
        case ResourceType::Wood:      return "wood";
        case ResourceType::Limestone: return "limestone";
        case ResourceType::Iron:      return "iron";
        case ResourceType::Oil:       return "oil";
    }
    return "none";
}

} // namespace eden
