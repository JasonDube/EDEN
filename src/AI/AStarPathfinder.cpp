#include "AStarPathfinder.hpp"
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace eden {

AStarPathfinder::AStarPathfinder() {
}

const AINode* AStarPathfinder::getNodeById(uint32_t id) const {
    if (!m_nodes) return nullptr;

    for (const auto& node : *m_nodes) {
        if (node && node->getId() == id) {
            return node.get();
        }
    }
    return nullptr;
}

float AStarPathfinder::calculateHeuristic(const glm::vec3& from, const glm::vec3& to,
                                          PathHeuristic heuristic) const {
    switch (heuristic) {
        case PathHeuristic::EUCLIDEAN: {
            // Straight-line distance
            glm::vec3 diff = to - from;
            return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
        }

        case PathHeuristic::MANHATTAN: {
            // Grid-aligned distance (sum of axis distances)
            glm::vec3 diff = glm::abs(to - from);
            return diff.x + diff.y + diff.z;
        }

        case PathHeuristic::DIJKSTRA:
        default:
            // No heuristic - explores uniformly
            return 0.0f;
    }
}

float AStarPathfinder::calculateEdgeCost(const AINode* from, const AINode* to) const {
    // Base cost is distance
    glm::vec3 diff = to->getPosition() - from->getPosition();
    float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

    // Multiply by edge costs of both nodes (average)
    float costMultiplier = (from->getEdgeCost() + to->getEdgeCost()) * 0.5f;

    return distance * costMultiplier;
}

bool AStarPathfinder::isNodeTraversable(const AINode* node, GraphLayer requiredLayer) const {
    if (!node) return false;
    if (!node->isVisible()) return false;

    // If no specific layer required, any GRAPH node is valid
    if (requiredLayer == GraphLayer::ALL) {
        return node->getType() == AINodeType::GRAPH;
    }

    // Check if node supports the required layer
    if (node->getType() == AINodeType::GRAPH) {
        return node->hasLayer(requiredLayer);
    }

    return false;
}

PathResult AStarPathfinder::findPath(uint32_t startNodeId, uint32_t endNodeId,
                                      GraphLayer requiredLayer, PathHeuristic heuristic) {
    auto startTime = std::chrono::high_resolution_clock::now();

    PathResult result;
    result.found = false;
    m_lastNodesExplored = 0;

    if (!m_nodes || m_nodes->empty()) {
        return result;
    }

    const AINode* startNode = getNodeById(startNodeId);
    const AINode* endNode = getNodeById(endNodeId);

    if (!startNode || !endNode) {
        return result;
    }

    // Check if start and end are traversable
    // Note: We allow start/end to be any node type, but intermediate nodes must be GRAPH
    if (!endNode->isVisible()) {
        return result;
    }

    // Priority queue (min-heap by fCost)
    auto compare = [](const SearchNode& a, const SearchNode& b) {
        return a.fCost() > b.fCost();
    };
    std::priority_queue<SearchNode, std::vector<SearchNode>, decltype(compare)> openSet(compare);

    // Track visited nodes and best paths
    std::unordered_set<uint32_t> closedSet;
    std::unordered_map<uint32_t, SearchNode> allNodes;

    // Initialize start node
    SearchNode start;
    start.nodeId = startNodeId;
    start.parentId = startNodeId;
    start.gCost = 0.0f;
    start.hCost = calculateHeuristic(startNode->getPosition(), endNode->getPosition(), heuristic);

    openSet.push(start);
    allNodes[startNodeId] = start;

    int iterations = 0;

    while (!openSet.empty() && iterations < m_maxIterations) {
        iterations++;
        m_lastNodesExplored++;

        // Get node with lowest fCost
        SearchNode current = openSet.top();
        openSet.pop();

        // Skip if already processed
        if (closedSet.count(current.nodeId)) {
            continue;
        }

        // Found the goal!
        if (current.nodeId == endNodeId) {
            result = reconstructPath(startNodeId, endNodeId, allNodes);
            result.found = true;

            auto endTime = std::chrono::high_resolution_clock::now();
            m_lastSearchTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

            return result;
        }

        closedSet.insert(current.nodeId);

        // Get current node
        const AINode* currentNode = getNodeById(current.nodeId);
        if (!currentNode) continue;

        // Explore neighbors (connected nodes)
        for (uint32_t neighborId : currentNode->getConnections()) {
            // Skip if already processed
            if (closedSet.count(neighborId)) {
                continue;
            }

            const AINode* neighborNode = getNodeById(neighborId);
            if (!neighborNode) continue;

            // Check if neighbor is traversable (unless it's the destination)
            if (neighborId != endNodeId && !isNodeTraversable(neighborNode, requiredLayer)) {
                continue;
            }

            // Calculate tentative gCost
            float edgeCost = calculateEdgeCost(currentNode, neighborNode);
            float tentativeG = current.gCost + edgeCost;

            // Check if this path is better than any previous one
            auto it = allNodes.find(neighborId);
            if (it != allNodes.end() && tentativeG >= it->second.gCost) {
                continue; // Not a better path
            }

            // This is a better path, record it
            SearchNode neighbor;
            neighbor.nodeId = neighborId;
            neighbor.parentId = current.nodeId;
            neighbor.gCost = tentativeG;
            neighbor.hCost = calculateHeuristic(neighborNode->getPosition(),
                                                 endNode->getPosition(), heuristic);

            openSet.push(neighbor);
            allNodes[neighborId] = neighbor;
        }
    }

    // No path found
    auto endTime = std::chrono::high_resolution_clock::now();
    m_lastSearchTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    return result;
}

PathResult AStarPathfinder::reconstructPath(uint32_t startId, uint32_t endId,
                                             const std::unordered_map<uint32_t, SearchNode>& cameFrom) {
    PathResult result;
    result.found = true;
    result.totalCost = 0.0f;
    result.totalDistance = 0.0f;

    // Trace back from end to start
    uint32_t currentId = endId;
    while (currentId != startId) {
        result.nodeIds.push_back(currentId);

        const AINode* node = getNodeById(currentId);
        if (node) {
            result.positions.push_back(node->getPosition());
        }

        auto it = cameFrom.find(currentId);
        if (it == cameFrom.end()) {
            break; // Shouldn't happen
        }

        result.totalCost = it->second.gCost; // Final gCost is total cost
        currentId = it->second.parentId;
    }

    // Add start node
    result.nodeIds.push_back(startId);
    const AINode* startNode = getNodeById(startId);
    if (startNode) {
        result.positions.push_back(startNode->getPosition());
    }

    // Reverse to get start-to-end order
    std::reverse(result.nodeIds.begin(), result.nodeIds.end());
    std::reverse(result.positions.begin(), result.positions.end());

    // Calculate total distance
    for (size_t i = 1; i < result.positions.size(); i++) {
        glm::vec3 diff = result.positions[i] - result.positions[i - 1];
        result.totalDistance += std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
    }

    return result;
}

PathResult AStarPathfinder::findPathToCategory(const glm::vec3& startPos, GraphCategory targetCategory,
                                                GraphLayer requiredLayer, PathHeuristic heuristic) {
    // Find nearest node to start position
    uint32_t startNodeId = findNearestNode(startPos, requiredLayer);
    if (startNodeId == 0) {
        return PathResult();
    }

    return findPathToCategory(startNodeId, targetCategory, requiredLayer, heuristic);
}

PathResult AStarPathfinder::findPathToCategory(uint32_t startNodeId, GraphCategory targetCategory,
                                                GraphLayer requiredLayer, PathHeuristic heuristic) {
    PathResult bestResult;
    bestResult.found = false;
    float bestCost = std::numeric_limits<float>::max();

    if (!m_nodes) return bestResult;

    // Find all nodes of the target category
    for (const auto& node : *m_nodes) {
        if (!node || !node->isVisible()) continue;
        if (node->getType() != AINodeType::GRAPH) continue;
        if (node->getCategory() != targetCategory) continue;

        // Check layer compatibility
        if (requiredLayer != GraphLayer::ALL && !node->hasLayer(requiredLayer)) {
            continue;
        }

        // Find path to this node
        PathResult pathResult = findPath(startNodeId, node->getId(), requiredLayer, heuristic);

        if (pathResult.found && pathResult.totalCost < bestCost) {
            bestCost = pathResult.totalCost;
            bestResult = pathResult;
        }
    }

    return bestResult;
}

uint32_t AStarPathfinder::findNearestNode(const glm::vec3& pos, GraphLayer requiredLayer) const {
    if (!m_nodes) return 0;

    uint32_t nearestId = 0;
    float nearestDist = std::numeric_limits<float>::max();

    for (const auto& node : *m_nodes) {
        if (!node || !node->isVisible()) continue;

        // For general nearest, accept any visible node
        // For specific layer, must be GRAPH with matching layer
        if (requiredLayer != GraphLayer::ALL) {
            if (node->getType() != AINodeType::GRAPH) continue;
            if (!node->hasLayer(requiredLayer)) continue;
        }

        glm::vec3 diff = node->getPosition() - pos;
        float dist = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z; // Squared distance

        if (dist < nearestDist) {
            nearestDist = dist;
            nearestId = node->getId();
        }
    }

    return nearestId;
}

uint32_t AStarPathfinder::findNearestNodeOfCategory(const glm::vec3& pos, GraphCategory category,
                                                     GraphLayer requiredLayer) const {
    if (!m_nodes) return 0;

    uint32_t nearestId = 0;
    float nearestDist = std::numeric_limits<float>::max();

    for (const auto& node : *m_nodes) {
        if (!node || !node->isVisible()) continue;
        if (node->getType() != AINodeType::GRAPH) continue;
        if (node->getCategory() != category) continue;

        if (requiredLayer != GraphLayer::ALL && !node->hasLayer(requiredLayer)) {
            continue;
        }

        glm::vec3 diff = node->getPosition() - pos;
        float dist = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

        if (dist < nearestDist) {
            nearestDist = dist;
            nearestId = node->getId();
        }
    }

    return nearestId;
}

std::vector<uint32_t> AStarPathfinder::findNodesInRange(const glm::vec3& pos, float range,
                                                         GraphCategory category,
                                                         GraphLayer requiredLayer) const {
    std::vector<uint32_t> result;
    if (!m_nodes) return result;

    float rangeSq = range * range;

    for (const auto& node : *m_nodes) {
        if (!node || !node->isVisible()) continue;

        // Category filter (NONE means any category)
        if (category != GraphCategory::NONE) {
            if (node->getType() != AINodeType::GRAPH) continue;
            if (node->getCategory() != category) continue;
        }

        // Layer filter
        if (requiredLayer != GraphLayer::ALL) {
            if (node->getType() != AINodeType::GRAPH) continue;
            if (!node->hasLayer(requiredLayer)) continue;
        }

        // Distance check
        glm::vec3 diff = node->getPosition() - pos;
        float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

        if (distSq <= rangeSq) {
            result.push_back(node->getId());
        }
    }

    return result;
}

bool AStarPathfinder::pathExists(uint32_t startNodeId, uint32_t endNodeId, GraphLayer requiredLayer) {
    // Quick check using A* but we can return early once found
    PathResult result = findPath(startNodeId, endNodeId, requiredLayer, PathHeuristic::EUCLIDEAN);
    return result.found;
}

} // namespace eden
