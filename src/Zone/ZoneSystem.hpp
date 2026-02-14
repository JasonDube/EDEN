#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

namespace eden {

enum class ZoneType : uint8_t {
    Wilderness = 0,
    Battlefield,
    SpawnSafe,
    Residential,
    Commercial,
    Industrial,
    Resource
};

enum class ResourceType : uint8_t {
    None = 0,
    Wood,       // Organic: Wood, Organic Matter, Rare Flora
    Limestone,  // Stone: Limestone, Mineral Deposits, Carbon
    Iron,       // Metal: Iron, Nickel, Aluminum, Titanium, Silver, Platinum, Gold, etc.
    Oil,        // Fossil: Oil
    Water,      // Water: Water, Water Ice, Salt Compounds, Marine Biomass
    Gas,        // Atmospheric: Oxygen, Nitrogen, Hydrogen, Helium, Methane, Ammonia, CO2, Helium-3
    Crystal,    // Crystal: Diamond, Rare Crystals, Silicon, Sulfur
    Energy,     // Energy: Geothermal Energy, Uranium
    Exotic      // Exotic: Dark Matter, Exotic Matter, Ancient Artifacts
};

struct ZoneCell {
    ZoneType type = ZoneType::Wilderness;
    ResourceType resource = ResourceType::None;
    std::string resourceName;    // Individual resource identity: "Water", "Iron", "Nitrogen", etc.
    uint32_t ownerPlayerId = 0;
    float purchasePrice = 100.0f;
    float resourceDensity = 0.0f;
};

class ZoneSystem {
public:
    ZoneSystem(float worldMinX, float worldMinZ, float worldMaxX, float worldMaxZ, float cellSize = 32.0f);

    // Core queries
    ZoneType getZoneType(float worldX, float worldZ) const;
    ResourceType getResource(float worldX, float worldZ) const;
    const std::string& getResourceName(float worldX, float worldZ) const;
    uint32_t getOwner(float worldX, float worldZ) const;
    bool canBuild(float worldX, float worldZ, uint32_t playerId) const;
    bool canEnter(float worldX, float worldZ, uint32_t playerId) const;
    const ZoneCell* getCell(float worldX, float worldZ) const;
    ZoneCell* getCellMutable(float worldX, float worldZ);

    // Grid coordinate conversion
    glm::ivec2 worldToGrid(float worldX, float worldZ) const;
    glm::vec2 gridToWorld(int gridX, int gridZ) const;
    int getGridWidth() const { return m_gridWidth; }
    int getGridHeight() const { return m_gridHeight; }

    // Zone painting (editor)
    void setZoneType(int gridX, int gridZ, ZoneType type);
    void setResource(int gridX, int gridZ, ResourceType resource, float density);
    void setOwner(int gridX, int gridZ, uint32_t playerId);

    // Batch operations
    void fillRect(int x1, int z1, int x2, int z2, ZoneType type);
    void fillCircle(int centerX, int centerZ, int radius, ZoneType type);
    void fillCircleResource(int centerX, int centerZ, int radius, ResourceType resource, float density);
    void fillCircleResource(int centerX, int centerZ, int radius, ResourceType resource, float density, const std::string& name);

    // Price calculation
    float getPlotPrice(int gridX, int gridZ) const;

    // Persistence
    void save(nlohmann::json& root) const;
    void load(const nlohmann::json& root);

    // Default layout generation
    void generateDefaultLayout();

    // Planet-aware layout generation from backend planet data
    void generatePlanetLayout(const nlohmann::json& planetData);

    // String helpers
    static const char* zoneTypeName(ZoneType type);
    static const char* resourceTypeName(ResourceType type);

    // World bounds
    float getWorldMinX() const { return m_worldMinX; }
    float getWorldMinZ() const { return m_worldMinZ; }
    float getCellSize() const { return m_cellSize; }

private:
    float m_worldMinX, m_worldMinZ;
    float m_worldMaxX, m_worldMaxZ;
    float m_cellSize;
    int m_gridWidth, m_gridHeight;
    std::vector<ZoneCell> m_grid;

    int cellIndex(int gridX, int gridZ) const;
    bool inBounds(int gridX, int gridZ) const;

    // Spawn center location (grid coords)
    glm::ivec2 m_spawnCenter{0, 0};

    // Empty string for out-of-bounds queries
    static const std::string s_emptyString;
};

} // namespace eden
