#pragma once
/**
 * PortSnapSystem.hpp — Port-based assembly snap for pipes and modular parts.
 *
 * Each piece has Ports (position + forward + up in local space). When placing
 * a new piece, this system finds the best matching port pair between the new
 * piece and any existing scene object, then computes the transform that aligns
 * them: positions match, forwards face each other, ups are aligned.
 */

#include "Editor/SceneObject.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <iostream>

using eden::SceneObject;

struct PortSnapResult {
    bool snapped = false;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    SceneObject* target = nullptr;  // Which object we snapped to
    int newPortIndex = -1;          // Which port on the new object
    int targetPortIndex = -1;       // Which port on the target
};

class PortSnapSystem {
public:
    /**
     * Try to snap a new object to an existing scene object via matching ports.
     *
     * @param newObj        The object being placed (must have ports)
     * @param sceneObjects  All scene objects to check against
     * @param camPos        Camera position (for crosshair ray origin)
     * @param camFront      Camera front direction (for crosshair ray)
     * @param maxRange      Maximum distance for crosshair target detection
     * @return PortSnapResult with transform if a snap was found
     */
    PortSnapResult trySnap(
        SceneObject* newObj,
        const std::vector<std::unique_ptr<SceneObject>>& sceneObjects,
        const glm::vec3& camPos,
        const glm::vec3& camFront,
        float maxRange = 20.0f
    ) {
        PortSnapResult result;
        if (!newObj || !newObj->hasPorts()) return result;

        // Find which scene object the crosshair is on
        SceneObject* targetObj = nullptr;
        float bestDist = maxRange;
        int portsChecked = 0;
        for (auto& so : sceneObjects) {
            if (!so || !so->isVisible()) continue;
            if (so.get() == newObj) continue;
            if (!so->hasPorts()) continue;
            portsChecked++;
            float d = so->getWorldBounds().intersect(camPos, camFront);
            if (d >= 0 && d < bestDist) {
                bestDist = d;
                targetObj = so.get();
            }
        }
        if (!targetObj) return result;

        // Find the best port pair (closest to crosshair hit point)
        glm::vec3 hitPoint = camPos + camFront * bestDist;
        glm::mat4 targetModel = targetObj->getTransform().getMatrix();
        const auto& targetPorts = targetObj->getPorts();
        const auto& newPorts = newObj->getPorts();

        float bestPortDist = 1e9f;
        int bestTargetPort = -1;
        int bestNewPort = -1;

        // Find the target port closest to the crosshair hit point
        for (int ti = 0; ti < static_cast<int>(targetPorts.size()); ++ti) {
            glm::vec3 tWorldPos = glm::vec3(targetModel * glm::vec4(targetPorts[ti].position, 1.0f));
            float d = glm::length(tWorldPos - hitPoint);
            if (d < bestPortDist) {
                bestPortDist = d;
                bestTargetPort = ti;
            }
        }
        if (bestTargetPort < 0) return result;

        // Find the complementary port on the new object:
        // "pipe_start" connects to "pipe_end" and vice versa.
        // Convention: swap "start"↔"end" in the name to find the match.
        bestNewPort = 0;
        std::string targetName = targetPorts[bestTargetPort].name;
        std::string wantName;
        // Try swapping start↔end
        {
            size_t startPos = targetName.find("start");
            size_t endPos = targetName.find("end");
            if (startPos != std::string::npos) {
                wantName = targetName;
                wantName.replace(startPos, 5, "end");
            } else if (endPos != std::string::npos) {
                wantName = targetName;
                wantName.replace(endPos, 3, "start");
            }
        }
        for (int ni = 0; ni < static_cast<int>(newPorts.size()); ++ni) {
            if (!wantName.empty() && newPorts[ni].name == wantName) {
                bestNewPort = ni;
                break;
            }
            // Fallback: if no start/end convention, use any port that ISN'T the same name
            if (wantName.empty() && newPorts[ni].name != targetName) {
                bestNewPort = ni;
                break;
            }
        }

        // Compute alignment transform
        // Goal: newPort.position aligns with targetPort.position
        //        newPort.forward faces opposite to targetPort.forward
        //        newPort.up aligns with targetPort.up
        const auto& tp = targetPorts[bestTargetPort];
        const auto& np = newPorts[bestNewPort];

        // Get target port vectors in world space
        glm::vec3 tPos = glm::vec3(targetModel * glm::vec4(tp.position, 1.0f));
        glm::vec3 tFwd = glm::normalize(glm::vec3(targetModel * glm::vec4(tp.forward, 0.0f)));
        glm::vec3 tUp = glm::normalize(glm::vec3(targetModel * glm::vec4(tp.up, 0.0f)));

        // The new object's port forward should point OPPOSITE to target's forward
        // (pipes face each other at the connection)
        glm::vec3 desiredFwd = -tFwd;
        glm::vec3 desiredUp = tUp;

        // Build rotation that takes np.forward → desiredFwd and np.up → desiredUp
        // Construct basis matrices and derive rotation
        glm::vec3 npRight = glm::normalize(glm::cross(np.forward, np.up));
        glm::vec3 npUp = glm::normalize(glm::cross(npRight, np.forward));
        glm::mat3 srcBasis(npRight, npUp, np.forward);  // Local port basis

        glm::vec3 desiredRight = glm::normalize(glm::cross(desiredFwd, desiredUp));
        desiredUp = glm::normalize(glm::cross(desiredRight, desiredFwd));  // Re-orthogonalize
        glm::mat3 dstBasis(desiredRight, desiredUp, desiredFwd);  // World target basis

        glm::mat3 rotMat = dstBasis * glm::inverse(srcBasis);
        glm::quat rot = glm::quat_cast(rotMat);

        // Compute position: after rotating, the new port's local position moves.
        // We need the world position of newPort to land exactly on tPos.
        glm::vec3 rotatedPortLocal = rot * np.position;
        glm::vec3 translation = tPos - rotatedPortLocal;

        result.snapped = true;
        result.position = translation;
        result.rotation = rot;
        result.target = targetObj;
        result.newPortIndex = bestNewPort;
        result.targetPortIndex = bestTargetPort;

        std::cout << "[PortSnap] " << newObj->getName() << " port '" << np.name
                  << "' → " << targetObj->getName() << " port '" << tp.name << "'" << std::endl;

        return result;
    }
};
