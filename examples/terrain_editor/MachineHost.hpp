#pragma once
/**
 * MachineHost.hpp — Interface for machines to access host application resources.
 *
 * The host (TerrainEditor, etc.) implements this so Machine subclasses can
 * access scene objects, physics, particles, and other systems without
 * depending directly on the host class.
 */

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace eden {
class SceneObject;
class ParticleRenderer;
}

struct MachineHost {
    virtual ~MachineHost() = default;

    // Scene access
    virtual std::vector<std::unique_ptr<eden::SceneObject>>& getSceneObjects() = 0;

    // Particle renderer (may return nullptr)
    virtual eden::ParticleRenderer* getParticleRenderer() = 0;

    // CP attachment tracking (child → parent)
    virtual std::unordered_map<eden::SceneObject*, eden::SceneObject*>& getCPAttachments() = 0;

    // Get world position of a control point by prefix search
    virtual bool getCPWorldPos(eden::SceneObject* obj, const std::string& cpPrefix, glm::vec3& outPos) = 0;

    // Show a screen message to the player
    virtual void showScreenMessage(const std::string& msg, float duration) = 0;
};
