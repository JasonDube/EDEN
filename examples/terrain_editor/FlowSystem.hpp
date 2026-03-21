#pragma once
/**
 * FlowSystem.hpp — Pipe network connectivity and pressure flow.
 *
 * Walks the port connection graph from a source (e.g., water tower),
 * calculates pressure loss through each piece, and returns a list of
 * open ends where fluid exits (for particle effects).
 *
 * Pressure model:
 *   - Source starts at 100
 *   - Straight pipe: 5% loss
 *   - 90-degree bend: 15% loss
 *   - T-split: divides remaining pressure equally among branches
 *   - Open port with no connection: fluid exits here
 */

#include "Editor/SceneObject.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <algorithm>
#include <iostream>

using eden::SceneObject;

struct FlowOutput {
    glm::vec3 position;      // World-space position of the open port
    glm::vec3 direction;     // World-space direction the fluid shoots
    float pressure;           // 0-100, determines particle intensity
    SceneObject* object;      // Which pipe piece this open port is on
};

struct FlowResult {
    bool active = false;
    std::vector<FlowOutput> openEnds;
    float totalPressure = 0.0f;
    float drainRateGPS = 0.0f;  // Total gallons per second drain across all outlets
};

struct WaterTank {
    float capacityGallons = 6500.0f;
    float currentGallons = 6500.0f;
    bool flowing = false;

    // Returns gallons per second drain rate for a given set of open ends
    static float computeDrainRate(const std::vector<FlowOutput>& openEnds) {
        float total = 0.0f;
        for (const auto& oe : openEnds) {
            // ~5 gallons/min at full pressure per outlet
            total += (oe.pressure / 100.0f) * 5.0f / 60.0f;
        }
        return total;
    }

    // Update tank level, returns false if empty
    bool update(float deltaTime, float drainRate) {
        if (!flowing || currentGallons <= 0.0f) {
            flowing = false;
            return false;
        }
        currentGallons -= drainRate * deltaTime;
        if (currentGallons <= 0.0f) {
            currentGallons = 0.0f;
            flowing = false;
            return false;
        }
        return true;
    }

    float getPercentFull() const { return currentGallons / capacityGallons * 100.0f; }
};

class FlowSystem {
public:
    /**
     * Calculate flow from a source object through connected pipes.
     * Discovers connections by proximity of ports in world space — does NOT
     * depend on m_cpAttachedTo, so it works after save/load.
     *
     * @param source         The source object (e.g., water tower)
     * @param sceneObjects   All scene objects
     * @param startPressure  Initial pressure (default 100)
     * @param snapDist       Max distance between ports to consider them connected
     * @return FlowResult with all open ends and their pressure values
     */
    FlowResult calculateFlow(
        SceneObject* source,
        const std::vector<std::unique_ptr<SceneObject>>& sceneObjects,
        float startPressure = 100.0f,
        float snapDist = 0.5f
    ) {
        FlowResult result;
        if (!source || !source->hasPorts()) return result;

        // Collect all ported objects and their world-space port positions
        struct PortInfo {
            SceneObject* obj;
            int portIndex;
            glm::vec3 worldPos;
            glm::vec3 worldDir;
        };
        std::vector<PortInfo> allPorts;
        for (auto& so : sceneObjects) {
            if (!so || !so->hasPorts() || !so->isVisible()) continue;
            const auto& ports = so->getPorts();
            glm::mat4 mat = so->getTransform().getMatrix();
            for (int pi = 0; pi < static_cast<int>(ports.size()); ++pi) {
                glm::vec3 wp = glm::vec3(mat * glm::vec4(ports[pi].position, 1.0f));
                glm::vec3 wd = glm::normalize(glm::vec3(mat * glm::vec4(ports[pi].forward, 0.0f)));
                allPorts.push_back({so.get(), pi, wp, wd});
            }
        }

        // Build adjacency by finding ports that are close together (connected)
        // Two ports are connected if they're within snapDist of each other
        // and belong to different objects
        std::unordered_map<SceneObject*, std::vector<SceneObject*>> neighbors;
        std::unordered_set<int> connectedPortGlobalIndices;  // Track which ports are connected

        for (size_t a = 0; a < allPorts.size(); ++a) {
            for (size_t b = a + 1; b < allPorts.size(); ++b) {
                if (allPorts[a].obj == allPorts[b].obj) continue;
                float dist = glm::length(allPorts[a].worldPos - allPorts[b].worldPos);
                if (dist < snapDist) {
                    // Only connected if ports face each other (directions oppose)
                    float dot = glm::dot(allPorts[a].worldDir, allPorts[b].worldDir);
                    if (dot > -0.3f) continue;  // Not opposing — skip (e.g., both face up)

                    neighbors[allPorts[a].obj].push_back(allPorts[b].obj);
                    neighbors[allPorts[b].obj].push_back(allPorts[a].obj);
                    connectedPortGlobalIndices.insert(static_cast<int>(a));
                    connectedPortGlobalIndices.insert(static_cast<int>(b));
                }
            }
        }

        // Deduplicate neighbor lists
        for (auto& [obj, neighList] : neighbors) {
            std::sort(neighList.begin(), neighList.end());
            neighList.erase(std::unique(neighList.begin(), neighList.end()), neighList.end());
        }

        // BFS from source
        std::unordered_set<SceneObject*> visited;
        struct QueueEntry {
            SceneObject* obj;
            float pressure;
        };
        std::vector<QueueEntry> queue;
        queue.push_back({source, startPressure});
        visited.insert(source);

        while (!queue.empty()) {
            auto current = queue.front();
            queue.erase(queue.begin());

            SceneObject* obj = current.obj;
            float pressure = current.pressure;

            if (!obj->hasPorts()) continue;

            float afterLoss = applyPressureLoss(obj, pressure);

            // Find unvisited neighbors
            std::vector<SceneObject*> unvisitedNeighbors;
            auto neighIt = neighbors.find(obj);
            if (neighIt != neighbors.end()) {
                for (auto* n : neighIt->second) {
                    if (visited.count(n) == 0) {
                        unvisitedNeighbors.push_back(n);
                    }
                }
            }

            // Split pressure among branches
            int branchCount = std::max(1, static_cast<int>(unvisitedNeighbors.size()));
            float branchPressure = afterLoss / static_cast<float>(branchCount);

            for (auto* n : unvisitedNeighbors) {
                visited.insert(n);
                queue.push_back({n, branchPressure});
            }

            // Find open ports on this object (ports not connected to anything)
            const auto& ports = obj->getPorts();
            glm::mat4 modelMat = obj->getTransform().getMatrix();

            for (int pi = 0; pi < static_cast<int>(ports.size()); ++pi) {
                // Check if this specific port is connected
                bool isConnected = false;
                glm::vec3 portWorldPos = glm::vec3(modelMat * glm::vec4(ports[pi].position, 1.0f));

                glm::vec3 portWorldDir = glm::normalize(glm::vec3(modelMat * glm::vec4(ports[pi].forward, 0.0f)));
                for (const auto& ap : allPorts) {
                    if (ap.obj == obj) continue;
                    if (glm::length(ap.worldPos - portWorldPos) < snapDist) {
                        // Only count as connected if directions oppose
                        float dot = glm::dot(portWorldDir, ap.worldDir);
                        if (dot < -0.3f) {
                            isConnected = true;
                            break;
                        }
                    }
                }

                if (!isConnected) {
                    // Skip source's own open ports (water comes FROM the source, not out of it)
                    if (obj == source) continue;

                    glm::vec3 worldDir = glm::normalize(glm::vec3(modelMat * glm::vec4(ports[pi].forward, 0.0f)));
                    result.openEnds.push_back({portWorldPos, worldDir, afterLoss, obj});
                }
            }
        }

        result.active = !result.openEnds.empty();
        result.totalPressure = startPressure;

        std::cout << "[Flow] Source: " << source->getName()
                  << ", visited " << visited.size() << " pieces"
                  << ", " << result.openEnds.size() << " open ends" << std::endl;
        for (const auto& oe : result.openEnds) {
            std::cout << "  Open end on " << oe.object->getName()
                      << " pressure=" << oe.pressure
                      << " dir=(" << oe.direction.x << "," << oe.direction.y << "," << oe.direction.z << ")"
                      << std::endl;
        }

        return result;
    }

private:
    float applyPressureLoss(SceneObject* obj, float inputPressure) {
        // Classify by port count and name
        const auto& ports = obj->getPorts();
        std::string name = obj->getName();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        // T-connector: 3+ ports
        if (ports.size() >= 3) {
            return inputPressure * 0.80f;  // 20% loss at T-junction
        }

        // 90-degree bend: check name or port directions
        if (name.find("90") != std::string::npos || name.find("elbow") != std::string::npos) {
            return inputPressure * 0.85f;  // 15% loss at bend
        }

        // Check if port directions differ significantly (another way to detect a bend)
        if (ports.size() == 2) {
            float dot = glm::dot(ports[0].forward, ports[1].forward);
            if (dot > -0.5f && dot < 0.5f) {
                // Ports face in different directions — it's a bend
                return inputPressure * 0.85f;
            }
        }

        // Straight pipe: 2 ports facing opposite directions
        return inputPressure * 0.95f;  // 5% loss
    }
};
