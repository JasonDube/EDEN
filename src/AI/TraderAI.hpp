#pragma once

#include "../Economy/EconomySystem.hpp"
#include "../Editor/AINode.hpp"
#include "AStarPathfinder.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <functional>

namespace eden {

// Trader behavior states
enum class TraderState {
    IDLE,           // At location, deciding what to do
    TRAVELING,      // Moving along path
    BUYING,         // Executing buy transaction
    SELLING,        // Executing sell transaction
    REFUELING,      // Getting fuel
    WAITING,        // Waiting (for price, cooldown, etc.)
    FLEEING,        // Running from danger (pirates)
};

// A trade opportunity evaluated by the AI
struct TradeOpportunity {
    GoodType good;
    uint32_t buyNodeId;         // Where to buy
    uint32_t sellNodeId;        // Where to sell
    float buyPrice;             // Price per unit at source
    float sellPrice;            // Price per unit at destination
    float profitPerUnit;        // sellPrice - buyPrice
    float estimatedProfit;      // Total profit for full cargo
    float travelCost;           // Fuel/time cost estimate
    float netProfit;            // estimatedProfit - travelCost
    float profitMargin;         // netProfit / investment
    float distance;             // Total travel distance
    bool valid = false;
};

// Cargo hold item
struct CargoItem {
    GoodType good;
    float quantity;
    float purchasePrice;        // What we paid per unit
};

// Message in trader's inbox
struct TraderMessage {
    float gameTime;
    EconomySignalType type;
    std::string text;
    bool read = false;
    uint32_t relatedNodeId = 0;
    GoodType relatedGood = GoodType::FOOD;
};

// Callback when trader does something notable
using TraderEventCallback = std::function<void(const std::string& event)>;

/**
 * AI Trader - Autonomous agent that buys and sells goods for profit.
 * Uses pathfinder to navigate, economy system for prices.
 */
class TraderAI {
public:
    TraderAI(uint32_t id, const std::string& name = "");

    // Connect to systems
    void setEconomySystem(EconomySystem* economy);
    void setPathfinder(AStarPathfinder* pathfinder);
    void setNodes(const std::vector<std::unique_ptr<AINode>>* nodes) { m_nodes = nodes; }

    // Update (call each frame)
    void update(float deltaTime, float gameTimeMinutes);

    // Identity
    uint32_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    // Position and movement
    const glm::vec3& getPosition() const { return m_position; }
    void setPosition(const glm::vec3& pos) { m_position = pos; }
    float getSpeed() const { return m_speed; }
    void setSpeed(float speed) { m_speed = speed; }

    // Current state
    TraderState getState() const { return m_state; }
    const char* getStateName() const;
    uint32_t getCurrentNodeId() const { return m_currentNodeId; }
    void setCurrentNodeId(uint32_t nodeId) { m_currentNodeId = nodeId; }
    uint32_t getTargetNodeId() const { return m_targetNodeId; }

    // Cargo
    float getCargoCapacity() const { return m_cargoCapacity; }
    void setCargoCapacity(float capacity) { m_cargoCapacity = capacity; }
    float getCargoUsed() const;
    float getCargoFree() const { return m_cargoCapacity - getCargoUsed(); }
    const std::vector<CargoItem>& getCargo() const { return m_cargo; }
    bool hasCargoSpace(float amount) const { return getCargoFree() >= amount; }
    bool hasCargo(GoodType good, float minAmount = 0.01f) const;

    // Money
    float getCredits() const { return m_credits; }
    void setCredits(float credits) { m_credits = credits; }
    void addCredits(float amount) { m_credits += amount; }

    // Fuel
    float getFuel() const { return m_fuel; }
    float getMaxFuel() const { return m_maxFuel; }
    void setFuel(float fuel) { m_fuel = std::clamp(fuel, 0.0f, m_maxFuel); }
    void setMaxFuel(float max) { m_maxFuel = max; }
    float getFuelEfficiency() const { return m_fuelPerMeter; }
    void setFuelEfficiency(float fuelPerMeter) { m_fuelPerMeter = fuelPerMeter; }
    bool needsFuel() const { return m_fuel < m_maxFuel * 0.2f; }

    // Messages/Inbox
    const std::deque<TraderMessage>& getMessages() const { return m_messages; }
    int getUnreadCount() const;
    void markAllRead();
    void clearMessages();

    // AI Settings
    void setAIEnabled(bool enabled) { m_aiEnabled = enabled; }
    bool isAIEnabled() const { return m_aiEnabled; }
    void setRiskTolerance(float risk) { m_riskTolerance = std::clamp(risk, 0.0f, 1.0f); }
    float getRiskTolerance() const { return m_riskTolerance; }
    void setMinProfitMargin(float margin) { m_minProfitMargin = margin; }
    float getMinProfitMargin() const { return m_minProfitMargin; }

    // Manual control (for player)
    bool goToNode(uint32_t nodeId);
    bool buyGoods(GoodType good, float quantity);
    bool sellGoods(GoodType good, float quantity);
    void cancelCurrentAction();

    // Current path (for rendering)
    const std::vector<glm::vec3>& getCurrentPath() const { return m_currentPath; }
    int getCurrentPathIndex() const { return m_pathIndex; }

    // Events
    void setOnEvent(TraderEventCallback callback) { m_onEvent = callback; }

    // Trade analysis (public for UI)
    TradeOpportunity evaluateTrade(GoodType good, uint32_t buyNode, uint32_t sellNode);
    std::vector<TradeOpportunity> findBestTrades(int maxResults = 5);

    // Layer for pathfinding (what kind of vehicle)
    void setMovementLayer(GraphLayer layer) { m_movementLayer = layer; }
    GraphLayer getMovementLayer() const { return m_movementLayer; }

private:
    // Economy signal handler
    void onEconomySignal(const EconomySignal& signal);

    // AI decision making
    void updateAI(float deltaTime, float gameTimeMinutes);
    void decideNextAction();
    void evaluateOpportunities();

    // State updates
    void updateIdle(float deltaTime);
    void updateTraveling(float deltaTime);
    void updateBuying(float deltaTime);
    void updateSelling(float deltaTime);
    void updateRefueling(float deltaTime);

    // Actions
    bool startTravelTo(uint32_t nodeId);
    bool executeBuy(GoodType good, float quantity);
    bool executeSell(GoodType good, float quantity);
    void arriveAtDestination();

    // Helpers
    const AINode* getNodeById(uint32_t id) const;
    void addMessage(EconomySignalType type, const std::string& text,
                    uint32_t nodeId = 0, GoodType good = GoodType::FOOD);
    void emitEvent(const std::string& event);

    // Identity
    uint32_t m_id;
    std::string m_name;

    // Position/Movement
    glm::vec3 m_position{0.0f};
    float m_speed = 50.0f;          // Meters per second
    GraphLayer m_movementLayer = GraphLayer::FLYING;

    // State
    TraderState m_state = TraderState::IDLE;
    uint32_t m_currentNodeId = 0;   // Node we're at (if idle)
    uint32_t m_targetNodeId = 0;    // Node we're going to

    // Path following
    std::vector<glm::vec3> m_currentPath;
    int m_pathIndex = 0;

    // Cargo
    std::vector<CargoItem> m_cargo;
    float m_cargoCapacity = 100.0f;

    // Resources
    float m_credits = 1000.0f;
    float m_fuel = 100.0f;
    float m_maxFuel = 100.0f;
    float m_fuelPerMeter = 0.01f;   // Fuel consumed per meter traveled

    // Messages
    std::deque<TraderMessage> m_messages;
    static constexpr size_t MAX_MESSAGES = 50;

    // AI settings
    bool m_aiEnabled = true;
    float m_riskTolerance = 0.5f;       // 0 = conservative, 1 = risky
    float m_minProfitMargin = 0.1f;     // Minimum 10% profit to consider trade
    float m_decisionCooldown = 0.0f;
    static constexpr float DECISION_INTERVAL = 5.0f;

    // Current trade plan
    TradeOpportunity m_currentTrade;
    bool m_hasTradePlan = false;

    // System references
    EconomySystem* m_economy = nullptr;
    AStarPathfinder* m_pathfinder = nullptr;
    const std::vector<std::unique_ptr<AINode>>* m_nodes = nullptr;

    // Callbacks
    TraderEventCallback m_onEvent;

    // Timing
    float m_stateTimer = 0.0f;      // Time in current state
    float m_gameTime = 0.0f;
};

} // namespace eden
