#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <cstdint>

namespace eden {

class EconomySystem;

// Building categories
enum class BuildingCategory {
    HOUSING,        // Population capacity
    FOOD,           // Food production
    RESOURCE,       // Raw material extraction (wood, stone, ore)
    INDUSTRY,       // Manufacturing/processing
    SERVICE,        // Happiness (church, entertainment, hospital)
    INFRASTRUCTURE, // Roads, power, water
    COMMERCIAL,     // Markets, shops
    MILITARY,       // Defense (optional)
    COUNT
};

// Individual building types
enum class BuildingType {
    // Housing (tier 1-3)
    SHACK,              // Cheap, low capacity, low happiness
    HOUSE,              // Medium tier
    APARTMENT,          // High density
    LUXURY_APARTMENT,   // High happiness

    // Food production
    FARM,               // Basic food
    RANCH,              // Advanced food + leather
    FISHERY,            // Coastal food
    GREENHOUSE,         // High-tech food, any location

    // Resource extraction
    LUMBER_MILL,        // Wood
    QUARRY,             // Stone
    MINE,               // Ore
    OIL_REFINERY,       // Fuel

    // Industry
    WORKSHOP,           // Basic goods from wood
    FOUNDRY,            // Metal from ore
    FACTORY,            // Goods from metal
    ELECTRONICS_PLANT,  // Electronics (high-tech)

    // Services
    CHAPEL,             // Basic religion happiness
    CHURCH,             // Medium religion
    CLINIC,             // Basic health
    HOSPITAL,           // Advanced health
    TAVERN,             // Basic entertainment
    THEATER,            // Medium entertainment
    SCHOOL,             // Education (unlocks tech faster)
    UNIVERSITY,         // Advanced education

    // Commercial
    MARKET,             // Buy/sell goods
    WAREHOUSE,          // Storage
    TRADING_POST,       // Player/AI trader interaction

    // Infrastructure
    ROAD,               // Connects buildings
    POWER_PLANT,        // Powers high-tech buildings
    WATER_TOWER,        // Water supply

    COUNT
};

// Tech tree nodes
enum class TechLevel {
    PRIMITIVE,      // Start: shacks, farms, lumber
    BASIC,          // Houses, workshop, chapel
    INTERMEDIATE,   // Apartments, factory, church, clinic
    ADVANCED,       // Foundry, hospital, theater
    MODERN,         // Electronics, university, luxury
    COUNT
};

// Building definition (for city AI - separate from economy CityBuildingDef)
struct CityBuildingDef {
    BuildingType type;
    BuildingCategory category;
    std::string name;
    TechLevel requiredTech;

    // Costs
    float buildCost = 100.0f;       // Money to build
    float maintenanceCost = 5.0f;   // Money per game day
    float woodCost = 0.0f;          // Resources needed
    float stoneCost = 0.0f;
    float metalCost = 0.0f;

    // Effects
    int housingCapacity = 0;        // Population it can house
    float happinessBonus = 0.0f;    // Happiness contribution
    int jobsProvided = 0;           // Employment
    float productionRate = 0.0f;    // Units produced per hour

    // Requirements
    bool requiresPower = false;
    bool requiresWater = false;
    bool requiresCoast = false;
};

// A placed building instance
struct Building {
    uint32_t id;
    BuildingType type;
    uint32_t graphNodeId;       // Location in GRAPH system
    std::string name;

    float health = 100.0f;
    float efficiency = 1.0f;    // Affected by workers, power, etc.
    int workers = 0;
    int maxWorkers = 10;

    bool hasPower = true;
    bool hasWater = true;
    bool isOperational = true;

    // For housing
    int residents = 0;
    int maxResidents = 0;
};

// City state snapshot
struct CityStats {
    // Population
    int population = 0;
    int housingCapacity = 0;
    int employed = 0;
    int unemployed = 0;

    // Happiness factors (0-100)
    float overallHappiness = 50.0f;
    float foodHappiness = 50.0f;
    float housingHappiness = 50.0f;
    float jobHappiness = 50.0f;
    float religionHappiness = 50.0f;
    float healthHappiness = 50.0f;
    float entertainmentHappiness = 50.0f;

    // Economy
    float treasury = 1000.0f;
    float dailyIncome = 0.0f;
    float dailyExpenses = 0.0f;

    // Resources
    float foodSupply = 0.0f;
    float foodDemand = 0.0f;

    // Tech
    TechLevel currentTech = TechLevel::PRIMITIVE;
    float researchProgress = 0.0f;
};

// Governor's current goal/priority
enum class GovernorPriority {
    GROW_POPULATION,    // Need more people
    BUILD_HOUSING,      // People need homes
    PRODUCE_FOOD,       // Food shortage
    CREATE_JOBS,        // Unemployment
    INCREASE_HAPPINESS, // People unhappy
    ADVANCE_TECH,       // Research
    BUILD_ECONOMY,      // More production
    BALANCE_BUDGET,     // Running out of money
};

// Callback when governor builds something
using BuildingPlacedCallback = std::function<void(const Building& building, const CityBuildingDef& def)>;

/**
 * City AI Governor - Manages city like a Tropico player.
 * Makes autonomous decisions about what to build based on city needs.
 */
class CityGovernor {
public:
    CityGovernor();

    // Connect to economy system
    void setEconomySystem(EconomySystem* economy) { m_economy = economy; }

    // Update simulation (call each frame)
    void update(float deltaTime, float gameTimeMinutes);

    // City management
    const CityStats& getStats() const { return m_stats; }
    TechLevel getTechLevel() const { return m_stats.currentTech; }

    // Building management
    bool canBuild(BuildingType type) const;
    bool build(BuildingType type, uint32_t graphNodeId);
    void demolish(uint32_t buildingId);
    const std::vector<Building>& getBuildings() const { return m_buildings; }
    const Building* getBuilding(uint32_t id) const;

    // Get building at a specific graph node
    const Building* getBuildingAtNode(uint32_t graphNodeId) const;

    // Building definitions
    static const CityBuildingDef& getBuildingDef(BuildingType type);
    static const char* getBuildingName(BuildingType type);
    static const char* getCategoryName(BuildingCategory cat);
    static const char* getTechLevelName(TechLevel level);

    // Governor AI settings
    void setAutoBuild(bool enabled) { m_autoBuild = enabled; }
    bool isAutoBuildEnabled() const { return m_autoBuild; }
    GovernorPriority getCurrentPriority() const { return m_currentPriority; }

    // Callbacks
    void setOnBuildingPlaced(BuildingPlacedCallback callback) { m_onBuildingPlaced = callback; }

    // Tax rate affects income and happiness
    void setTaxRate(float rate) { m_taxRate = std::clamp(rate, 0.0f, 0.5f); }
    float getTaxRate() const { return m_taxRate; }

    // Initial city setup (creates starting buildings)
    void initializeCity(const std::vector<uint32_t>& startingNodeIds);

private:
    void initializeBuildingDefs();
    void updatePopulation(float deltaTime);
    void updateHappiness();
    void updateEconomy(float deltaTime);
    void updateResearch(float deltaTime);
    void updateBuildings(float deltaTime);

    // AI decision making
    void evaluatePriorities();
    BuildingType decideToBuild();
    uint32_t findBuildLocation(BuildingType type);

    // Helpers
    int countBuildings(BuildingType type) const;
    int countBuildingsByCategory(BuildingCategory cat) const;
    float calculateProductionCapacity(BuildingCategory cat) const;

    // State
    CityStats m_stats;
    std::vector<Building> m_buildings;
    uint32_t m_nextBuildingId = 1;

    // Building definitions (static data)
    static std::unordered_map<BuildingType, CityBuildingDef> s_buildingDefs;
    static bool s_defsInitialized;

    // AI
    bool m_autoBuild = true;
    GovernorPriority m_currentPriority = GovernorPriority::GROW_POPULATION;
    float m_decisionCooldown = 0.0f;
    static constexpr float DECISION_INTERVAL = 60.0f; // Decide every game minute

    // Economy link
    EconomySystem* m_economy = nullptr;

    // Settings
    float m_taxRate = 0.1f; // 10% default

    // Available build locations (GRAPH node IDs the governor can use)
    std::vector<uint32_t> m_availableLocations;

    // Callbacks
    BuildingPlacedCallback m_onBuildingPlaced;

    // Timing
    float m_timeSinceLastUpdate = 0.0f;
    float m_gameDay = 0.0f;
    static constexpr float MINUTES_PER_DAY = 1440.0f;
};

} // namespace eden
