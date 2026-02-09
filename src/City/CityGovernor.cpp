#include "CityGovernor.hpp"
#include "../Economy/EconomySystem.hpp"
#include <algorithm>
#include <cmath>

namespace eden {

// Static members
std::unordered_map<BuildingType, CityBuildingDef> CityGovernor::s_buildingDefs;
bool CityGovernor::s_defsInitialized = false;

CityGovernor::CityGovernor() {
    initializeBuildingDefs();
}

void CityGovernor::initializeBuildingDefs() {
    if (s_defsInitialized) return;
    s_defsInitialized = true;

    // === HOUSING ===
    {
        CityBuildingDef def;
        def.type = BuildingType::SHACK;
        def.category = BuildingCategory::HOUSING;
        def.name = "Shack";
        def.requiredTech = TechLevel::PRIMITIVE;
        def.buildCost = 50.0f;
        def.maintenanceCost = 1.0f;
        def.woodCost = 10.0f;
        def.housingCapacity = 4;
        def.happinessBonus = -5.0f; // People don't like shacks
        s_buildingDefs[BuildingType::SHACK] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::HOUSE;
        def.category = BuildingCategory::HOUSING;
        def.name = "House";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 200.0f;
        def.maintenanceCost = 5.0f;
        def.woodCost = 30.0f;
        def.stoneCost = 20.0f;
        def.housingCapacity = 6;
        def.happinessBonus = 5.0f;
        s_buildingDefs[BuildingType::HOUSE] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::APARTMENT;
        def.category = BuildingCategory::HOUSING;
        def.name = "Apartment";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 500.0f;
        def.maintenanceCost = 15.0f;
        def.stoneCost = 50.0f;
        def.metalCost = 20.0f;
        def.housingCapacity = 20;
        def.happinessBonus = 0.0f;
        def.requiresPower = true;
        s_buildingDefs[BuildingType::APARTMENT] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::LUXURY_APARTMENT;
        def.category = BuildingCategory::HOUSING;
        def.name = "Luxury Apartment";
        def.requiredTech = TechLevel::MODERN;
        def.buildCost = 1500.0f;
        def.maintenanceCost = 50.0f;
        def.stoneCost = 80.0f;
        def.metalCost = 50.0f;
        def.housingCapacity = 30;
        def.happinessBonus = 15.0f;
        def.requiresPower = true;
        def.requiresWater = true;
        s_buildingDefs[BuildingType::LUXURY_APARTMENT] = def;
    }

    // === FOOD PRODUCTION ===
    {
        CityBuildingDef def;
        def.type = BuildingType::FARM;
        def.category = BuildingCategory::FOOD;
        def.name = "Farm";
        def.requiredTech = TechLevel::PRIMITIVE;
        def.buildCost = 100.0f;
        def.maintenanceCost = 5.0f;
        def.woodCost = 20.0f;
        def.jobsProvided = 5;
        def.productionRate = 10.0f; // Food per hour
        s_buildingDefs[BuildingType::FARM] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::RANCH;
        def.category = BuildingCategory::FOOD;
        def.name = "Ranch";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 250.0f;
        def.maintenanceCost = 10.0f;
        def.woodCost = 40.0f;
        def.jobsProvided = 8;
        def.productionRate = 15.0f;
        s_buildingDefs[BuildingType::RANCH] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::FISHERY;
        def.category = BuildingCategory::FOOD;
        def.name = "Fishery";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 200.0f;
        def.maintenanceCost = 8.0f;
        def.woodCost = 30.0f;
        def.jobsProvided = 6;
        def.productionRate = 12.0f;
        def.requiresCoast = true;
        s_buildingDefs[BuildingType::FISHERY] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::GREENHOUSE;
        def.category = BuildingCategory::FOOD;
        def.name = "Greenhouse";
        def.requiredTech = TechLevel::ADVANCED;
        def.buildCost = 800.0f;
        def.maintenanceCost = 30.0f;
        def.metalCost = 40.0f;
        def.jobsProvided = 4;
        def.productionRate = 25.0f;
        def.requiresPower = true;
        s_buildingDefs[BuildingType::GREENHOUSE] = def;
    }

    // === RESOURCE EXTRACTION ===
    {
        CityBuildingDef def;
        def.type = BuildingType::LUMBER_MILL;
        def.category = BuildingCategory::RESOURCE;
        def.name = "Lumber Mill";
        def.requiredTech = TechLevel::PRIMITIVE;
        def.buildCost = 150.0f;
        def.maintenanceCost = 8.0f;
        def.woodCost = 10.0f;
        def.jobsProvided = 6;
        def.productionRate = 8.0f; // Wood per hour
        s_buildingDefs[BuildingType::LUMBER_MILL] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::QUARRY;
        def.category = BuildingCategory::RESOURCE;
        def.name = "Quarry";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 200.0f;
        def.maintenanceCost = 10.0f;
        def.woodCost = 25.0f;
        def.jobsProvided = 8;
        def.productionRate = 6.0f; // Stone per hour
        s_buildingDefs[BuildingType::QUARRY] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::MINE;
        def.category = BuildingCategory::RESOURCE;
        def.name = "Mine";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 400.0f;
        def.maintenanceCost = 20.0f;
        def.woodCost = 30.0f;
        def.stoneCost = 20.0f;
        def.jobsProvided = 12;
        def.productionRate = 5.0f; // Ore per hour
        s_buildingDefs[BuildingType::MINE] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::OIL_REFINERY;
        def.category = BuildingCategory::RESOURCE;
        def.name = "Oil Refinery";
        def.requiredTech = TechLevel::ADVANCED;
        def.buildCost = 1000.0f;
        def.maintenanceCost = 40.0f;
        def.metalCost = 60.0f;
        def.jobsProvided = 10;
        def.productionRate = 8.0f; // Fuel per hour
        def.requiresPower = true;
        s_buildingDefs[BuildingType::OIL_REFINERY] = def;
    }

    // === INDUSTRY ===
    {
        CityBuildingDef def;
        def.type = BuildingType::WORKSHOP;
        def.category = BuildingCategory::INDUSTRY;
        def.name = "Workshop";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 180.0f;
        def.maintenanceCost = 8.0f;
        def.woodCost = 25.0f;
        def.jobsProvided = 4;
        def.productionRate = 3.0f; // Basic goods
        s_buildingDefs[BuildingType::WORKSHOP] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::FOUNDRY;
        def.category = BuildingCategory::INDUSTRY;
        def.name = "Foundry";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 500.0f;
        def.maintenanceCost = 25.0f;
        def.stoneCost = 40.0f;
        def.jobsProvided = 8;
        def.productionRate = 4.0f; // Metal from ore
        def.requiresPower = true;
        s_buildingDefs[BuildingType::FOUNDRY] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::FACTORY;
        def.category = BuildingCategory::INDUSTRY;
        def.name = "Factory";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 700.0f;
        def.maintenanceCost = 35.0f;
        def.stoneCost = 50.0f;
        def.metalCost = 30.0f;
        def.jobsProvided = 15;
        def.productionRate = 8.0f; // Manufactured goods
        def.requiresPower = true;
        s_buildingDefs[BuildingType::FACTORY] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::ELECTRONICS_PLANT;
        def.category = BuildingCategory::INDUSTRY;
        def.name = "Electronics Plant";
        def.requiredTech = TechLevel::MODERN;
        def.buildCost = 2000.0f;
        def.maintenanceCost = 80.0f;
        def.metalCost = 100.0f;
        def.jobsProvided = 20;
        def.productionRate = 5.0f; // Electronics
        def.requiresPower = true;
        def.requiresWater = true;
        s_buildingDefs[BuildingType::ELECTRONICS_PLANT] = def;
    }

    // === SERVICES ===
    {
        CityBuildingDef def;
        def.type = BuildingType::CHAPEL;
        def.category = BuildingCategory::SERVICE;
        def.name = "Chapel";
        def.requiredTech = TechLevel::PRIMITIVE;
        def.buildCost = 100.0f;
        def.maintenanceCost = 5.0f;
        def.woodCost = 20.0f;
        def.jobsProvided = 1;
        def.happinessBonus = 10.0f;
        s_buildingDefs[BuildingType::CHAPEL] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::CHURCH;
        def.category = BuildingCategory::SERVICE;
        def.name = "Church";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 400.0f;
        def.maintenanceCost = 15.0f;
        def.stoneCost = 50.0f;
        def.jobsProvided = 3;
        def.happinessBonus = 20.0f;
        s_buildingDefs[BuildingType::CHURCH] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::CLINIC;
        def.category = BuildingCategory::SERVICE;
        def.name = "Clinic";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 250.0f;
        def.maintenanceCost = 15.0f;
        def.woodCost = 20.0f;
        def.stoneCost = 15.0f;
        def.jobsProvided = 3;
        def.happinessBonus = 10.0f;
        s_buildingDefs[BuildingType::CLINIC] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::HOSPITAL;
        def.category = BuildingCategory::SERVICE;
        def.name = "Hospital";
        def.requiredTech = TechLevel::ADVANCED;
        def.buildCost = 1000.0f;
        def.maintenanceCost = 50.0f;
        def.stoneCost = 60.0f;
        def.metalCost = 40.0f;
        def.jobsProvided = 15;
        def.happinessBonus = 25.0f;
        def.requiresPower = true;
        s_buildingDefs[BuildingType::HOSPITAL] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::TAVERN;
        def.category = BuildingCategory::SERVICE;
        def.name = "Tavern";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 150.0f;
        def.maintenanceCost = 10.0f;
        def.woodCost = 25.0f;
        def.jobsProvided = 4;
        def.happinessBonus = 8.0f;
        s_buildingDefs[BuildingType::TAVERN] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::THEATER;
        def.category = BuildingCategory::SERVICE;
        def.name = "Theater";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 500.0f;
        def.maintenanceCost = 25.0f;
        def.stoneCost = 40.0f;
        def.metalCost = 10.0f;
        def.jobsProvided = 8;
        def.happinessBonus = 18.0f;
        s_buildingDefs[BuildingType::THEATER] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::SCHOOL;
        def.category = BuildingCategory::SERVICE;
        def.name = "School";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 300.0f;
        def.maintenanceCost = 15.0f;
        def.woodCost = 30.0f;
        def.stoneCost = 20.0f;
        def.jobsProvided = 5;
        def.happinessBonus = 5.0f;
        s_buildingDefs[BuildingType::SCHOOL] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::UNIVERSITY;
        def.category = BuildingCategory::SERVICE;
        def.name = "University";
        def.requiredTech = TechLevel::ADVANCED;
        def.buildCost = 1200.0f;
        def.maintenanceCost = 60.0f;
        def.stoneCost = 80.0f;
        def.metalCost = 30.0f;
        def.jobsProvided = 20;
        def.happinessBonus = 10.0f;
        def.requiresPower = true;
        s_buildingDefs[BuildingType::UNIVERSITY] = def;
    }

    // === COMMERCIAL ===
    {
        CityBuildingDef def;
        def.type = BuildingType::MARKET;
        def.category = BuildingCategory::COMMERCIAL;
        def.name = "Market";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 200.0f;
        def.maintenanceCost = 10.0f;
        def.woodCost = 30.0f;
        def.jobsProvided = 6;
        def.happinessBonus = 5.0f;
        s_buildingDefs[BuildingType::MARKET] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::WAREHOUSE;
        def.category = BuildingCategory::COMMERCIAL;
        def.name = "Warehouse";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 250.0f;
        def.maintenanceCost = 8.0f;
        def.woodCost = 40.0f;
        def.stoneCost = 20.0f;
        def.jobsProvided = 4;
        s_buildingDefs[BuildingType::WAREHOUSE] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::TRADING_POST;
        def.category = BuildingCategory::COMMERCIAL;
        def.name = "Trading Post";
        def.requiredTech = TechLevel::PRIMITIVE;
        def.buildCost = 150.0f;
        def.maintenanceCost = 5.0f;
        def.woodCost = 25.0f;
        def.jobsProvided = 2;
        s_buildingDefs[BuildingType::TRADING_POST] = def;
    }

    // === INFRASTRUCTURE ===
    {
        CityBuildingDef def;
        def.type = BuildingType::POWER_PLANT;
        def.category = BuildingCategory::INFRASTRUCTURE;
        def.name = "Power Plant";
        def.requiredTech = TechLevel::INTERMEDIATE;
        def.buildCost = 800.0f;
        def.maintenanceCost = 40.0f;
        def.stoneCost = 60.0f;
        def.metalCost = 40.0f;
        def.jobsProvided = 8;
        s_buildingDefs[BuildingType::POWER_PLANT] = def;
    }
    {
        CityBuildingDef def;
        def.type = BuildingType::WATER_TOWER;
        def.category = BuildingCategory::INFRASTRUCTURE;
        def.name = "Water Tower";
        def.requiredTech = TechLevel::BASIC;
        def.buildCost = 300.0f;
        def.maintenanceCost = 10.0f;
        def.stoneCost = 30.0f;
        def.metalCost = 20.0f;
        def.jobsProvided = 2;
        s_buildingDefs[BuildingType::WATER_TOWER] = def;
    }
}

void CityGovernor::update(float deltaTime, float gameTimeMinutes) {
    m_timeSinceLastUpdate += deltaTime;

    // Track game day for daily economy updates
    float newDay = gameTimeMinutes / MINUTES_PER_DAY;
    bool dayChanged = static_cast<int>(newDay) > static_cast<int>(m_gameDay);
    m_gameDay = newDay;

    // Update at regular intervals
    if (m_timeSinceLastUpdate < 1.0f) {
        return;
    }
    m_timeSinceLastUpdate = 0.0f;

    // Update simulation
    updateBuildings(1.0f);
    updatePopulation(1.0f);
    updateHappiness();

    if (dayChanged) {
        updateEconomy(1.0f);
    }

    updateResearch(1.0f);

    // AI decision making
    if (m_autoBuild) {
        m_decisionCooldown -= 1.0f;
        if (m_decisionCooldown <= 0.0f) {
            evaluatePriorities();
            BuildingType toBuild = decideToBuild();
            if (toBuild != BuildingType::COUNT) {
                uint32_t location = findBuildLocation(toBuild);
                if (location != 0) {
                    build(toBuild, location);
                }
            }
            m_decisionCooldown = DECISION_INTERVAL;
        }
    }
}

void CityGovernor::updatePopulation(float deltaTime) {
    // Calculate housing capacity
    m_stats.housingCapacity = 0;
    for (const auto& building : m_buildings) {
        const auto& def = getBuildingDef(building.type);
        if (def.category == BuildingCategory::HOUSING) {
            m_stats.housingCapacity += def.housingCapacity;
        }
    }

    // Population growth based on happiness and food
    float growthRate = 0.0f;

    // Positive factors
    if (m_stats.overallHappiness > 60.0f) {
        growthRate += (m_stats.overallHappiness - 60.0f) * 0.001f;
    }
    if (m_stats.foodSupply > m_stats.foodDemand) {
        growthRate += 0.005f;
    }

    // Negative factors
    if (m_stats.overallHappiness < 30.0f) {
        growthRate -= (30.0f - m_stats.overallHappiness) * 0.002f; // People leave
    }
    if (m_stats.foodSupply < m_stats.foodDemand * 0.5f) {
        growthRate -= 0.01f; // Starvation
    }

    // Apply growth (capped by housing)
    float potentialPop = m_stats.population + growthRate * m_stats.population * deltaTime;
    m_stats.population = static_cast<int>(std::min(potentialPop, static_cast<float>(m_stats.housingCapacity)));
    m_stats.population = std::max(m_stats.population, 10); // Minimum population

    // Calculate employment
    int totalJobs = 0;
    for (const auto& building : m_buildings) {
        const auto& def = getBuildingDef(building.type);
        totalJobs += def.jobsProvided;
    }

    m_stats.employed = std::min(m_stats.population, totalJobs);
    m_stats.unemployed = m_stats.population - m_stats.employed;

    // Update economy system population if connected
    if (m_economy) {
        m_economy->setPopulation(m_stats.population);
    }
}

void CityGovernor::updateHappiness() {
    // Food happiness
    if (m_stats.foodDemand > 0) {
        float foodRatio = m_stats.foodSupply / m_stats.foodDemand;
        m_stats.foodHappiness = std::clamp(foodRatio * 50.0f, 0.0f, 100.0f);
    } else {
        m_stats.foodHappiness = 50.0f;
    }

    // Housing happiness
    if (m_stats.population > 0) {
        float housingRatio = static_cast<float>(m_stats.housingCapacity) / m_stats.population;
        m_stats.housingHappiness = std::clamp(housingRatio * 50.0f, 0.0f, 100.0f);

        // Bonus from nice housing
        float housingBonus = 0.0f;
        for (const auto& building : m_buildings) {
            const auto& def = getBuildingDef(building.type);
            if (def.category == BuildingCategory::HOUSING) {
                housingBonus += def.happinessBonus;
            }
        }
        m_stats.housingHappiness += housingBonus / std::max(1, countBuildingsByCategory(BuildingCategory::HOUSING));
    }

    // Job happiness
    if (m_stats.population > 0) {
        float employmentRate = static_cast<float>(m_stats.employed) / m_stats.population;
        m_stats.jobHappiness = employmentRate * 100.0f;
    }

    // Service happiness (religion, health, entertainment)
    m_stats.religionHappiness = 30.0f; // Base
    m_stats.healthHappiness = 30.0f;
    m_stats.entertainmentHappiness = 30.0f;

    for (const auto& building : m_buildings) {
        const auto& def = getBuildingDef(building.type);
        if (def.category == BuildingCategory::SERVICE) {
            // Distribute bonus based on building type
            if (building.type == BuildingType::CHAPEL || building.type == BuildingType::CHURCH) {
                m_stats.religionHappiness += def.happinessBonus;
            } else if (building.type == BuildingType::CLINIC || building.type == BuildingType::HOSPITAL) {
                m_stats.healthHappiness += def.happinessBonus;
            } else if (building.type == BuildingType::TAVERN || building.type == BuildingType::THEATER) {
                m_stats.entertainmentHappiness += def.happinessBonus;
            }
        }
    }

    m_stats.religionHappiness = std::clamp(m_stats.religionHappiness, 0.0f, 100.0f);
    m_stats.healthHappiness = std::clamp(m_stats.healthHappiness, 0.0f, 100.0f);
    m_stats.entertainmentHappiness = std::clamp(m_stats.entertainmentHappiness, 0.0f, 100.0f);

    // Overall happiness is weighted average
    m_stats.overallHappiness = (
        m_stats.foodHappiness * 0.25f +
        m_stats.housingHappiness * 0.20f +
        m_stats.jobHappiness * 0.20f +
        m_stats.religionHappiness * 0.10f +
        m_stats.healthHappiness * 0.15f +
        m_stats.entertainmentHappiness * 0.10f
    );

    // Tax penalty
    m_stats.overallHappiness -= m_taxRate * 50.0f; // High taxes hurt happiness
    m_stats.overallHappiness = std::clamp(m_stats.overallHappiness, 0.0f, 100.0f);
}

void CityGovernor::updateEconomy(float deltaTime) {
    // Daily income from taxes
    m_stats.dailyIncome = m_stats.population * m_taxRate * 2.0f;

    // Daily expenses from building maintenance
    m_stats.dailyExpenses = 0.0f;
    for (const auto& building : m_buildings) {
        const auto& def = getBuildingDef(building.type);
        m_stats.dailyExpenses += def.maintenanceCost;
    }

    // Update treasury
    m_stats.treasury += m_stats.dailyIncome - m_stats.dailyExpenses;

    // Calculate food supply/demand
    m_stats.foodDemand = m_stats.population * 0.5f; // Each person needs 0.5 food/day
    m_stats.foodSupply = calculateProductionCapacity(BuildingCategory::FOOD);
}

void CityGovernor::updateResearch(float deltaTime) {
    // Research speed based on education buildings
    float researchSpeed = 0.1f; // Base speed

    if (countBuildings(BuildingType::SCHOOL) > 0) {
        researchSpeed += 0.2f;
    }
    if (countBuildings(BuildingType::UNIVERSITY) > 0) {
        researchSpeed += 0.5f;
    }

    // Progress toward next tech level
    if (m_stats.currentTech < TechLevel::MODERN) {
        m_stats.researchProgress += researchSpeed * deltaTime * 0.01f;

        // Tech level thresholds
        float threshold = 100.0f * (static_cast<int>(m_stats.currentTech) + 1);

        if (m_stats.researchProgress >= threshold) {
            m_stats.currentTech = static_cast<TechLevel>(static_cast<int>(m_stats.currentTech) + 1);
        }
    }
}

void CityGovernor::updateBuildings(float deltaTime) {
    for (auto& building : m_buildings) {
        // Check operational status
        const auto& def = getBuildingDef(building.type);

        // Power check
        if (def.requiresPower) {
            building.hasPower = countBuildings(BuildingType::POWER_PLANT) > 0;
        }

        // Water check
        if (def.requiresWater) {
            building.hasWater = countBuildings(BuildingType::WATER_TOWER) > 0;
        }

        building.isOperational = (!def.requiresPower || building.hasPower) &&
                                 (!def.requiresWater || building.hasWater);

        // Efficiency based on workers
        if (def.jobsProvided > 0) {
            building.efficiency = building.isOperational ?
                static_cast<float>(building.workers) / def.jobsProvided : 0.0f;
        } else {
            building.efficiency = building.isOperational ? 1.0f : 0.0f;
        }
    }
}

void CityGovernor::evaluatePriorities() {
    // Evaluate city needs and set priority

    // Critical: starvation
    if (m_stats.foodSupply < m_stats.foodDemand * 0.5f) {
        m_currentPriority = GovernorPriority::PRODUCE_FOOD;
        return;
    }

    // Critical: no housing
    if (m_stats.housingCapacity < m_stats.population) {
        m_currentPriority = GovernorPriority::BUILD_HOUSING;
        return;
    }

    // Critical: broke
    if (m_stats.treasury < 0) {
        m_currentPriority = GovernorPriority::BALANCE_BUDGET;
        return;
    }

    // Important: unhappy citizens
    if (m_stats.overallHappiness < 40.0f) {
        m_currentPriority = GovernorPriority::INCREASE_HAPPINESS;
        return;
    }

    // Important: unemployment
    if (m_stats.unemployed > m_stats.population * 0.3f) {
        m_currentPriority = GovernorPriority::CREATE_JOBS;
        return;
    }

    // Growth: food surplus allows growth
    if (m_stats.foodSupply < m_stats.foodDemand * 1.2f) {
        m_currentPriority = GovernorPriority::PRODUCE_FOOD;
        return;
    }

    // Growth: housing for growth
    if (m_stats.housingCapacity < m_stats.population * 1.2f) {
        m_currentPriority = GovernorPriority::BUILD_HOUSING;
        return;
    }

    // Default: grow economy
    m_currentPriority = GovernorPriority::BUILD_ECONOMY;
}

BuildingType CityGovernor::decideToBuild() {
    // Based on priority, decide what to build

    switch (m_currentPriority) {
        case GovernorPriority::PRODUCE_FOOD:
            if (canBuild(BuildingType::GREENHOUSE)) return BuildingType::GREENHOUSE;
            if (canBuild(BuildingType::RANCH)) return BuildingType::RANCH;
            if (canBuild(BuildingType::FARM)) return BuildingType::FARM;
            break;

        case GovernorPriority::BUILD_HOUSING:
            if (canBuild(BuildingType::APARTMENT)) return BuildingType::APARTMENT;
            if (canBuild(BuildingType::HOUSE)) return BuildingType::HOUSE;
            if (canBuild(BuildingType::SHACK)) return BuildingType::SHACK;
            break;

        case GovernorPriority::CREATE_JOBS:
            if (canBuild(BuildingType::FACTORY)) return BuildingType::FACTORY;
            if (canBuild(BuildingType::WORKSHOP)) return BuildingType::WORKSHOP;
            if (canBuild(BuildingType::MARKET)) return BuildingType::MARKET;
            break;

        case GovernorPriority::INCREASE_HAPPINESS:
            // Check what's lowest
            if (m_stats.religionHappiness < 50.0f) {
                if (canBuild(BuildingType::CHURCH)) return BuildingType::CHURCH;
                if (canBuild(BuildingType::CHAPEL)) return BuildingType::CHAPEL;
            }
            if (m_stats.healthHappiness < 50.0f) {
                if (canBuild(BuildingType::HOSPITAL)) return BuildingType::HOSPITAL;
                if (canBuild(BuildingType::CLINIC)) return BuildingType::CLINIC;
            }
            if (m_stats.entertainmentHappiness < 50.0f) {
                if (canBuild(BuildingType::THEATER)) return BuildingType::THEATER;
                if (canBuild(BuildingType::TAVERN)) return BuildingType::TAVERN;
            }
            break;

        case GovernorPriority::BUILD_ECONOMY:
            // Build production chain
            if (countBuildings(BuildingType::LUMBER_MILL) == 0 && canBuild(BuildingType::LUMBER_MILL)) {
                return BuildingType::LUMBER_MILL;
            }
            if (countBuildings(BuildingType::QUARRY) == 0 && canBuild(BuildingType::QUARRY)) {
                return BuildingType::QUARRY;
            }
            if (countBuildings(BuildingType::MINE) == 0 && canBuild(BuildingType::MINE)) {
                return BuildingType::MINE;
            }
            if (countBuildings(BuildingType::FOUNDRY) == 0 && canBuild(BuildingType::FOUNDRY)) {
                return BuildingType::FOUNDRY;
            }
            if (canBuild(BuildingType::WAREHOUSE)) return BuildingType::WAREHOUSE;
            break;

        case GovernorPriority::ADVANCE_TECH:
            if (countBuildings(BuildingType::SCHOOL) == 0 && canBuild(BuildingType::SCHOOL)) {
                return BuildingType::SCHOOL;
            }
            if (canBuild(BuildingType::UNIVERSITY)) return BuildingType::UNIVERSITY;
            break;

        case GovernorPriority::BALANCE_BUDGET:
            // Don't build anything when broke
            break;

        default:
            break;
    }

    return BuildingType::COUNT; // Nothing to build
}

uint32_t CityGovernor::findBuildLocation(BuildingType type) {
    // Find an available location for the building
    // For now, just return first available location
    for (uint32_t nodeId : m_availableLocations) {
        if (!getBuildingAtNode(nodeId)) {
            return nodeId;
        }
    }
    return 0;
}

bool CityGovernor::canBuild(BuildingType type) const {
    const auto& def = getBuildingDef(type);

    // Tech requirement
    if (def.requiredTech > m_stats.currentTech) {
        return false;
    }

    // Cost check
    if (m_stats.treasury < def.buildCost) {
        return false;
    }

    // Resource checks would go here (check economy system for materials)

    return true;
}

bool CityGovernor::build(BuildingType type, uint32_t graphNodeId) {
    if (!canBuild(type)) {
        return false;
    }

    const auto& def = getBuildingDef(type);

    // Deduct costs
    m_stats.treasury -= def.buildCost;

    // Create building
    Building building;
    building.id = m_nextBuildingId++;
    building.type = type;
    building.graphNodeId = graphNodeId;
    building.name = def.name + "_" + std::to_string(building.id);
    building.maxWorkers = def.jobsProvided;

    if (def.category == BuildingCategory::HOUSING) {
        building.maxResidents = def.housingCapacity;
    }

    m_buildings.push_back(building);

    // TODO: Economy integration disabled - will be updated when building types are finalized
    // The CityGovernor economy integration needs to be rewritten for the new goods system
    (void)graphNodeId;  // Suppress unused warning

    // Notify callback
    if (m_onBuildingPlaced) {
        m_onBuildingPlaced(building, def);
    }

    return true;
}

void CityGovernor::demolish(uint32_t buildingId) {
    auto it = std::find_if(m_buildings.begin(), m_buildings.end(),
        [buildingId](const Building& b) { return b.id == buildingId; });

    if (it != m_buildings.end()) {
        if (m_economy) {
            m_economy->unregisterNode(it->graphNodeId);
        }
        m_buildings.erase(it);
    }
}

const Building* CityGovernor::getBuilding(uint32_t id) const {
    for (const auto& b : m_buildings) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

const Building* CityGovernor::getBuildingAtNode(uint32_t graphNodeId) const {
    for (const auto& b : m_buildings) {
        if (b.graphNodeId == graphNodeId) return &b;
    }
    return nullptr;
}

void CityGovernor::initializeCity(const std::vector<uint32_t>& startingNodeIds) {
    m_availableLocations = startingNodeIds;

    // Start with basic buildings if we have enough locations
    if (startingNodeIds.size() >= 3) {
        build(BuildingType::TRADING_POST, startingNodeIds[0]);
        build(BuildingType::FARM, startingNodeIds[1]);
        build(BuildingType::SHACK, startingNodeIds[2]);
    }
}

int CityGovernor::countBuildings(BuildingType type) const {
    return static_cast<int>(std::count_if(m_buildings.begin(), m_buildings.end(),
        [type](const Building& b) { return b.type == type; }));
}

int CityGovernor::countBuildingsByCategory(BuildingCategory cat) const {
    int count = 0;
    for (const auto& b : m_buildings) {
        if (getBuildingDef(b.type).category == cat) {
            count++;
        }
    }
    return count;
}

float CityGovernor::calculateProductionCapacity(BuildingCategory cat) const {
    float total = 0.0f;
    for (const auto& b : m_buildings) {
        const auto& def = getBuildingDef(b.type);
        if (def.category == cat) {
            total += def.productionRate * b.efficiency;
        }
    }
    return total;
}

const CityBuildingDef& CityGovernor::getBuildingDef(BuildingType type) {
    static CityBuildingDef empty;
    auto it = s_buildingDefs.find(type);
    return it != s_buildingDefs.end() ? it->second : empty;
}

const char* CityGovernor::getBuildingName(BuildingType type) {
    const auto& def = getBuildingDef(type);
    return def.name.c_str();
}

const char* CityGovernor::getCategoryName(BuildingCategory cat) {
    switch (cat) {
        case BuildingCategory::HOUSING:        return "Housing";
        case BuildingCategory::FOOD:           return "Food";
        case BuildingCategory::RESOURCE:       return "Resource";
        case BuildingCategory::INDUSTRY:       return "Industry";
        case BuildingCategory::SERVICE:        return "Service";
        case BuildingCategory::INFRASTRUCTURE: return "Infrastructure";
        case BuildingCategory::COMMERCIAL:     return "Commercial";
        case BuildingCategory::MILITARY:       return "Military";
        default:                               return "Unknown";
    }
}

const char* CityGovernor::getTechLevelName(TechLevel level) {
    switch (level) {
        case TechLevel::PRIMITIVE:    return "Primitive";
        case TechLevel::BASIC:        return "Basic";
        case TechLevel::INTERMEDIATE: return "Intermediate";
        case TechLevel::ADVANCED:     return "Advanced";
        case TechLevel::MODERN:       return "Modern";
        default:                      return "Unknown";
    }
}

} // namespace eden
