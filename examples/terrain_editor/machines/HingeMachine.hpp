#pragma once
/**
 * HingeMachine.hpp — Hinged door/panel machine type.
 *
 * Detects objects with "hinge_start" ports and animates them open/closed
 * on E-key interaction. The port's UP vector defines the swing axis.
 *
 * Port convention:
 *   - Parent (fridge body): "hinge_end" port at hinge location
 *   - Door (moving piece):  "hinge_start" port at hinge location
 *   - Port forward: snap direction (opposed, like pipes)
 *   - Port up: swing/rotation axis
 *
 * Metadata on the door's .lime file:
 *   meta hinge_angle: 110    (max swing in degrees, default 90)
 *   Use negative angle to reverse swing direction: meta hinge_angle: -110
 *
 * Press E on the door OR the parent body to toggle open/close.
 */

#include "../Machine.hpp"
#include "../MachineHost.hpp"
#include "Editor/SceneObject.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Renderer/ModelRenderer.hpp"  // GPUPointLight

#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

using eden::SceneObject;
using eden::GPUPointLight;

class HingeMachine : public Machine {
public:
    // ── Machine interface ──────────────────────────────────

    bool onInteract(SceneObject* obj, MachineHost& host) override {
        if (!obj->hasPorts()) return false;

        // Check if this is a door (has hinge_start)
        int hingeStartIdx = -1;
        bool hasHingeEnd = false;
        const auto& ports = obj->getPorts();
        for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
            if (ports[i].name.find("hinge") != std::string::npos) {
                if (ports[i].name.find("start") != std::string::npos)
                    hingeStartIdx = i;
                if (ports[i].name.find("end") != std::string::npos)
                    hasHingeEnd = true;
            }
        }

        // If this is a parent body (has hinge_end), find and toggle its door
        if (hasHingeEnd && hingeStartIdx < 0) {
            for (auto& hd : m_doors) {
                if (hd.parent == obj) {
                    hd.isOpen = !hd.isOpen;
                    hd.targetAngle = hd.isOpen ? hd.maxAngle : 0.0f;
                    return true;
                }
            }
            return false;
        }

        if (hingeStartIdx < 0) return false;

        // Check if this door is already tracked
        for (auto& hd : m_doors) {
            if (hd.door == obj) {
                hd.isOpen = !hd.isOpen;
                hd.targetAngle = hd.isOpen ? hd.maxAngle : 0.0f;
                return true;
            }
        }

        // First interaction — register this door
        const auto& port = ports[hingeStartIdx];
        glm::mat4 modelMat = obj->getTransform().getMatrix();
        glm::quat objRot = obj->getTransform().getRotation();

        // Compute hinge point and axis in world space from the door's current (closed) state
        glm::vec3 hingeWorldPos = glm::vec3(modelMat * glm::vec4(port.position, 1.0f));
        glm::vec3 hingeWorldAxis = glm::normalize(objRot * port.up);

        // Read max angle from metadata (default -90: opens outward)
        float maxAngle = -90.0f;
        std::string angleStr = obj->getMetaValue("hinge_angle");
        if (!angleStr.empty()) {
            try { maxAngle = -std::stof(angleStr); } catch (...) {}
        }

        // Find the parent body (nearest object with hinge_end port)
        SceneObject* parent = nullptr;
        float bestParentDist = 1.0f;  // Max 1m snap distance
        for (auto& so : host.getSceneObjects()) {
            if (!so || so.get() == obj || !so->hasPorts()) continue;
            for (const auto& p : so->getPorts()) {
                if (p.name.find("hinge") != std::string::npos &&
                    p.name.find("end") != std::string::npos) {
                    glm::vec3 pWorld = glm::vec3(so->getTransform().getMatrix() *
                                                  glm::vec4(p.position, 1.0f));
                    float d = glm::length(pWorld - hingeWorldPos);
                    if (d < bestParentDist) {
                        bestParentDist = d;
                        parent = so.get();
                    }
                }
            }
        }

        HingedDoor hd;
        hd.door = obj;
        hd.parent = parent;
        hd.closedPos = obj->getTransform().getPosition();
        hd.closedRot = objRot;
        hd.hingeWorldPos = hingeWorldPos;
        hd.hingeWorldAxis = hingeWorldAxis;
        hd.maxAngle = maxAngle;
        hd.currentAngle = 0.0f;
        hd.targetAngle = maxAngle;  // Open on first interaction
        hd.isOpen = true;

        // Check parent for interior_light port
        if (parent && parent->hasPorts()) {
            glm::mat4 parentMat = parent->getTransform().getMatrix();
            for (const auto& p : parent->getPorts()) {
                if (p.name.find("interior_light") != std::string::npos) {
                    hd.hasInteriorLight = true;
                    hd.interiorLightPos = glm::vec3(parentMat * glm::vec4(p.position, 1.0f));
                    break;
                }
            }
        }

        m_doors.push_back(hd);
        return true;
    }

    void update(float deltaTime, MachineHost& host) override {
        constexpr float swingSpeed = 180.0f;  // degrees per second

        for (auto& hd : m_doors) {
            if (std::abs(hd.currentAngle - hd.targetAngle) < 0.1f) {
                hd.currentAngle = hd.targetAngle;
                continue;
            }

            // Lerp toward target
            float dir = (hd.targetAngle > hd.currentAngle) ? 1.0f : -1.0f;
            hd.currentAngle += dir * swingSpeed * deltaTime;

            // Clamp
            if (dir > 0 && hd.currentAngle > hd.targetAngle) hd.currentAngle = hd.targetAngle;
            if (dir < 0 && hd.currentAngle < hd.targetAngle) hd.currentAngle = hd.targetAngle;

            // Apply rotation around hinge point
            glm::quat swingQuat = glm::angleAxis(glm::radians(hd.currentAngle), hd.hingeWorldAxis);
            hd.door->getTransform().setRotation(swingQuat * hd.closedRot);

            // Pivot position around hinge point
            glm::vec3 offset = hd.closedPos - hd.hingeWorldPos;
            hd.door->getTransform().setPosition(hd.hingeWorldPos + swingQuat * offset);
        }
    }

    void onPickup(SceneObject* obj, MachineHost& host) override {
        removeDoor(obj);
    }

    void onDelete(SceneObject* obj) override {
        removeDoor(obj);
        // Also remove any doors whose parent was deleted
        for (auto it = m_doors.begin(); it != m_doors.end(); ) {
            if (it->parent == obj) {
                it->door->getTransform().setPosition(it->closedPos);
                it->door->getTransform().setRotation(it->closedRot);
                it = m_doors.erase(it);
            } else {
                ++it;
            }
        }
    }

    void shutdown(MachineHost& host) override {
        // Restore all doors to closed position
        for (auto& hd : m_doors) {
            hd.door->getTransform().setPosition(hd.closedPos);
            hd.door->getTransform().setRotation(hd.closedRot);
        }
        m_doors.clear();
    }

    bool isRunning(SceneObject* obj) const override {
        for (const auto& hd : m_doors) {
            if (hd.door == obj && hd.isOpen) return true;
        }
        return false;
    }

    // Check if this object is a parent body with an open hinged door
    bool hasOpenDoor(SceneObject* obj) const {
        for (const auto& hd : m_doors) {
            if (hd.parent == obj && hd.isOpen) return true;
        }
        return false;
    }

    // Returns point lights for open doors with interior_light ports
    std::vector<GPUPointLight> getActiveLights() const {
        std::vector<GPUPointLight> lights;
        for (const auto& hd : m_doors) {
            if (!hd.isOpen || !hd.hasInteriorLight) continue;
            // Only emit light when door is mostly open (> 30% swing)
            if (std::abs(hd.currentAngle) < std::abs(hd.maxAngle) * 0.3f) continue;
            GPUPointLight light;
            light.position = glm::vec4(hd.interiorLightPos, 2.0f);  // 2m radius
            light.color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);     // warm white, moderate
            light.direction = glm::vec4(0.0f);                       // point light
            lights.push_back(light);
        }
        return lights;
    }

private:
    struct HingedDoor {
        SceneObject* door;
        SceneObject* parent = nullptr; // The body this door is attached to
        glm::vec3 closedPos;       // World position when closed
        glm::quat closedRot;       // World rotation when closed
        glm::vec3 hingeWorldPos;   // Hinge pivot in world space
        glm::vec3 hingeWorldAxis;  // Swing axis in world space
        float maxAngle;            // Max swing (degrees, negative = reverse)
        float currentAngle;        // Current swing angle
        float targetAngle;         // Target angle (0=closed, max=open)
        bool isOpen;
        bool hasInteriorLight = false;
        glm::vec3 interiorLightPos{0}; // World position of interior light
    };

    std::vector<HingedDoor> m_doors;

    void removeDoor(SceneObject* obj) {
        for (auto it = m_doors.begin(); it != m_doors.end(); ++it) {
            if (it->door == obj) {
                it->door->getTransform().setPosition(it->closedPos);
                it->door->getTransform().setRotation(it->closedRot);
                m_doors.erase(it);
                return;
            }
        }
    }
};
