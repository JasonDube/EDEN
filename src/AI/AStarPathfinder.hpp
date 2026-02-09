#pragma once

#include "../Editor/AINode.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace eden {

// Result of a pathfinding query
struct PathResult {
    bool found = false;
    std::vector<uint32_t> nodeIds;      // Path as node IDs
    std::vector<glm::vec3> positions;   // Path as world positions
    float totalCost = 0.0f;             // Total weighted cost
    float totalDistance = 0.0f;         // Actual distance in meters
};

// Heuristic types for A*
enum class PathHeuristic {
    EUCLIDEAN,      // Straight-line distance (good for flying)
    MANHATTAN,      // Grid-aligned distance (good for roads)
    DIJKSTRA        // No heuristic (explores all, guaranteed optimal)
};

/**
 * A* Pathfinder for GRAPH nodes.
 * Finds optimal paths considering layers (FLYING, VEHICLE, etc.) and edge costs.
 */
class AStarPathfinder {
public:
    AStarPathfinder();

    // Set the node graph to search (reference to AINodes)
    void setNodes(const std::vector<std::unique_ptr<AINode>>* nodes) { m_nodes = nodes; }

    // Find path between two nodes
    PathResult findPath(uint32_t startNodeId, uint32_t endNodeId,
                        GraphLayer requiredLayer = GraphLayer::ALL,
                        PathHeuristic heuristic = PathHeuristic::EUCLIDEAN);

    // Find path from position to nearest node of category
    PathResult findPathToCategory(const glm::vec3& startPos, GraphCategory targetCategory,
                                  GraphLayer requiredLayer = GraphLayer::ALL,
                                  PathHeuristic heuristic = PathHeuristic::EUCLIDEAN);

    // Find path from one node to nearest node of category
    PathResult findPathToCategory(uint32_t startNodeId, GraphCategory targetCategory,
                                  GraphLayer requiredLayer = GraphLayer::ALL,
                                  PathHeuristic heuristic = PathHeuristic::EUCLIDEAN);

    // Find nearest node to a position
    uint32_t findNearestNode(const glm::vec3& pos, GraphLayer requiredLayer = GraphLayer::ALL) const;

    // Find nearest node of specific category
    uint32_t findNearestNodeOfCategory(const glm::vec3& pos, GraphCategory category,
                                        GraphLayer requiredLayer = GraphLayer::ALL) const;

    // Find all nodes of a category within range
    std::vector<uint32_t> findNodesInRange(const glm::vec3& pos, float range,
                                           GraphCategory category = GraphCategory::NONE,
                                           GraphLayer requiredLayer = GraphLayer::ALL) const;

    // Check if a path exists (faster than full pathfind)
    bool pathExists(uint32_t startNodeId, uint32_t endNodeId,
                    GraphLayer requiredLayer = GraphLayer::ALL);

    // Settings
    void setMaxIterations(int max) { m_maxIterations = max; }
    int getMaxIterations() const { return m_maxIterations; }

    // Debug: get last search stats
    int getLastNodesExplored() const { return m_lastNodesExplored; }
    float getLastSearchTime() const { return m_lastSearchTime; }

private:
    // Internal node for A* algorithm
    struct SearchNode {
        uint32_t nodeId;
        uint32_t parentId;
        float gCost;        // Cost from start
        float hCost;        // Heuristic to end
        float fCost() const { return gCost + hCost; }

        bool operator>(const SearchNode& other) const {
            return fCost() > other.fCost();
        }
    };

    // Get AINode by ID
    const AINode* getNodeById(uint32_t id) const;

    // Calculate heuristic cost
    float calculateHeuristic(const glm::vec3& from, const glm::vec3& to,
                             PathHeuristic heuristic) const;

    // Calculate actual cost between connected nodes
    float calculateEdgeCost(const AINode* from, const AINode* to) const;

    // Check if node is traversable with required layer
    bool isNodeTraversable(const AINode* node, GraphLayer requiredLayer) const;

    // Reconstruct path from search results
    PathResult reconstructPath(uint32_t startId, uint32_t endId,
                               const std::unordered_map<uint32_t, SearchNode>& cameFrom);

    // Node graph reference
    const std::vector<std::unique_ptr<AINode>>* m_nodes = nullptr;

    // Settings
    int m_maxIterations = 10000;

    // Debug stats
    int m_lastNodesExplored = 0;
    float m_lastSearchTime = 0.0f;
};

} // namespace eden
