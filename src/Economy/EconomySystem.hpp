#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace eden {

// Types of goods in the economy
enum class GoodType {
    // === RAW RESOURCES (from base producers) ===
    OIL,            // From oil rigs
    NATURAL_GAS,    // From gas wells
    LIMESTONE,      // From quarries
    COAL,           // From mines
    PHOSPHATES,     // From mines
    SULFUR,         // From mines
    GRAIN,          // From farms
    COTTON,         // From cotton farms
    FISH,           // From fisheries
    TIMBER,         // From logging camps (raw logs)
    SAND,           // From sand quarries
    GRAVEL,         // From gravel pits
    CHEMICALS,      // Now manufactured from raw resources
    PLASTICS,       // Now manufactured from raw resources
    EXPLOSIVES,
    FLARES,
    PURE_WATER,
    CIGARS,         // Huskar Cigars
    FOOD,           // Processed from grain, meat, fish
    MEAT,           // From ranches (needs grain)
    FURS,           // From ranches
    ORE,
    GEMS,
    CONSTMAT,       // Construction Materials
    FUEL,           // Vehicle/ship fuel
    SCRAP_METAL,    // For recycling

    // === PROCESSED MATERIALS ===
    SHEET_METAL,
    EX_METAL,       // Exotic Metal
    LUMBER,         // From sawmills (processed timber)
    STEEL,          // From steel mills (ore + coal + limestone)
    CONCRETE,       // From cement plants (limestone + sand + gravel + water)

    // === COMPONENTS ===
    COMP_COMP,      // Computer Components
    MACH_PARTS,     // Machine Parts
    CELL_1,         // Power Cell Type 1
    CELL_2,         // Power Cell Type 2
    CELL_3,         // Power Cell Type 3
    CELL_4,         // Power Cell Type 4
    FUSION_PARTS,
    LASER,

    // === ENGINES & PODS ===
    ENGINE_1,
    ENGINE_2,
    POD_SMALLEST,
    POD_SMALL,
    POD_MEDIUM,
    POD_LARGE,

    // === WEAPONS & EQUIPMENT ===
    SPRAT,          // Missile type (x10)
    SWARM,          // Missile type (x10)
    DEVASTATOR,     // Heavy weapon
    HOLOGRAM,       // Countermeasure (x5)
    CHAFF,          // Countermeasure (x10)
    SALVAGE_DRONE,

    // === CONSUMABLES ===
    NARCOTICS,
    ALCOHOL,
    MEDICINE,       // Healthcare - manufactured from chemicals + water
    TEXTILES,       // Clothing, fabric goods

    // === VEHICLES (Moths) ===
    MOTH_SILVER_Y,
    MOTH_SWALLOW,
    MOTH_HAWK,
    MOTH_NEO_TIGER,
    MOTH_MOON,
    MOTH_POLICE,
    MOTH_DEATHS_HEAD,

    COUNT
};

// Building production type
enum class BuildingRole {
    BASE_PRODUCER,      // Creates goods from nothing (mines, farms, etc.)
    MANUFACTURER,       // Converts input goods to output goods
    CONSUMER,           // Consumes goods (residences, etc.)
    STORAGE,            // Stores goods (warehouses)
    MARKET,             // Buys/sells goods
};

// Definition of what a building produces/consumes
struct ProductionRule {
    GoodType good;
    float rate;         // Units per game hour
};

// Building template definition
struct BuildingDef {
    std::string name;
    BuildingRole role;
    std::vector<ProductionRule> inputs;     // What it consumes
    std::vector<ProductionRule> outputs;    // What it produces
    float baseInventoryCapacity = 100.0f;
};

// Get building definitions
const std::vector<BuildingDef>& getBaseProducers();
const std::vector<BuildingDef>& getManufacturers();
const std::vector<BuildingDef>& getConsumers();
const BuildingDef* findBuildingDef(const std::string& name);

// Economic signal types broadcast to traders
enum class EconomySignalType {
    PRICE_SPIKE,        // Price increased significantly
    PRICE_DROP,         // Price decreased significantly
    SHORTAGE,           // Supply critically low
    SURPLUS,            // Supply very high (good time to buy)
    NEW_DEMAND,         // New consumer entered market (new building)
    PRODUCTION_ONLINE,  // New producer (factory opened)
    PRODUCTION_OFFLINE, // Producer closed/destroyed
};

// Signal sent to traders (player and AI)
struct EconomySignal {
    EconomySignalType type;
    GoodType good;
    uint32_t locationNodeId;    // Which GRAPH node this relates to
    float magnitude;            // How significant (0-1, for prioritization)
    std::string message;        // Human-readable for player UI
    float gameTime;             // When it happened
};

// Tracks a single good's market state
struct GoodMarket {
    GoodType type;

    // Global supply/demand (across all nodes)
    float globalSupply = 0.0f;      // Total available in economy
    float globalDemand = 0.0f;      // Total wanted by consumers

    // Price calculation
    float basePrice = 10.0f;        // Default price when balanced
    float currentPrice = 10.0f;     // Actual price based on supply/demand
    float priceVolatility = 0.1f;   // How fast price reacts (0-1)

    // Thresholds for signals
    float shortageThreshold = 0.3f; // supply/demand ratio below this = shortage
    float surplusThreshold = 2.0f;  // supply/demand ratio above this = surplus
    float priceChangeThreshold = 0.15f; // 15% change triggers signal

    // History for trend analysis
    float lastPrice = 10.0f;
    float priceChangeRate = 0.0f;   // Positive = rising, negative = falling
};

// Represents a location that produces or consumes goods
struct EconomyNode {
    uint32_t graphNodeId;           // Links to GRAPH node in AINode system
    std::string name;

    // What this location produces
    struct Production {
        GoodType good;
        float rate;                 // Units per game hour
        float efficiency = 1.0f;    // Multiplier (workers, upgrades affect this)
    };
    std::vector<Production> produces;

    // What this location consumes/buys
    struct Consumption {
        GoodType good;
        float rate;                 // Units per game hour
        float priority = 1.0f;      // How much they'll pay above market
    };
    std::vector<Consumption> consumes;

    // Local inventory at this node
    std::unordered_map<GoodType, float> inventory;
    std::unordered_map<GoodType, float> maxInventory;

    // Price modifiers (local supply/demand affects local prices)
    std::unordered_map<GoodType, float> buyPriceModifier;   // Willing to pay X% of market
    std::unordered_map<GoodType, float> sellPriceModifier;  // Selling at X% of market
};

// Callback type for signal subscribers
using EconomySignalCallback = std::function<void(const EconomySignal&)>;

/**
 * Central economy simulation system.
 * Tracks supply/demand, calculates prices, broadcasts signals to traders.
 */
class EconomySystem {
public:
    EconomySystem();

    // Update economy simulation (call each frame with delta time)
    void update(float deltaTime, float gameTimeMinutes);

    // Market data queries
    float getPrice(GoodType good) const;
    float getSupplyDemandRatio(GoodType good) const;
    const GoodMarket& getMarket(GoodType good) const;
    bool isShortage(GoodType good) const;
    bool isSurplus(GoodType good) const;

    // Node management
    void registerNode(const EconomyNode& node);
    void unregisterNode(uint32_t graphNodeId);
    void clearNodes();
    EconomyNode* getNode(uint32_t graphNodeId);
    const EconomyNode* getNode(uint32_t graphNodeId) const;
    const std::unordered_map<uint32_t, EconomyNode>& getNodes() const { return m_nodes; }

    // Get nodes by criteria (for trader queries)
    std::vector<uint32_t> findNodesSelling(GoodType good) const;
    std::vector<uint32_t> findNodesBuying(GoodType good) const;
    std::vector<uint32_t> findBestBuyPrice(GoodType good, int maxResults = 5) const;
    std::vector<uint32_t> findBestSellPrice(GoodType good, int maxResults = 5) const;

    // Trading operations
    bool canBuy(uint32_t nodeId, GoodType good, float quantity) const;
    bool canSell(uint32_t nodeId, GoodType good, float quantity) const;
    float getBuyPrice(uint32_t nodeId, GoodType good) const;  // What you pay
    float getSellPrice(uint32_t nodeId, GoodType good) const; // What you receive
    bool executeTrade(uint32_t nodeId, GoodType good, float quantity, bool buying);

    // Signal subscription
    void subscribe(EconomySignalCallback callback);
    void unsubscribeAll();

    // Population affects demand
    void setPopulation(int population);
    int getPopulation() const { return m_population; }

    // Debug/UI
    static const char* getGoodName(GoodType good);
    static const char* getSignalTypeName(EconomySignalType type);

private:
    void initializeMarkets();
    void updateProduction(float deltaTime);
    void updateConsumption(float deltaTime);
    void updatePrices(float gameTimeMinutes);
    void checkAndEmitSignals(float gameTimeMinutes);
    void emitSignal(const EconomySignal& signal);

    // Markets for each good type
    std::unordered_map<GoodType, GoodMarket> m_markets;

    // All economy-participating nodes
    std::unordered_map<uint32_t, EconomyNode> m_nodes;

    // Signal subscribers (traders)
    std::vector<EconomySignalCallback> m_subscribers;

    // Recent signals (for UI/history)
    std::vector<EconomySignal> m_recentSignals;
    static constexpr size_t MAX_SIGNAL_HISTORY = 50;

    // Global modifiers
    int m_population = 100;
    float m_economySpeed = 1.0f;    // Simulation speed multiplier

    // Update timing
    float m_timeSinceLastUpdate = 0.0f;
    static constexpr float UPDATE_INTERVAL = 1.0f; // Update economy every 1 game minute
};

} // namespace eden
