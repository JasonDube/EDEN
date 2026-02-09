#include "TraderAI.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace eden {

TraderAI::TraderAI(uint32_t id, const std::string& name)
    : m_id(id)
    , m_name(name.empty() ? "Trader_" + std::to_string(id) : name)
{
}

void TraderAI::setEconomySystem(EconomySystem* economy) {
    m_economy = economy;

    // Subscribe to economy signals
    if (m_economy) {
        m_economy->subscribe([this](const EconomySignal& sig) {
            onEconomySignal(sig);
        });
    }
}

void TraderAI::setPathfinder(AStarPathfinder* pathfinder) {
    m_pathfinder = pathfinder;
}

void TraderAI::update(float deltaTime, float gameTimeMinutes) {
    m_gameTime = gameTimeMinutes;
    m_stateTimer += deltaTime;

    // Update based on current state
    switch (m_state) {
        case TraderState::IDLE:
            updateIdle(deltaTime);
            break;
        case TraderState::TRAVELING:
            updateTraveling(deltaTime);
            break;
        case TraderState::BUYING:
            updateBuying(deltaTime);
            break;
        case TraderState::SELLING:
            updateSelling(deltaTime);
            break;
        case TraderState::REFUELING:
            updateRefueling(deltaTime);
            break;
        case TraderState::WAITING:
        case TraderState::FLEEING:
            // TODO: implement these states
            break;
    }

    // AI decision making
    if (m_aiEnabled) {
        updateAI(deltaTime, gameTimeMinutes);
    }
}

void TraderAI::updateAI(float deltaTime, float gameTimeMinutes) {
    m_decisionCooldown -= deltaTime;

    if (m_decisionCooldown <= 0.0f && m_state == TraderState::IDLE) {
        decideNextAction();
        m_decisionCooldown = DECISION_INTERVAL;
    }
}

void TraderAI::decideNextAction() {
    std::cout << m_name << " deciding at node " << m_currentNodeId
              << " (credits: $" << m_credits << ", cargo: " << getCargoUsed() << ")" << std::endl;

    // Priority 1: Need fuel?
    if (needsFuel()) {
        uint32_t refuelNode = 0;
        if (m_pathfinder) {
            refuelNode = m_pathfinder->findNearestNodeOfCategory(
                m_position, GraphCategory::REFUEL, m_movementLayer);
        }
        if (refuelNode != 0) {
            addMessage(EconomySignalType::SHORTAGE, "Low fuel! Heading to refuel station.");
            startTravelTo(refuelNode);
            return;
        }
    }

    // Priority 2: Have cargo to sell?
    if (!m_cargo.empty() && m_hasTradePlan && m_currentTrade.valid) {
        // Go sell our cargo
        if (m_currentNodeId != m_currentTrade.sellNodeId) {
            startTravelTo(m_currentTrade.sellNodeId);
            return;
        }
    }

    // Priority 3: Find new trade opportunity
    evaluateOpportunities();

    if (m_hasTradePlan && m_currentTrade.valid) {
        // Go to buy location
        if (m_currentNodeId != m_currentTrade.buyNodeId) {
            addMessage(EconomySignalType::NEW_DEMAND,
                "Found opportunity: " + std::string(EconomySystem::getGoodName(m_currentTrade.good)) +
                " profit margin " + std::to_string(static_cast<int>(m_currentTrade.profitMargin * 100)) + "%");
            startTravelTo(m_currentTrade.buyNodeId);
            return;
        } else {
            // Already at buy location, buy goods
            std::cout << m_name << " at buy location, attempting to buy "
                      << EconomySystem::getGoodName(m_currentTrade.good) << std::endl;
            float affordableQty = m_credits / m_currentTrade.buyPrice;
            float cargoQty = getCargoFree();
            float buyQty = std::min(affordableQty, cargoQty);

            std::cout << "  affordableQty=" << affordableQty << ", cargoQty=" << cargoQty
                      << ", buyQty=" << buyQty << std::endl;

            if (buyQty > 0.1f) {
                bool bought = buyGoods(m_currentTrade.good, buyQty);
                std::cout << "  buyGoods result: " << (bought ? "SUCCESS" : "FAILED") << std::endl;
                if (!bought) {
                    // Failed to buy - clear trade plan so we find a different opportunity
                    std::cout << "  Clearing trade plan, will find new opportunity" << std::endl;
                    m_hasTradePlan = false;
                    m_currentTrade = TradeOpportunity();
                }
            } else {
                std::cout << "  buyQty too small!" << std::endl;
                m_hasTradePlan = false;
                m_currentTrade = TradeOpportunity();
            }
        }
    } else {
        std::cout << m_name << " has no trade plan after evaluation" << std::endl;
    }
}

void TraderAI::evaluateOpportunities() {
    auto trades = findBestTrades(10);
    std::cout << m_name << " found " << trades.size() << " potential trades" << std::endl;

    m_hasTradePlan = false;
    m_currentTrade = TradeOpportunity();

    for (const auto& trade : trades) {
        // Check if we can afford it
        float minBuy = trade.buyPrice * 1.0f; // At least 1 unit
        if (m_credits < minBuy) {
            std::cout << "  - rejected: can't afford (need $" << minBuy << ")" << std::endl;
            continue;
        }

        // Check profit margin meets our minimum
        if (trade.profitMargin < m_minProfitMargin) {
            std::cout << "  - rejected: margin " << (trade.profitMargin*100) << "% < min " << (m_minProfitMargin*100) << "%" << std::endl;
            continue;
        }

        // Check if we have fuel for the journey
        float fuelNeeded = trade.distance * m_fuelPerMeter;
        if (m_fuel < fuelNeeded * 1.2f) {
            std::cout << "  - rejected: not enough fuel (need " << fuelNeeded << ")" << std::endl;
            continue;
        }

        // This trade looks good
        std::cout << "  + ACCEPTED: " << EconomySystem::getGoodName(trade.good)
                  << " buy@" << trade.buyNodeId << " sell@" << trade.sellNodeId << std::endl;
        m_currentTrade = trade;
        m_hasTradePlan = true;
        break;
    }

    if (!m_hasTradePlan && !trades.empty()) {
        std::cout << m_name << " rejected all " << trades.size() << " trades" << std::endl;
    }
}

std::vector<TradeOpportunity> TraderAI::findBestTrades(int maxResults) {
    std::vector<TradeOpportunity> opportunities;

    if (!m_economy || !m_pathfinder) {
        return opportunities;
    }

    // For each good type, find buy/sell opportunities
    for (int g = 0; g < static_cast<int>(GoodType::COUNT); g++) {
        GoodType good = static_cast<GoodType>(g);

        // Find places selling this good (where we can buy)
        auto sellers = m_economy->findBestBuyPrice(good, 5);

        // Find places buying this good (where we can sell)
        auto buyers = m_economy->findBestSellPrice(good, 5);

        // Evaluate each combination
        for (uint32_t sellerId : sellers) {
            for (uint32_t buyerId : buyers) {
                if (sellerId == buyerId) continue;

                TradeOpportunity opp = evaluateTrade(good, sellerId, buyerId);
                if (opp.valid) {
                    if (opp.netProfit > 0) {
                        opportunities.push_back(opp);
                    }
                    // else: valid but no profit (travel cost > profit)
                }
            }
        }
    }

    // Sort by profit margin (best first)
    std::sort(opportunities.begin(), opportunities.end(),
        [](const TradeOpportunity& a, const TradeOpportunity& b) {
            return a.profitMargin > b.profitMargin;
        });

    // Limit results
    if (opportunities.size() > static_cast<size_t>(maxResults)) {
        opportunities.resize(maxResults);
    }

    return opportunities;
}

TradeOpportunity TraderAI::evaluateTrade(GoodType good, uint32_t buyNode, uint32_t sellNode) {
    TradeOpportunity opp;
    opp.good = good;
    opp.buyNodeId = buyNode;
    opp.sellNodeId = sellNode;
    opp.valid = false;

    if (!m_economy || !m_pathfinder) {
        return opp;
    }

    // Check if there's actually stock to buy
    if (!m_economy->canBuy(buyNode, good, 1.0f)) {
        return opp; // No stock available
    }

    // Get prices
    opp.buyPrice = m_economy->getBuyPrice(buyNode, good);
    opp.sellPrice = m_economy->getSellPrice(sellNode, good);

    if (opp.buyPrice <= 0 || opp.sellPrice <= 0) {
        return opp;
    }

    opp.profitPerUnit = opp.sellPrice - opp.buyPrice;

    // Calculate travel distance using A* pathfinding
    float distToSell = 0.0f;

    // First: distance from current position to buy node
    float distToBuy = 0.0f;
    if (m_currentNodeId != 0 && m_currentNodeId != buyNode) {
        PathResult pathToBuy = m_pathfinder->findPath(m_currentNodeId, buyNode, m_movementLayer);
        if (!pathToBuy.found) {
            return opp; // Can't reach buy location
        }
        distToBuy = pathToBuy.totalDistance;
    }

    // Second: distance from buy node to sell node
    PathResult pathToSell = m_pathfinder->findPath(buyNode, sellNode, m_movementLayer);
    if (!pathToSell.found) {
        return opp; // Can't reach sell location from buy location
    }
    distToSell = pathToSell.totalDistance;

    // Total distance includes getting to buy location
    opp.distance = distToBuy + distToSell;

    // Estimate travel cost (fuel)
    float fuelCost = opp.distance * m_fuelPerMeter * m_economy->getPrice(GoodType::FUEL);
    opp.travelCost = fuelCost;

    // Calculate potential profit
    // How much can we carry?
    float maxQuantity = getCargoFree();
    float affordableQuantity = m_credits / opp.buyPrice;
    float quantity = std::min(maxQuantity, affordableQuantity);

    if (quantity < 0.1f) {
        return opp; // Can't afford meaningful amount
    }

    opp.estimatedProfit = opp.profitPerUnit * quantity;
    opp.netProfit = opp.estimatedProfit - opp.travelCost;

    // Profit margin = net profit / investment
    float investment = opp.buyPrice * quantity + opp.travelCost;
    opp.profitMargin = investment > 0 ? opp.netProfit / investment : 0.0f;

    opp.valid = true;

    return opp;
}

void TraderAI::updateIdle(float deltaTime) {
    // Just waiting, AI will make decisions
}

void TraderAI::updateTraveling(float deltaTime) {
    if (m_currentPath.empty() || m_pathIndex >= static_cast<int>(m_currentPath.size())) {
        arriveAtDestination();
        return;
    }

    // Move toward next waypoint
    glm::vec3 target = m_currentPath[m_pathIndex];
    glm::vec3 toTarget = target - m_position;
    float distance = glm::length(toTarget);

    if (distance < 1.0f) {
        // Reached waypoint, move to next
        m_pathIndex++;
        if (m_pathIndex >= static_cast<int>(m_currentPath.size())) {
            arriveAtDestination();
        }
        return;
    }

    // Move toward target
    glm::vec3 direction = toTarget / distance;
    float moveDistance = m_speed * deltaTime;

    // Don't overshoot
    moveDistance = std::min(moveDistance, distance);

    m_position += direction * moveDistance;

    // Consume fuel
    m_fuel -= moveDistance * m_fuelPerMeter;
    m_fuel = std::max(0.0f, m_fuel);

    // Out of fuel!
    if (m_fuel <= 0.0f) {
        m_state = TraderState::IDLE;
        addMessage(EconomySignalType::SHORTAGE, "Out of fuel! Stranded!");
        emitEvent("OUT_OF_FUEL");
    }
}

void TraderAI::updateBuying(float deltaTime) {
    // Transaction takes a moment
    if (m_stateTimer > 2.0f) {
        m_state = TraderState::IDLE;
        m_stateTimer = 0.0f;
    }
}

void TraderAI::updateSelling(float deltaTime) {
    // Transaction takes a moment
    if (m_stateTimer > 2.0f) {
        m_state = TraderState::IDLE;
        m_stateTimer = 0.0f;
        m_hasTradePlan = false; // Trade complete
    }
}

void TraderAI::updateRefueling(float deltaTime) {
    // Refuel over time
    float refuelRate = 20.0f; // Units per second
    float refuelAmount = refuelRate * deltaTime;
    float fuelCost = refuelAmount * m_economy->getPrice(GoodType::FUEL) * 0.1f;

    if (m_credits >= fuelCost && m_fuel < m_maxFuel) {
        m_fuel = std::min(m_fuel + refuelAmount, m_maxFuel);
        m_credits -= fuelCost;
    }

    // Done refueling
    if (m_fuel >= m_maxFuel * 0.95f || m_stateTimer > 10.0f) {
        m_state = TraderState::IDLE;
        m_stateTimer = 0.0f;
        addMessage(EconomySignalType::PRODUCTION_ONLINE, "Refueled to " +
            std::to_string(static_cast<int>(m_fuel / m_maxFuel * 100)) + "%");
    }
}

bool TraderAI::startTravelTo(uint32_t nodeId) {
    if (!m_pathfinder || m_currentNodeId == 0) {
        std::cout << m_name << " cannot travel - pathfinder=" << (m_pathfinder ? "yes" : "no")
                  << ", currentNodeId=" << m_currentNodeId << std::endl;
        return false;
    }

    PathResult path = m_pathfinder->findPath(m_currentNodeId, nodeId, m_movementLayer);

    if (!path.found) {
        std::cout << m_name << " pathfinding failed from " << m_currentNodeId << " to " << nodeId << std::endl;
        addMessage(EconomySignalType::PRODUCTION_OFFLINE, "Cannot find path to destination!");
        return false;
    }

    std::cout << m_name << " traveling from " << m_currentNodeId << " to " << nodeId
              << " (" << path.positions.size() << " waypoints)" << std::endl;
    m_currentPath = path.positions;
    m_pathIndex = 0;
    m_targetNodeId = nodeId;
    m_state = TraderState::TRAVELING;
    m_stateTimer = 0.0f;

    return true;
}

void TraderAI::arriveAtDestination() {
    std::cout << m_name << " arrived at node " << m_targetNodeId << std::endl;

    m_currentNodeId = m_targetNodeId;
    m_targetNodeId = 0;
    m_currentPath.clear();
    m_pathIndex = 0;

    // CRITICAL: Set state to IDLE first to prevent re-entry if trade fails
    m_state = TraderState::IDLE;
    m_stateTimer = 0.0f;

    const AINode* node = getNodeById(m_currentNodeId);
    if (node) {
        m_position = node->getPosition();
    }

    // Check what kind of location we arrived at
    if (node && node->getType() == AINodeType::GRAPH) {
        GraphCategory cat = node->getCategory();

        if (cat == GraphCategory::REFUEL) {
            m_state = TraderState::REFUELING;
            m_stateTimer = 0.0f;
            addMessage(EconomySignalType::PRODUCTION_ONLINE, "Arrived at refuel station");
            return;
        }

        // If we have a trade plan
        if (m_hasTradePlan && m_currentTrade.valid) {
            if (m_currentNodeId == m_currentTrade.buyNodeId) {
                // Buy goods
                float affordableQty = m_credits / m_currentTrade.buyPrice;
                float cargoQty = getCargoFree();
                float buyQty = std::min(affordableQty, cargoQty);

                if (buyQty > 0.1f) {
                    buyGoods(m_currentTrade.good, buyQty);
                    return;
                }
            } else if (m_currentNodeId == m_currentTrade.sellNodeId) {
                // Sell goods
                for (auto& cargo : m_cargo) {
                    if (cargo.good == m_currentTrade.good && cargo.quantity > 0) {
                        sellGoods(cargo.good, cargo.quantity);
                        return;
                    }
                }
            }
        }
    }
    // State is already IDLE from the top of this function
}

bool TraderAI::goToNode(uint32_t nodeId) {
    return startTravelTo(nodeId);
}

bool TraderAI::buyGoods(GoodType good, float quantity) {
    std::cout << "  buyGoods: " << EconomySystem::getGoodName(good) << " x" << quantity
              << " at node " << m_currentNodeId << std::endl;

    if (!m_economy || m_currentNodeId == 0) {
        std::cout << "  buyGoods FAIL: no economy or node=0" << std::endl;
        return false;
    }

    // Check how much stock is actually available
    const EconomyNode* econNode = m_economy->getNode(m_currentNodeId);
    if (!econNode) {
        std::cout << "  buyGoods FAIL: node not found in economy" << std::endl;
        return false;
    }

    auto invIt = econNode->inventory.find(good);
    float available = (invIt != econNode->inventory.end()) ? invIt->second : 0.0f;
    std::cout << "  Available stock: " << available << std::endl;

    if (available < 0.1f) {
        std::cout << "  buyGoods FAIL: not enough stock available" << std::endl;
        addMessage(EconomySignalType::SHORTAGE, "Cannot buy - not enough stock!");
        return false;
    }

    // Buy whatever is available, up to requested quantity
    quantity = std::min(quantity, available);
    std::cout << "  Adjusted quantity to: " << quantity << std::endl;

    float price = m_economy->getBuyPrice(m_currentNodeId, good);
    float totalCost = price * quantity;

    // Check if we can afford it
    if (m_credits < totalCost) {
        quantity = m_credits / price;
        totalCost = m_credits;
    }

    // Check cargo space
    if (quantity > getCargoFree()) {
        quantity = getCargoFree();
        totalCost = price * quantity;
    }

    if (quantity < 0.01f) {
        return false;
    }

    // Execute transaction
    if (m_economy->executeTrade(m_currentNodeId, good, quantity, true)) {
        m_credits -= totalCost;

        // Add to cargo
        CargoItem item;
        item.good = good;
        item.quantity = quantity;
        item.purchasePrice = price;
        m_cargo.push_back(item);

        m_state = TraderState::BUYING;
        m_stateTimer = 0.0f;

        addMessage(EconomySignalType::NEW_DEMAND,
            "Bought " + std::to_string(static_cast<int>(quantity)) + " " +
            EconomySystem::getGoodName(good) + " for $" + std::to_string(static_cast<int>(totalCost)));

        emitEvent("BOUGHT_GOODS");
        return true;
    }

    return false;
}

bool TraderAI::sellGoods(GoodType good, float quantity) {
    if (!m_economy || m_currentNodeId == 0) {
        return false;
    }

    // Find cargo of this type
    auto it = std::find_if(m_cargo.begin(), m_cargo.end(),
        [good](const CargoItem& c) { return c.good == good; });

    if (it == m_cargo.end() || it->quantity < quantity) {
        return false;
    }

    // Check if node buys this good
    if (!m_economy->canSell(m_currentNodeId, good, quantity)) {
        addMessage(EconomySignalType::SURPLUS, "Cannot sell - location won't buy!");
        return false;
    }

    float price = m_economy->getSellPrice(m_currentNodeId, good);
    float totalRevenue = price * quantity;

    // Execute transaction
    if (m_economy->executeTrade(m_currentNodeId, good, quantity, false)) {
        float profit = (price - it->purchasePrice) * quantity;
        m_credits += totalRevenue;

        // Remove from cargo
        it->quantity -= quantity;
        if (it->quantity < 0.01f) {
            m_cargo.erase(it);
        }

        m_state = TraderState::SELLING;
        m_stateTimer = 0.0f;

        addMessage(EconomySignalType::PRICE_SPIKE,
            "Sold " + std::to_string(static_cast<int>(quantity)) + " " +
            EconomySystem::getGoodName(good) + " for $" + std::to_string(static_cast<int>(totalRevenue)) +
            " (profit: $" + std::to_string(static_cast<int>(profit)) + ")");

        emitEvent("SOLD_GOODS");
        return true;
    }

    return false;
}

void TraderAI::cancelCurrentAction() {
    m_state = TraderState::IDLE;
    m_currentPath.clear();
    m_pathIndex = 0;
    m_targetNodeId = 0;
    m_hasTradePlan = false;
    m_stateTimer = 0.0f;
}

void TraderAI::onEconomySignal(const EconomySignal& signal) {
    // Add significant signals to inbox
    if (signal.magnitude > 0.3f) {
        addMessage(signal.type, signal.message, signal.locationNodeId, signal.good);
    }
}

void TraderAI::addMessage(EconomySignalType type, const std::string& text,
                          uint32_t nodeId, GoodType good) {
    TraderMessage msg;
    msg.gameTime = m_gameTime;
    msg.type = type;
    msg.text = text;
    msg.relatedNodeId = nodeId;
    msg.relatedGood = good;
    msg.read = false;

    m_messages.push_front(msg);

    // Limit message history
    while (m_messages.size() > MAX_MESSAGES) {
        m_messages.pop_back();
    }
}

int TraderAI::getUnreadCount() const {
    int count = 0;
    for (const auto& msg : m_messages) {
        if (!msg.read) count++;
    }
    return count;
}

void TraderAI::markAllRead() {
    for (auto& msg : m_messages) {
        msg.read = true;
    }
}

void TraderAI::clearMessages() {
    m_messages.clear();
}

float TraderAI::getCargoUsed() const {
    float total = 0.0f;
    for (const auto& item : m_cargo) {
        total += item.quantity;
    }
    return total;
}

bool TraderAI::hasCargo(GoodType good, float minAmount) const {
    for (const auto& item : m_cargo) {
        if (item.good == good && item.quantity >= minAmount) {
            return true;
        }
    }
    return false;
}

const AINode* TraderAI::getNodeById(uint32_t id) const {
    if (!m_nodes) return nullptr;
    for (const auto& node : *m_nodes) {
        if (node && node->getId() == id) {
            return node.get();
        }
    }
    return nullptr;
}

void TraderAI::emitEvent(const std::string& event) {
    if (m_onEvent) {
        m_onEvent(event);
    }
}

const char* TraderAI::getStateName() const {
    switch (m_state) {
        case TraderState::IDLE:      return "Idle";
        case TraderState::TRAVELING: return "Traveling";
        case TraderState::BUYING:    return "Buying";
        case TraderState::SELLING:   return "Selling";
        case TraderState::REFUELING: return "Refueling";
        case TraderState::WAITING:   return "Waiting";
        case TraderState::FLEEING:   return "Fleeing";
        default:                     return "Unknown";
    }
}

} // namespace eden
