#include "EconomySystem.hpp"
#include <algorithm>
#include <cmath>

namespace eden {

EconomySystem::EconomySystem() {
    initializeMarkets();
}

void EconomySystem::initializeMarkets() {
    // Set up base prices and characteristics for each good

    // === NEW RAW RESOURCES ===
    auto& oil = m_markets[GoodType::OIL];
    oil.type = GoodType::OIL;
    oil.basePrice = 8.0f;
    oil.currentPrice = 8.0f;
    oil.priceVolatility = 0.18f;  // Oil prices are volatile

    auto& naturalGas = m_markets[GoodType::NATURAL_GAS];
    naturalGas.type = GoodType::NATURAL_GAS;
    naturalGas.basePrice = 6.0f;
    naturalGas.currentPrice = 6.0f;
    naturalGas.priceVolatility = 0.15f;

    auto& limestone = m_markets[GoodType::LIMESTONE];
    limestone.type = GoodType::LIMESTONE;
    limestone.basePrice = 4.0f;
    limestone.currentPrice = 4.0f;
    limestone.priceVolatility = 0.06f;  // Very stable

    auto& coal = m_markets[GoodType::COAL];
    coal.type = GoodType::COAL;
    coal.basePrice = 5.0f;
    coal.currentPrice = 5.0f;
    coal.priceVolatility = 0.08f;

    auto& phosphates = m_markets[GoodType::PHOSPHATES];
    phosphates.type = GoodType::PHOSPHATES;
    phosphates.basePrice = 7.0f;
    phosphates.currentPrice = 7.0f;
    phosphates.priceVolatility = 0.1f;

    auto& sulfur = m_markets[GoodType::SULFUR];
    sulfur.type = GoodType::SULFUR;
    sulfur.basePrice = 6.0f;
    sulfur.currentPrice = 6.0f;
    sulfur.priceVolatility = 0.1f;

    auto& grain = m_markets[GoodType::GRAIN];
    grain.type = GoodType::GRAIN;
    grain.basePrice = 3.0f;
    grain.currentPrice = 3.0f;
    grain.priceVolatility = 0.12f;  // Weather dependent

    auto& cotton = m_markets[GoodType::COTTON];
    cotton.type = GoodType::COTTON;
    cotton.basePrice = 4.0f;
    cotton.currentPrice = 4.0f;
    cotton.priceVolatility = 0.1f;

    auto& fish = m_markets[GoodType::FISH];
    fish.type = GoodType::FISH;
    fish.basePrice = 4.0f;
    fish.currentPrice = 4.0f;
    fish.priceVolatility = 0.15f;  // Seasonal

    auto& meat = m_markets[GoodType::MEAT];
    meat.type = GoodType::MEAT;
    meat.basePrice = 6.0f;
    meat.currentPrice = 6.0f;
    meat.priceVolatility = 0.1f;

    // === CONSTRUCTION RAW RESOURCES ===
    auto& timber = m_markets[GoodType::TIMBER];
    timber.type = GoodType::TIMBER;
    timber.basePrice = 5.0f;
    timber.currentPrice = 5.0f;
    timber.priceVolatility = 0.08f;  // Stable

    auto& sand = m_markets[GoodType::SAND];
    sand.type = GoodType::SAND;
    sand.basePrice = 2.0f;
    sand.currentPrice = 2.0f;
    sand.priceVolatility = 0.05f;  // Very stable, bulk commodity

    auto& gravel = m_markets[GoodType::GRAVEL];
    gravel.type = GoodType::GRAVEL;
    gravel.basePrice = 3.0f;
    gravel.currentPrice = 3.0f;
    gravel.priceVolatility = 0.05f;  // Very stable

    // === PROCESSED CONSTRUCTION MATERIALS ===
    auto& lumber = m_markets[GoodType::LUMBER];
    lumber.type = GoodType::LUMBER;
    lumber.basePrice = 12.0f;
    lumber.currentPrice = 12.0f;
    lumber.priceVolatility = 0.1f;

    auto& steel = m_markets[GoodType::STEEL];
    steel.type = GoodType::STEEL;
    steel.basePrice = 25.0f;
    steel.currentPrice = 25.0f;
    steel.priceVolatility = 0.12f;

    auto& concrete = m_markets[GoodType::CONCRETE];
    concrete.type = GoodType::CONCRETE;
    concrete.basePrice = 15.0f;
    concrete.currentPrice = 15.0f;
    concrete.priceVolatility = 0.08f;

    auto& chemicals = m_markets[GoodType::CHEMICALS];
    chemicals.type = GoodType::CHEMICALS;
    chemicals.basePrice = 15.0f;
    chemicals.currentPrice = 15.0f;
    chemicals.priceVolatility = 0.12f;

    auto& plastics = m_markets[GoodType::PLASTICS];
    plastics.type = GoodType::PLASTICS;
    plastics.basePrice = 12.0f;
    plastics.currentPrice = 12.0f;
    plastics.priceVolatility = 0.1f;

    auto& explosives = m_markets[GoodType::EXPLOSIVES];
    explosives.type = GoodType::EXPLOSIVES;
    explosives.basePrice = 50.0f;
    explosives.currentPrice = 50.0f;
    explosives.priceVolatility = 0.15f;

    auto& flares = m_markets[GoodType::FLARES];
    flares.type = GoodType::FLARES;
    flares.basePrice = 8.0f;
    flares.currentPrice = 8.0f;
    flares.priceVolatility = 0.1f;

    auto& pureWater = m_markets[GoodType::PURE_WATER];
    pureWater.type = GoodType::PURE_WATER;
    pureWater.basePrice = 5.0f;
    pureWater.currentPrice = 5.0f;
    pureWater.priceVolatility = 0.08f;

    auto& cigars = m_markets[GoodType::CIGARS];
    cigars.type = GoodType::CIGARS;
    cigars.basePrice = 25.0f;
    cigars.currentPrice = 25.0f;
    cigars.priceVolatility = 0.1f;

    auto& food = m_markets[GoodType::FOOD];
    food.type = GoodType::FOOD;
    food.basePrice = 5.0f;
    food.currentPrice = 5.0f;
    food.priceVolatility = 0.15f;

    auto& furs = m_markets[GoodType::FURS];
    furs.type = GoodType::FURS;
    furs.basePrice = 30.0f;
    furs.currentPrice = 30.0f;
    furs.priceVolatility = 0.12f;

    auto& ore = m_markets[GoodType::ORE];
    ore.type = GoodType::ORE;
    ore.basePrice = 10.0f;
    ore.currentPrice = 10.0f;
    ore.priceVolatility = 0.1f;

    auto& gems = m_markets[GoodType::GEMS];
    gems.type = GoodType::GEMS;
    gems.basePrice = 100.0f;
    gems.currentPrice = 100.0f;
    gems.priceVolatility = 0.2f;  // Gems are volatile

    auto& constmat = m_markets[GoodType::CONSTMAT];
    constmat.type = GoodType::CONSTMAT;
    constmat.basePrice = 8.0f;
    constmat.currentPrice = 8.0f;
    constmat.priceVolatility = 0.08f;  // Construction materials are stable

    auto& fuel = m_markets[GoodType::FUEL];
    fuel.type = GoodType::FUEL;
    fuel.basePrice = 15.0f;
    fuel.currentPrice = 15.0f;
    fuel.priceVolatility = 0.2f;

    auto& scrapMetal = m_markets[GoodType::SCRAP_METAL];
    scrapMetal.type = GoodType::SCRAP_METAL;
    scrapMetal.basePrice = 3.0f;
    scrapMetal.currentPrice = 3.0f;
    scrapMetal.priceVolatility = 0.15f;

    // Processed materials
    auto& sheetMetal = m_markets[GoodType::SHEET_METAL];
    sheetMetal.type = GoodType::SHEET_METAL;
    sheetMetal.basePrice = 20.0f;
    sheetMetal.currentPrice = 20.0f;
    sheetMetal.priceVolatility = 0.1f;

    auto& exMetal = m_markets[GoodType::EX_METAL];
    exMetal.type = GoodType::EX_METAL;
    exMetal.basePrice = 45.0f;
    exMetal.currentPrice = 45.0f;
    exMetal.priceVolatility = 0.12f;

    // Components
    auto& compComp = m_markets[GoodType::COMP_COMP];
    compComp.type = GoodType::COMP_COMP;
    compComp.basePrice = 35.0f;
    compComp.currentPrice = 35.0f;
    compComp.priceVolatility = 0.1f;

    auto& machParts = m_markets[GoodType::MACH_PARTS];
    machParts.type = GoodType::MACH_PARTS;
    machParts.basePrice = 30.0f;
    machParts.currentPrice = 30.0f;
    machParts.priceVolatility = 0.1f;

    auto& cell1 = m_markets[GoodType::CELL_1];
    cell1.type = GoodType::CELL_1;
    cell1.basePrice = 25.0f;
    cell1.currentPrice = 25.0f;
    cell1.priceVolatility = 0.08f;

    auto& cell2 = m_markets[GoodType::CELL_2];
    cell2.type = GoodType::CELL_2;
    cell2.basePrice = 40.0f;
    cell2.currentPrice = 40.0f;
    cell2.priceVolatility = 0.08f;

    auto& cell3 = m_markets[GoodType::CELL_3];
    cell3.type = GoodType::CELL_3;
    cell3.basePrice = 60.0f;
    cell3.currentPrice = 60.0f;
    cell3.priceVolatility = 0.08f;

    auto& cell4 = m_markets[GoodType::CELL_4];
    cell4.type = GoodType::CELL_4;
    cell4.basePrice = 85.0f;
    cell4.currentPrice = 85.0f;
    cell4.priceVolatility = 0.08f;

    auto& fusionParts = m_markets[GoodType::FUSION_PARTS];
    fusionParts.type = GoodType::FUSION_PARTS;
    fusionParts.basePrice = 75.0f;
    fusionParts.currentPrice = 75.0f;
    fusionParts.priceVolatility = 0.1f;

    auto& laser = m_markets[GoodType::LASER];
    laser.type = GoodType::LASER;
    laser.basePrice = 50.0f;
    laser.currentPrice = 50.0f;
    laser.priceVolatility = 0.12f;

    // Engines & Pods
    auto& engine1 = m_markets[GoodType::ENGINE_1];
    engine1.type = GoodType::ENGINE_1;
    engine1.basePrice = 150.0f;
    engine1.currentPrice = 150.0f;
    engine1.priceVolatility = 0.1f;

    auto& engine2 = m_markets[GoodType::ENGINE_2];
    engine2.type = GoodType::ENGINE_2;
    engine2.basePrice = 250.0f;
    engine2.currentPrice = 250.0f;
    engine2.priceVolatility = 0.1f;

    auto& podSmallest = m_markets[GoodType::POD_SMALLEST];
    podSmallest.type = GoodType::POD_SMALLEST;
    podSmallest.basePrice = 80.0f;
    podSmallest.currentPrice = 80.0f;
    podSmallest.priceVolatility = 0.08f;

    auto& podSmall = m_markets[GoodType::POD_SMALL];
    podSmall.type = GoodType::POD_SMALL;
    podSmall.basePrice = 120.0f;
    podSmall.currentPrice = 120.0f;
    podSmall.priceVolatility = 0.08f;

    auto& podMedium = m_markets[GoodType::POD_MEDIUM];
    podMedium.type = GoodType::POD_MEDIUM;
    podMedium.basePrice = 180.0f;
    podMedium.currentPrice = 180.0f;
    podMedium.priceVolatility = 0.08f;

    auto& podLarge = m_markets[GoodType::POD_LARGE];
    podLarge.type = GoodType::POD_LARGE;
    podLarge.basePrice = 250.0f;
    podLarge.currentPrice = 250.0f;
    podLarge.priceVolatility = 0.08f;

    // Weapons
    auto& sprat = m_markets[GoodType::SPRAT];
    sprat.type = GoodType::SPRAT;
    sprat.basePrice = 60.0f;
    sprat.currentPrice = 60.0f;
    sprat.priceVolatility = 0.15f;

    auto& swarm = m_markets[GoodType::SWARM];
    swarm.type = GoodType::SWARM;
    swarm.basePrice = 80.0f;
    swarm.currentPrice = 80.0f;
    swarm.priceVolatility = 0.15f;

    auto& devastator = m_markets[GoodType::DEVASTATOR];
    devastator.type = GoodType::DEVASTATOR;
    devastator.basePrice = 200.0f;
    devastator.currentPrice = 200.0f;
    devastator.priceVolatility = 0.12f;

    auto& hologram = m_markets[GoodType::HOLOGRAM];
    hologram.type = GoodType::HOLOGRAM;
    hologram.basePrice = 45.0f;
    hologram.currentPrice = 45.0f;
    hologram.priceVolatility = 0.1f;

    auto& chaff = m_markets[GoodType::CHAFF];
    chaff.type = GoodType::CHAFF;
    chaff.basePrice = 30.0f;
    chaff.currentPrice = 30.0f;
    chaff.priceVolatility = 0.1f;

    auto& salvageDrone = m_markets[GoodType::SALVAGE_DRONE];
    salvageDrone.type = GoodType::SALVAGE_DRONE;
    salvageDrone.basePrice = 100.0f;
    salvageDrone.currentPrice = 100.0f;
    salvageDrone.priceVolatility = 0.1f;

    // Consumables
    auto& narcotics = m_markets[GoodType::NARCOTICS];
    narcotics.type = GoodType::NARCOTICS;
    narcotics.basePrice = 40.0f;
    narcotics.currentPrice = 40.0f;
    narcotics.priceVolatility = 0.25f;  // Illegal goods are volatile

    auto& alcohol = m_markets[GoodType::ALCOHOL];
    alcohol.type = GoodType::ALCOHOL;
    alcohol.basePrice = 20.0f;
    alcohol.currentPrice = 20.0f;
    alcohol.priceVolatility = 0.12f;

    auto& medicine = m_markets[GoodType::MEDICINE];
    medicine.type = GoodType::MEDICINE;
    medicine.basePrice = 35.0f;
    medicine.currentPrice = 35.0f;
    medicine.priceVolatility = 0.15f;

    auto& textiles = m_markets[GoodType::TEXTILES];
    textiles.type = GoodType::TEXTILES;
    textiles.basePrice = 12.0f;
    textiles.currentPrice = 12.0f;
    textiles.priceVolatility = 0.08f;

    // Moths (vehicles) - expensive!
    auto& mothSilverY = m_markets[GoodType::MOTH_SILVER_Y];
    mothSilverY.type = GoodType::MOTH_SILVER_Y;
    mothSilverY.basePrice = 2000.0f;
    mothSilverY.currentPrice = 2000.0f;
    mothSilverY.priceVolatility = 0.08f;

    auto& mothSwallow = m_markets[GoodType::MOTH_SWALLOW];
    mothSwallow.type = GoodType::MOTH_SWALLOW;
    mothSwallow.basePrice = 1500.0f;
    mothSwallow.currentPrice = 1500.0f;
    mothSwallow.priceVolatility = 0.08f;

    auto& mothHawk = m_markets[GoodType::MOTH_HAWK];
    mothHawk.type = GoodType::MOTH_HAWK;
    mothHawk.basePrice = 3000.0f;
    mothHawk.currentPrice = 3000.0f;
    mothHawk.priceVolatility = 0.08f;

    auto& mothNeoTiger = m_markets[GoodType::MOTH_NEO_TIGER];
    mothNeoTiger.type = GoodType::MOTH_NEO_TIGER;
    mothNeoTiger.basePrice = 4000.0f;
    mothNeoTiger.currentPrice = 4000.0f;
    mothNeoTiger.priceVolatility = 0.08f;

    auto& mothMoon = m_markets[GoodType::MOTH_MOON];
    mothMoon.type = GoodType::MOTH_MOON;
    mothMoon.basePrice = 2500.0f;
    mothMoon.currentPrice = 2500.0f;
    mothMoon.priceVolatility = 0.08f;

    auto& mothPolice = m_markets[GoodType::MOTH_POLICE];
    mothPolice.type = GoodType::MOTH_POLICE;
    mothPolice.basePrice = 3500.0f;
    mothPolice.currentPrice = 3500.0f;
    mothPolice.priceVolatility = 0.08f;

    auto& mothDeathsHead = m_markets[GoodType::MOTH_DEATHS_HEAD];
    mothDeathsHead.type = GoodType::MOTH_DEATHS_HEAD;
    mothDeathsHead.basePrice = 5000.0f;
    mothDeathsHead.currentPrice = 5000.0f;
    mothDeathsHead.priceVolatility = 0.08f;
}

void EconomySystem::update(float deltaTime, float gameTimeMinutes) {
    m_timeSinceLastUpdate += deltaTime;

    // Only update economy at intervals (not every frame)
    if (m_timeSinceLastUpdate < UPDATE_INTERVAL) {
        return;
    }
    m_timeSinceLastUpdate = 0.0f;

    // Run economy simulation
    updateProduction(UPDATE_INTERVAL);
    updateConsumption(UPDATE_INTERVAL);
    updatePrices(gameTimeMinutes);
    checkAndEmitSignals(gameTimeMinutes);
}

void EconomySystem::updateProduction(float deltaTime) {
    for (auto& [nodeId, node] : m_nodes) {
        for (const auto& prod : node.produces) {
            // Calculate production for this interval
            float produced = prod.rate * prod.efficiency * deltaTime * m_economySpeed;

            // Add to node's local inventory
            float& inv = node.inventory[prod.good];
            float maxInv = node.maxInventory.count(prod.good) ?
                           node.maxInventory.at(prod.good) : 1000.0f;

            float actualProduced = std::min(produced, maxInv - inv);
            inv += actualProduced;

            // Update global supply
            m_markets[prod.good].globalSupply += actualProduced;
        }
    }
}

void EconomySystem::updateConsumption(float deltaTime) {
    // Population consumes food
    float foodNeeded = m_population * 0.01f * deltaTime * m_economySpeed;
    m_markets[GoodType::FOOD].globalDemand += foodNeeded;

    // Population creates demand for luxury goods (cigars, furs)
    float luxuryNeeded = m_population * 0.003f * deltaTime * m_economySpeed;
    m_markets[GoodType::CIGARS].globalDemand += luxuryNeeded;
    m_markets[GoodType::FURS].globalDemand += luxuryNeeded * 0.5f;

    // Node-based consumption
    for (auto& [nodeId, node] : m_nodes) {
        for (const auto& cons : node.consumes) {
            float demanded = cons.rate * deltaTime * m_economySpeed;
            m_markets[cons.good].globalDemand += demanded;

            // Try to consume from local inventory
            float& inv = node.inventory[cons.good];
            float consumed = std::min(inv, demanded);
            inv -= consumed;

            // Reduce global supply
            if (consumed > 0) {
                m_markets[cons.good].globalSupply -= consumed;
            }
        }
    }

    // Decay demand over time (so it doesn't accumulate forever)
    for (auto& [type, market] : m_markets) {
        market.globalDemand *= 0.95f;  // 5% decay per update
        market.globalSupply = std::max(0.0f, market.globalSupply);
        market.globalDemand = std::max(0.1f, market.globalDemand);  // Minimum demand
    }
}

void EconomySystem::updatePrices(float gameTimeMinutes) {
    for (auto& [type, market] : m_markets) {
        market.lastPrice = market.currentPrice;

        // Price based on supply/demand ratio
        float ratio = getSupplyDemandRatio(type);

        // Price formula: inverse relationship with supply/demand
        // High supply, low demand = low price
        // Low supply, high demand = high price
        float targetPrice = market.basePrice;

        if (ratio > 0.01f) {
            // Inverse relationship: price = base / ratio
            // Clamped to reasonable bounds
            targetPrice = market.basePrice / std::sqrt(ratio);
            targetPrice = std::clamp(targetPrice, market.basePrice * 0.25f, market.basePrice * 4.0f);
        } else {
            // Extreme shortage
            targetPrice = market.basePrice * 4.0f;
        }

        // Smooth price changes based on volatility
        float priceChange = (targetPrice - market.currentPrice) * market.priceVolatility;
        market.currentPrice += priceChange;

        // Track rate of change
        market.priceChangeRate = (market.currentPrice - market.lastPrice) / market.lastPrice;
    }
}

void EconomySystem::checkAndEmitSignals(float gameTimeMinutes) {
    for (const auto& [type, market] : m_markets) {
        float ratio = getSupplyDemandRatio(type);

        // Check for shortage
        if (ratio < market.shortageThreshold) {
            EconomySignal sig;
            sig.type = EconomySignalType::SHORTAGE;
            sig.good = type;
            sig.locationNodeId = 0;  // Global signal
            sig.magnitude = 1.0f - (ratio / market.shortageThreshold);
            sig.message = std::string(getGoodName(type)) + " shortage! Price: $" +
                          std::to_string(static_cast<int>(market.currentPrice));
            sig.gameTime = gameTimeMinutes;
            emitSignal(sig);
        }

        // Check for surplus
        if (ratio > market.surplusThreshold) {
            EconomySignal sig;
            sig.type = EconomySignalType::SURPLUS;
            sig.good = type;
            sig.locationNodeId = 0;
            sig.magnitude = std::min(1.0f, (ratio - market.surplusThreshold) / market.surplusThreshold);
            sig.message = std::string(getGoodName(type)) + " surplus - good buying opportunity!";
            sig.gameTime = gameTimeMinutes;
            emitSignal(sig);
        }

        // Check for significant price changes
        if (std::abs(market.priceChangeRate) > market.priceChangeThreshold) {
            EconomySignal sig;
            sig.type = market.priceChangeRate > 0 ? EconomySignalType::PRICE_SPIKE : EconomySignalType::PRICE_DROP;
            sig.good = type;
            sig.locationNodeId = 0;
            sig.magnitude = std::abs(market.priceChangeRate);
            int percentChange = static_cast<int>(market.priceChangeRate * 100);
            sig.message = std::string(getGoodName(type)) +
                          (percentChange > 0 ? " +" : " ") +
                          std::to_string(percentChange) + "%";
            sig.gameTime = gameTimeMinutes;
            emitSignal(sig);
        }
    }
}

void EconomySystem::emitSignal(const EconomySignal& signal) {
    // Add to history
    m_recentSignals.push_back(signal);
    if (m_recentSignals.size() > MAX_SIGNAL_HISTORY) {
        m_recentSignals.erase(m_recentSignals.begin());
    }

    // Notify all subscribers
    for (const auto& callback : m_subscribers) {
        callback(signal);
    }
}

// Market queries
float EconomySystem::getPrice(GoodType good) const {
    auto it = m_markets.find(good);
    return it != m_markets.end() ? it->second.currentPrice : 0.0f;
}

float EconomySystem::getSupplyDemandRatio(GoodType good) const {
    auto it = m_markets.find(good);
    if (it == m_markets.end()) return 1.0f;

    const auto& market = it->second;
    if (market.globalDemand < 0.01f) return 100.0f;  // No demand = infinite supply ratio
    return market.globalSupply / market.globalDemand;
}

const GoodMarket& EconomySystem::getMarket(GoodType good) const {
    static GoodMarket empty;
    auto it = m_markets.find(good);
    return it != m_markets.end() ? it->second : empty;
}

bool EconomySystem::isShortage(GoodType good) const {
    const auto& market = getMarket(good);
    return getSupplyDemandRatio(good) < market.shortageThreshold;
}

bool EconomySystem::isSurplus(GoodType good) const {
    const auto& market = getMarket(good);
    return getSupplyDemandRatio(good) > market.surplusThreshold;
}

// Node management
void EconomySystem::registerNode(const EconomyNode& node) {
    m_nodes[node.graphNodeId] = node;

    // Emit signal for new production
    for (const auto& prod : node.produces) {
        EconomySignal sig;
        sig.type = EconomySignalType::PRODUCTION_ONLINE;
        sig.good = prod.good;
        sig.locationNodeId = node.graphNodeId;
        sig.magnitude = prod.rate / 10.0f;  // Relative significance
        sig.message = node.name + " now producing " + getGoodName(prod.good);
        sig.gameTime = 0;  // Will be set properly if called during game
        emitSignal(sig);
    }

    // Emit signal for new demand
    for (const auto& cons : node.consumes) {
        EconomySignal sig;
        sig.type = EconomySignalType::NEW_DEMAND;
        sig.good = cons.good;
        sig.locationNodeId = node.graphNodeId;
        sig.magnitude = cons.rate / 10.0f;
        sig.message = node.name + " now buying " + getGoodName(cons.good);
        sig.gameTime = 0;
        emitSignal(sig);
    }
}

void EconomySystem::unregisterNode(uint32_t graphNodeId) {
    auto it = m_nodes.find(graphNodeId);
    if (it != m_nodes.end()) {
        // Emit production offline signals
        for (const auto& prod : it->second.produces) {
            EconomySignal sig;
            sig.type = EconomySignalType::PRODUCTION_OFFLINE;
            sig.good = prod.good;
            sig.locationNodeId = graphNodeId;
            sig.magnitude = prod.rate / 10.0f;
            sig.message = it->second.name + " stopped producing " + getGoodName(prod.good);
            sig.gameTime = 0;
            emitSignal(sig);
        }
        m_nodes.erase(it);
    }
}

void EconomySystem::clearNodes() {
    m_nodes.clear();
}

EconomyNode* EconomySystem::getNode(uint32_t graphNodeId) {
    auto it = m_nodes.find(graphNodeId);
    return it != m_nodes.end() ? &it->second : nullptr;
}

const EconomyNode* EconomySystem::getNode(uint32_t graphNodeId) const {
    auto it = m_nodes.find(graphNodeId);
    return it != m_nodes.end() ? &it->second : nullptr;
}

// Trading location queries
std::vector<uint32_t> EconomySystem::findNodesSelling(GoodType good) const {
    std::vector<uint32_t> result;
    for (const auto& [id, node] : m_nodes) {
        // Check if produces this good and has inventory
        for (const auto& prod : node.produces) {
            if (prod.good == good) {
                auto invIt = node.inventory.find(good);
                if (invIt != node.inventory.end() && invIt->second > 0) {
                    result.push_back(id);
                }
                break;
            }
        }
    }
    return result;
}

std::vector<uint32_t> EconomySystem::findNodesBuying(GoodType good) const {
    std::vector<uint32_t> result;
    for (const auto& [id, node] : m_nodes) {
        for (const auto& cons : node.consumes) {
            if (cons.good == good) {
                result.push_back(id);
                break;
            }
        }
    }
    return result;
}

std::vector<uint32_t> EconomySystem::findBestBuyPrice(GoodType good, int maxResults) const {
    std::vector<std::pair<uint32_t, float>> priced;

    for (const auto& [id, node] : m_nodes) {
        // Check if this node sells this good
        bool sells = false;
        for (const auto& prod : node.produces) {
            if (prod.good == good) {
                sells = true;
                break;
            }
        }

        if (sells) {
            float price = getBuyPrice(id, good);
            if (price > 0 && canBuy(id, good, 1.0f)) {
                priced.emplace_back(id, price);
            }
        }
    }

    // Sort by price (lowest first = best for buyer)
    std::sort(priced.begin(), priced.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<uint32_t> result;
    for (int i = 0; i < maxResults && i < static_cast<int>(priced.size()); i++) {
        result.push_back(priced[i].first);
    }
    return result;
}

std::vector<uint32_t> EconomySystem::findBestSellPrice(GoodType good, int maxResults) const {
    std::vector<std::pair<uint32_t, float>> priced;

    for (const auto& [id, node] : m_nodes) {
        // Check if this node buys this good
        bool buys = false;
        for (const auto& cons : node.consumes) {
            if (cons.good == good) {
                buys = true;
                break;
            }
        }

        if (buys) {
            float price = getSellPrice(id, good);
            if (price > 0 && canSell(id, good, 1.0f)) {
                priced.emplace_back(id, price);
            }
        }
    }

    // Sort by price (highest first = best for seller)
    std::sort(priced.begin(), priced.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<uint32_t> result;
    for (int i = 0; i < maxResults && i < static_cast<int>(priced.size()); i++) {
        result.push_back(priced[i].first);
    }
    return result;
}

// Trading operations
bool EconomySystem::canBuy(uint32_t nodeId, GoodType good, float quantity) const {
    const auto* node = getNode(nodeId);
    if (!node) return false;

    auto invIt = node->inventory.find(good);
    return invIt != node->inventory.end() && invIt->second >= quantity;
}

bool EconomySystem::canSell(uint32_t nodeId, GoodType good, float quantity) const {
    const auto* node = getNode(nodeId);
    if (!node) return false;

    // Check if node accepts this good
    for (const auto& cons : node->consumes) {
        if (cons.good == good) {
            // Check if has inventory space
            auto invIt = node->inventory.find(good);
            auto maxIt = node->maxInventory.find(good);
            float current = invIt != node->inventory.end() ? invIt->second : 0.0f;
            float max = maxIt != node->maxInventory.end() ? maxIt->second : 1000.0f;
            return (current + quantity) <= max;
        }
    }
    return false;
}

float EconomySystem::getBuyPrice(uint32_t nodeId, GoodType good) const {
    const auto* node = getNode(nodeId);
    if (!node) return 0.0f;

    float basePrice = getPrice(good);

    // Apply local modifier if exists
    auto modIt = node->sellPriceModifier.find(good);
    float modifier = modIt != node->sellPriceModifier.end() ? modIt->second : 1.0f;

    return basePrice * modifier;
}

float EconomySystem::getSellPrice(uint32_t nodeId, GoodType good) const {
    const auto* node = getNode(nodeId);
    if (!node) return 0.0f;

    float basePrice = getPrice(good);

    // Apply local modifier and consumer priority
    auto modIt = node->buyPriceModifier.find(good);
    float modifier = modIt != node->buyPriceModifier.end() ? modIt->second : 1.0f;

    // Find consumption priority
    float priority = 1.0f;
    for (const auto& cons : node->consumes) {
        if (cons.good == good) {
            priority = cons.priority;
            break;
        }
    }

    return basePrice * modifier * priority;
}

bool EconomySystem::executeTrade(uint32_t nodeId, GoodType good, float quantity, bool buying) {
    auto* node = getNode(nodeId);
    if (!node) return false;

    if (buying) {
        if (!canBuy(nodeId, good, quantity)) return false;
        node->inventory[good] -= quantity;
        m_markets[good].globalSupply -= quantity;
    } else {
        if (!canSell(nodeId, good, quantity)) return false;
        node->inventory[good] += quantity;
        m_markets[good].globalSupply += quantity;
    }

    return true;
}

// Subscription
void EconomySystem::subscribe(EconomySignalCallback callback) {
    m_subscribers.push_back(std::move(callback));
}

void EconomySystem::unsubscribeAll() {
    m_subscribers.clear();
}

void EconomySystem::setPopulation(int population) {
    m_population = std::max(0, population);
}

// Static helpers
const char* EconomySystem::getGoodName(GoodType good) {
    switch (good) {
        // Raw resources
        case GoodType::OIL:             return "Oil";
        case GoodType::NATURAL_GAS:     return "Natural Gas";
        case GoodType::LIMESTONE:       return "Limestone";
        case GoodType::COAL:            return "Coal";
        case GoodType::PHOSPHATES:      return "Phosphates";
        case GoodType::SULFUR:          return "Sulfur";
        case GoodType::GRAIN:           return "Grain";
        case GoodType::COTTON:          return "Cotton";
        case GoodType::FISH:            return "Fish";
        case GoodType::TIMBER:          return "Timber";
        case GoodType::SAND:            return "Sand";
        case GoodType::GRAVEL:          return "Gravel";
        case GoodType::MEAT:            return "Meat";
        case GoodType::CHEMICALS:       return "Chemicals";
        case GoodType::PLASTICS:        return "Plastics";
        case GoodType::EXPLOSIVES:      return "Explosives";
        case GoodType::FLARES:          return "Flares";
        case GoodType::PURE_WATER:      return "Pure Water";
        case GoodType::CIGARS:          return "Cigars";
        case GoodType::FOOD:            return "Food";
        case GoodType::FURS:            return "Furs";
        case GoodType::ORE:             return "Ore";
        case GoodType::GEMS:            return "Gems";
        case GoodType::CONSTMAT:        return "Constmat";
        case GoodType::FUEL:            return "Fuel";
        case GoodType::SCRAP_METAL:     return "Scrap Metal";
        // Processed materials
        case GoodType::SHEET_METAL:     return "Sheet Metal";
        case GoodType::EX_METAL:        return "Exotic Metal";
        case GoodType::LUMBER:          return "Lumber";
        case GoodType::STEEL:           return "Steel";
        case GoodType::CONCRETE:        return "Concrete";
        // Components
        case GoodType::COMP_COMP:       return "CompComp";
        case GoodType::MACH_PARTS:      return "MachParts";
        case GoodType::CELL_1:          return "Cell #1";
        case GoodType::CELL_2:          return "Cell #2";
        case GoodType::CELL_3:          return "Cell #3";
        case GoodType::CELL_4:          return "Cell #4";
        case GoodType::FUSION_PARTS:    return "Fusion Parts";
        case GoodType::LASER:           return "Laser";
        // Engines & Pods
        case GoodType::ENGINE_1:        return "Engine #1";
        case GoodType::ENGINE_2:        return "Engine #2";
        case GoodType::POD_SMALLEST:    return "Smallest Pod";
        case GoodType::POD_SMALL:       return "Small Pod";
        case GoodType::POD_MEDIUM:      return "Medium Pod";
        case GoodType::POD_LARGE:       return "Large Pod";
        // Weapons & Equipment
        case GoodType::SPRAT:           return "Sprat x10";
        case GoodType::SWARM:           return "Swarm x10";
        case GoodType::DEVASTATOR:      return "Devastator";
        case GoodType::HOLOGRAM:        return "Hologram x5";
        case GoodType::CHAFF:           return "Chaff x10";
        case GoodType::SALVAGE_DRONE:   return "Salvage Drone";
        // Consumables
        case GoodType::NARCOTICS:       return "Narcotics";
        case GoodType::ALCOHOL:         return "Alcohol";
        case GoodType::MEDICINE:        return "Medicine";
        case GoodType::TEXTILES:        return "Textiles";
        // Moths
        case GoodType::MOTH_SILVER_Y:   return "Silver-Y Moth";
        case GoodType::MOTH_SWALLOW:    return "Swallow";
        case GoodType::MOTH_HAWK:       return "Hawk Moth";
        case GoodType::MOTH_NEO_TIGER:  return "Neo Tiger Moth";
        case GoodType::MOTH_MOON:       return "Moon Moth";
        case GoodType::MOTH_POLICE:     return "Police Moth";
        case GoodType::MOTH_DEATHS_HEAD:return "Death's Head";
        default:                        return "Unknown";
    }
}

const char* EconomySystem::getSignalTypeName(EconomySignalType type) {
    switch (type) {
        case EconomySignalType::PRICE_SPIKE:        return "Price Spike";
        case EconomySignalType::PRICE_DROP:         return "Price Drop";
        case EconomySignalType::SHORTAGE:           return "Shortage";
        case EconomySignalType::SURPLUS:            return "Surplus";
        case EconomySignalType::NEW_DEMAND:         return "New Demand";
        case EconomySignalType::PRODUCTION_ONLINE:  return "Production Online";
        case EconomySignalType::PRODUCTION_OFFLINE: return "Production Offline";
        default:                                    return "Unknown";
    }
}

// ============================================================================
// Building Definitions - Base Producers
// ============================================================================

static std::vector<BuildingDef> s_baseProducers = {
    // === OIL RIGS ===
    {
        "Blackgold Offshore",
        BuildingRole::BASE_PRODUCER,
        {},  // No inputs - base producer
        {{GoodType::OIL, 15.0f}},
        250.0f
    },
    {
        "Deepwater Horizon II",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::OIL, 12.0f}},
        250.0f
    },

    // === GAS WELLS ===
    {
        "Methane Heights",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::NATURAL_GAS, 18.0f}},
        200.0f
    },
    {
        "Frostbite Extraction",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::NATURAL_GAS, 14.0f}},
        200.0f
    },

    // === QUARRIES ===
    {
        "Whiterock Quarry",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::LIMESTONE, 20.0f}},
        150.0f
    },
    {
        "Old Stone Canyon",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::LIMESTONE, 16.0f}},
        150.0f
    },

    // === MINES (Coal, Phosphates, Sulfur) ===
    {
        "Shadowdeep Colliery",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::COAL, 14.0f}, {GoodType::SULFUR, 4.0f}},
        200.0f
    },
    {
        "Ashvein Mines",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::COAL, 12.0f}, {GoodType::PHOSPHATES, 6.0f}},
        200.0f
    },
    {
        "Brimstone Hollow",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::SULFUR, 10.0f}, {GoodType::PHOSPHATES, 8.0f}},
        180.0f
    },
    {
        "Devil's Basin Mine",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::COAL, 10.0f}, {GoodType::SULFUR, 5.0f}, {GoodType::PHOSPHATES, 5.0f}},
        220.0f
    },

    // Water/Chemicals
    {
        "Sewage Control",
        BuildingRole::BASE_PRODUCER,
        {},
        {
            {GoodType::PURE_WATER, 20.0f},
            {GoodType::CHEMICALS, 5.0f}
        },
        300.0f
    },
    // Luxury goods
    {
        "Bill Moritz",
        BuildingRole::BASE_PRODUCER,
        {},
        {
            {GoodType::CIGARS, 5.0f}
        },
        100.0f
    },
    // === GRAIN FARMS ===
    {
        "Greenfield Farm",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::GRAIN, 20.0f}},
        120.0f
    },
    {
        "Harvest Valley",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::GRAIN, 18.0f}},
        120.0f
    },
    {
        "Sunrise Acres",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::GRAIN, 16.0f}},
        120.0f
    },
    {
        "Golden Plains Co-op",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::GRAIN, 22.0f}},
        140.0f
    },

    // === COTTON FARMS ===
    {
        "Whitepetal Cotton Co",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::COTTON, 15.0f}},
        100.0f
    },
    {
        "Southern Fibers",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::COTTON, 12.0f}},
        100.0f
    },

    // === FISHERIES ===
    {
        "Deepwater Fisheries",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::FISH, 14.0f}},
        130.0f
    },
    {
        "Coastal Catch",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::FISH, 12.0f}},
        130.0f
    },
    {
        "Harbor Trawlers",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::FISH, 10.0f}},
        110.0f
    },

    // === LOGGING CAMPS (Timber) ===
    {
        "Northwood Logging",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::TIMBER, 18.0f}},
        160.0f
    },
    {
        "Redpine Lumber Camp",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::TIMBER, 15.0f}},
        160.0f
    },
    {
        "Clearcut Operations",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::TIMBER, 12.0f}},
        140.0f
    },

    // === SAND QUARRIES ===
    {
        "Desert Sands Quarry",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::SAND, 25.0f}},
        200.0f
    },
    {
        "Riverside Sand Co",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::SAND, 20.0f}},
        180.0f
    },

    // === GRAVEL PITS ===
    {
        "Stonebreak Gravel",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::GRAVEL, 22.0f}},
        180.0f
    },
    {
        "Roadbed Aggregates",
        BuildingRole::BASE_PRODUCER,
        {},
        {{GoodType::GRAVEL, 18.0f}},
        180.0f
    },

    // Mining
    {
        "Prison Mine",
        BuildingRole::BASE_PRODUCER,
        {},
        {
            {GoodType::ORE, 15.0f},
            {GoodType::GEMS, 3.0f},
            {GoodType::COAL, 5.0f}
        },
        250.0f
    }
};

static std::vector<BuildingDef> s_manufacturers = {
    // === CHEMICAL PLANTS (now manufacturers requiring raw resources) ===
    {
        "Downtown Chemicals",
        BuildingRole::MANUFACTURER,
        {{GoodType::OIL, 12.0f}, {GoodType::NATURAL_GAS, 8.0f}, {GoodType::SULFUR, 4.0f}},
        {
            {GoodType::CHEMICALS, 10.0f},
            {GoodType::PLASTICS, 8.0f},
            {GoodType::EXPLOSIVES, 3.0f},
            {GoodType::FLARES, 5.0f}
        },
        200.0f
    },
    {
        "Riverside Chemicals",
        BuildingRole::MANUFACTURER,
        {{GoodType::OIL, 10.0f}, {GoodType::NATURAL_GAS, 10.0f}, {GoodType::LIMESTONE, 5.0f}},
        {
            {GoodType::CHEMICALS, 10.0f},
            {GoodType::PLASTICS, 8.0f},
            {GoodType::EXPLOSIVES, 3.0f},
            {GoodType::FLARES, 5.0f}
        },
        200.0f
    },
    {
        "Agrochem",
        BuildingRole::MANUFACTURER,
        {{GoodType::NATURAL_GAS, 12.0f}, {GoodType::PHOSPHATES, 8.0f}, {GoodType::SULFUR, 6.0f}},
        {
            {GoodType::CHEMICALS, 12.0f},
            {GoodType::PLASTICS, 6.0f},
            {GoodType::EXPLOSIVES, 4.0f},
            {GoodType::FLARES, 4.0f}
        },
        200.0f
    },

    // === RANCHES (Grain -> Meat + Furs) ===
    {
        "Dusty Trail Ranch",
        BuildingRole::MANUFACTURER,
        {{GoodType::GRAIN, 15.0f}, {GoodType::PURE_WATER, 5.0f}},
        {{GoodType::MEAT, 10.0f}, {GoodType::FURS, 4.0f}},
        150.0f
    },
    {
        "Rolling Hills Livestock",
        BuildingRole::MANUFACTURER,
        {{GoodType::GRAIN, 12.0f}, {GoodType::PURE_WATER, 4.0f}},
        {{GoodType::MEAT, 8.0f}, {GoodType::FURS, 3.0f}},
        150.0f
    },
    {
        "Prairie Star Ranch",
        BuildingRole::MANUFACTURER,
        {{GoodType::GRAIN, 10.0f}, {GoodType::PURE_WATER, 3.0f}},
        {{GoodType::MEAT, 6.0f}, {GoodType::FURS, 5.0f}},
        140.0f
    },

    // === TEXTILE MILLS (Cotton -> Textiles) ===
    {
        "Threadwell Mills",
        BuildingRole::MANUFACTURER,
        {{GoodType::COTTON, 12.0f}},
        {{GoodType::TEXTILES, 10.0f}},
        120.0f
    },
    {
        "Riverside Weavers",
        BuildingRole::MANUFACTURER,
        {{GoodType::COTTON, 10.0f}},
        {{GoodType::TEXTILES, 8.0f}},
        120.0f
    },

    // === FOOD PROCESSING (Grain + Meat + Fish -> Food) ===
    {
        "Central Food Processing",
        BuildingRole::MANUFACTURER,
        {{GoodType::GRAIN, 10.0f}, {GoodType::MEAT, 5.0f}, {GoodType::FISH, 5.0f}},
        {{GoodType::FOOD, 20.0f}},
        180.0f
    },
    {
        "Provisions Inc",
        BuildingRole::MANUFACTURER,
        {{GoodType::GRAIN, 8.0f}, {GoodType::MEAT, 6.0f}},
        {{GoodType::FOOD, 15.0f}},
        160.0f
    },
    {
        "Harbor Cannery",
        BuildingRole::MANUFACTURER,
        {{GoodType::FISH, 12.0f}, {GoodType::GRAIN, 4.0f}},
        {{GoodType::FOOD, 14.0f}},
        150.0f
    },

    // === HYDROPONICS (High-tech food, needs Water + Chemicals) ===
    {
        "AeroGrow Hydroponics",
        BuildingRole::MANUFACTURER,
        {{GoodType::PURE_WATER, 15.0f}, {GoodType::CHEMICALS, 5.0f}},
        {{GoodType::FOOD, 12.0f}},
        200.0f
    },
    {
        "NutraFarms Vertical",
        BuildingRole::MANUFACTURER,
        {{GoodType::PURE_WATER, 12.0f}, {GoodType::CHEMICALS, 4.0f}},
        {{GoodType::FOOD, 10.0f}},
        200.0f
    },

    // === PHARMACEUTICAL (Chemicals + Water -> Medicine) ===
    {
        "MediCorp Labs",
        BuildingRole::MANUFACTURER,
        {{GoodType::CHEMICALS, 10.0f}, {GoodType::PURE_WATER, 8.0f}},
        {{GoodType::MEDICINE, 8.0f}},
        220.0f
    },
    {
        "LifeScience Pharma",
        BuildingRole::MANUFACTURER,
        {{GoodType::CHEMICALS, 8.0f}, {GoodType::PURE_WATER, 6.0f}},
        {{GoodType::MEDICINE, 6.0f}},
        220.0f
    },

    // === SAWMILLS (Timber -> Lumber) ===
    {
        "Woodcraft Sawmill",
        BuildingRole::MANUFACTURER,
        {{GoodType::TIMBER, 15.0f}},
        {{GoodType::LUMBER, 12.0f}},
        140.0f
    },
    {
        "Pioneer Timber Works",
        BuildingRole::MANUFACTURER,
        {{GoodType::TIMBER, 12.0f}},
        {{GoodType::LUMBER, 10.0f}},
        140.0f
    },
    {
        "Millbrook Processing",
        BuildingRole::MANUFACTURER,
        {{GoodType::TIMBER, 10.0f}},
        {{GoodType::LUMBER, 8.0f}},
        120.0f
    },

    // === STEEL MILLS (Ore + Coal + Limestone -> Steel) ===
    {
        "Ironforge Steelworks",
        BuildingRole::MANUFACTURER,
        {{GoodType::ORE, 15.0f}, {GoodType::COAL, 10.0f}, {GoodType::LIMESTONE, 5.0f}},
        {{GoodType::STEEL, 12.0f}},
        250.0f
    },
    {
        "Titan Steel Co",
        BuildingRole::MANUFACTURER,
        {{GoodType::ORE, 12.0f}, {GoodType::COAL, 8.0f}, {GoodType::LIMESTONE, 4.0f}},
        {{GoodType::STEEL, 10.0f}},
        250.0f
    },
    {
        "Blast Furnace Industries",
        BuildingRole::MANUFACTURER,
        {{GoodType::ORE, 18.0f}, {GoodType::COAL, 12.0f}, {GoodType::LIMESTONE, 6.0f}},
        {{GoodType::STEEL, 15.0f}},
        280.0f
    },

    // === CEMENT PLANTS (Limestone + Sand + Gravel + Water -> Concrete) ===
    {
        "Graystone Cement",
        BuildingRole::MANUFACTURER,
        {{GoodType::LIMESTONE, 10.0f}, {GoodType::SAND, 15.0f}, {GoodType::GRAVEL, 12.0f}, {GoodType::PURE_WATER, 8.0f}},
        {{GoodType::CONCRETE, 14.0f}},
        200.0f
    },
    {
        "Quickset Concrete Works",
        BuildingRole::MANUFACTURER,
        {{GoodType::LIMESTONE, 8.0f}, {GoodType::SAND, 12.0f}, {GoodType::GRAVEL, 10.0f}, {GoodType::PURE_WATER, 6.0f}},
        {{GoodType::CONCRETE, 12.0f}},
        200.0f
    },
    {
        "Foundation Industries",
        BuildingRole::MANUFACTURER,
        {{GoodType::LIMESTONE, 12.0f}, {GoodType::SAND, 18.0f}, {GoodType::GRAVEL, 15.0f}, {GoodType::PURE_WATER, 10.0f}},
        {{GoodType::CONCRETE, 18.0f}},
        240.0f
    },

    // === CONSTRUCTION YARDS (Steel + Concrete + Lumber -> ConstMat) ===
    {
        "BuildRight Construction",
        BuildingRole::MANUFACTURER,
        {{GoodType::STEEL, 8.0f}, {GoodType::CONCRETE, 10.0f}, {GoodType::LUMBER, 6.0f}},
        {{GoodType::CONSTMAT, 12.0f}},
        180.0f
    },
    {
        "Metro Construction Supply",
        BuildingRole::MANUFACTURER,
        {{GoodType::STEEL, 6.0f}, {GoodType::CONCRETE, 8.0f}, {GoodType::LUMBER, 5.0f}},
        {{GoodType::CONSTMAT, 10.0f}},
        180.0f
    },
    {
        "Skyline Builders Depot",
        BuildingRole::MANUFACTURER,
        {{GoodType::STEEL, 10.0f}, {GoodType::CONCRETE, 12.0f}, {GoodType::LUMBER, 8.0f}},
        {{GoodType::CONSTMAT, 15.0f}},
        220.0f
    },

    // === ORE PROCESSING ===
    {
        "Ore Processing",
        BuildingRole::MANUFACTURER,
        {{GoodType::ORE, 35.0f}},
        {{GoodType::SHEET_METAL, 20.0f}, {GoodType::EX_METAL, 10.0f}},
        200.0f
    },
    {
        "Ore Proc 2",
        BuildingRole::MANUFACTURER,
        {{GoodType::ORE, 35.0f}},
        {{GoodType::SHEET_METAL, 20.0f}, {GoodType::EX_METAL, 10.0f}},
        200.0f
    },
    {
        "The Ore House",
        BuildingRole::MANUFACTURER,
        {{GoodType::ORE, 35.0f}},
        {{GoodType::SHEET_METAL, 20.0f}, {GoodType::EX_METAL, 10.0f}},
        200.0f
    },

    // === RECYCLING ===
    {
        "Downtown Recycling",
        BuildingRole::MANUFACTURER,
        {{GoodType::SCRAP_METAL, 25.0f}},
        {{GoodType::SHEET_METAL, 15.0f}},
        150.0f
    },
    {
        "JunkYard",
        BuildingRole::MANUFACTURER,
        {{GoodType::SCRAP_METAL, 25.0f}},
        {{GoodType::SHEET_METAL, 15.0f}},
        150.0f
    },
    {
        "Recycle Joint",
        BuildingRole::MANUFACTURER,
        {{GoodType::SCRAP_METAL, 25.0f}},
        {{GoodType::SHEET_METAL, 15.0f}},
        150.0f
    },

    // === COMPONENTS MANUFACTURERS ===
    {
        "Downtown Components",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::PURE_WATER, 20.0f}, {GoodType::CHEMICALS, 20.0f},
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::PLASTICS, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::COMP_COMP, 10.0f}, {GoodType::MACH_PARTS, 10.0f},
            {GoodType::CELL_1, 5.0f}, {GoodType::CELL_2, 5.0f},
            {GoodType::CELL_3, 5.0f}, {GoodType::CELL_4, 5.0f},
            {GoodType::SALVAGE_DRONE, 2.0f}, {GoodType::CHAFF, 8.0f}
        },
        300.0f
    },
    {
        "Riverside Parts",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::PURE_WATER, 20.0f}, {GoodType::CHEMICALS, 20.0f},
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::PLASTICS, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::COMP_COMP, 10.0f}, {GoodType::MACH_PARTS, 10.0f},
            {GoodType::CELL_1, 5.0f}, {GoodType::CELL_2, 5.0f},
            {GoodType::CELL_3, 5.0f}, {GoodType::CELL_4, 5.0f},
            {GoodType::SALVAGE_DRONE, 2.0f}, {GoodType::CHAFF, 8.0f}
        },
        300.0f
    },
    {
        "Cravan Components",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::PURE_WATER, 20.0f}, {GoodType::CHEMICALS, 20.0f},
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::PLASTICS, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::COMP_COMP, 10.0f}, {GoodType::MACH_PARTS, 10.0f},
            {GoodType::CELL_1, 5.0f}, {GoodType::CELL_2, 5.0f},
            {GoodType::CELL_3, 5.0f}, {GoodType::CELL_4, 5.0f},
            {GoodType::SALVAGE_DRONE, 2.0f}, {GoodType::CHAFF, 8.0f}
        },
        300.0f
    },
    {
        "TechParts",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::PLASTICS, 20.0f}
        },
        {{GoodType::COMP_COMP, 10.0f}},
        150.0f
    },

    // === WEAPONS MANUFACTURERS ===
    {
        "Downtown Munitions",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::COMP_COMP, 20.0f},
            {GoodType::MACH_PARTS, 20.0f}, {GoodType::EXPLOSIVES, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::SPRAT, 5.0f}, {GoodType::SWARM, 5.0f},
            {GoodType::DEVASTATOR, 2.0f}, {GoodType::HOLOGRAM, 3.0f}
        },
        250.0f
    },
    {
        "Dr Jobes Weapons",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::COMP_COMP, 20.0f},
            {GoodType::MACH_PARTS, 20.0f}, {GoodType::EXPLOSIVES, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::SPRAT, 5.0f}, {GoodType::SWARM, 5.0f},
            {GoodType::DEVASTATOR, 2.0f}, {GoodType::HOLOGRAM, 3.0f}
        },
        250.0f
    },
    {
        "Psyco Bob's 1",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::COMP_COMP, 20.0f},
            {GoodType::MACH_PARTS, 20.0f}, {GoodType::EXPLOSIVES, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::SPRAT, 5.0f}, {GoodType::SWARM, 5.0f},
            {GoodType::DEVASTATOR, 2.0f}, {GoodType::HOLOGRAM, 3.0f}
        },
        250.0f
    },
    {
        "Psyco Bob's 2",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::GEMS, 20.0f}, {GoodType::EX_METAL, 20.0f},
            {GoodType::SHEET_METAL, 20.0f}, {GoodType::COMP_COMP, 20.0f},
            {GoodType::MACH_PARTS, 20.0f}, {GoodType::EXPLOSIVES, 20.0f},
            {GoodType::FUSION_PARTS, 20.0f}
        },
        {
            {GoodType::SPRAT, 5.0f}, {GoodType::SWARM, 5.0f},
            {GoodType::DEVASTATOR, 2.0f}, {GoodType::HOLOGRAM, 3.0f}
        },
        250.0f
    },

    // === MOTH FACTORIES ===
    {
        "Bargain Moths",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::CELL_1, 3.0f}, {GoodType::ENGINE_1, 2.0f},
            {GoodType::COMP_COMP, 25.0f}, {GoodType::MACH_PARTS, 25.0f},
            {GoodType::ORE, 10.0f}, {GoodType::SHEET_METAL, 25.0f},
            {GoodType::PLASTICS, 25.0f}, {GoodType::LASER, 2.0f}
        },
        {{GoodType::MOTH_SILVER_Y, 1.0f}, {GoodType::MOTH_SWALLOW, 1.0f}},
        400.0f
    },
    {
        "Downtown Moths",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::ORE, 10.0f}, {GoodType::SHEET_METAL, 25.0f},
            {GoodType::COMP_COMP, 25.0f}, {GoodType::MACH_PARTS, 25.0f},
            {GoodType::PLASTICS, 25.0f}, {GoodType::CELL_1, 3.0f},
            {GoodType::CELL_2, 3.0f}, {GoodType::CELL_3, 3.0f},
            {GoodType::LASER, 2.0f}
        },
        {
            {GoodType::MOTH_HAWK, 0.5f}, {GoodType::MOTH_NEO_TIGER, 0.5f},
            {GoodType::MOTH_MOON, 0.5f}, {GoodType::MOTH_POLICE, 0.5f},
            {GoodType::MOTH_DEATHS_HEAD, 0.5f}, {GoodType::MOTH_SILVER_Y, 0.5f},
            {GoodType::MOTH_SWALLOW, 0.5f}
        },
        500.0f
    },
    {
        "Highrise Motors",
        BuildingRole::MANUFACTURER,
        {
            {GoodType::ORE, 10.0f}, {GoodType::SHEET_METAL, 25.0f},
            {GoodType::COMP_COMP, 25.0f}, {GoodType::MACH_PARTS, 25.0f},
            {GoodType::PLASTICS, 25.0f}, {GoodType::CELL_1, 3.0f},
            {GoodType::CELL_2, 3.0f}, {GoodType::CELL_3, 3.0f},
            {GoodType::LASER, 2.0f}
        },
        {
            {GoodType::MOTH_HAWK, 0.5f}, {GoodType::MOTH_NEO_TIGER, 0.5f},
            {GoodType::MOTH_MOON, 0.5f}, {GoodType::MOTH_POLICE, 0.5f},
            {GoodType::MOTH_DEATHS_HEAD, 0.5f}, {GoodType::MOTH_SILVER_Y, 0.5f},
            {GoodType::MOTH_SWALLOW, 0.5f}
        },
        500.0f
    },

    // === CONSUMABLES ===
    {
        "General Industrial",
        BuildingRole::MANUFACTURER,
        {{GoodType::CHEMICALS, 25.0f}},
        {{GoodType::NARCOTICS, 10.0f}},
        100.0f
    },
    {
        "Waterfront Booze",
        BuildingRole::MANUFACTURER,
        {{GoodType::PURE_WATER, 35.0f}, {GoodType::CHEMICALS, 35.0f}},
        {{GoodType::ALCOHOL, 20.0f}},
        150.0f
    }
};

// === CONSUMERS (bars, entertainment - create demand, no output) ===
static std::vector<BuildingDef> s_consumers = {
    {
        "The After Dark",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},  // No outputs
        100.0f
    },
    {
        "Jupiter 4",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},
        100.0f
    },
    {
        "Shanty Inn",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},
        100.0f
    },
    {
        "The Slum",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},
        100.0f
    },
    {
        "The Waterfront",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},
        100.0f
    },
    {
        "Flyers Retreat",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},
        100.0f
    },
    {
        "Traders Rest",
        BuildingRole::CONSUMER,
        {{GoodType::ALCOHOL, 15.0f}, {GoodType::NARCOTICS, 10.0f}, {GoodType::CIGARS, 8.0f}},
        {},
        100.0f
    }
};

const std::vector<BuildingDef>& getBaseProducers() {
    return s_baseProducers;
}

const std::vector<BuildingDef>& getManufacturers() {
    return s_manufacturers;
}

const std::vector<BuildingDef>& getConsumers() {
    return s_consumers;
}

const BuildingDef* findBuildingDef(const std::string& name) {
    // Search base producers
    for (const auto& def : s_baseProducers) {
        if (def.name == name) return &def;
    }
    // Search manufacturers
    for (const auto& def : s_manufacturers) {
        if (def.name == name) return &def;
    }
    // Search consumers
    for (const auto& def : s_consumers) {
        if (def.name == name) return &def;
    }
    return nullptr;
}

} // namespace eden
