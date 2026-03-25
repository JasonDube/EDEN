#pragma once
/**
 * CompressorMachine.hpp — Compressor machine type.
 *
 * Generic compressor that provides cooling to whatever it's connected to.
 * Connected to a fridge body → cooling zone for perishables.
 * Connected to an air tank → pressurized air (future).
 *
 * Port convention:
 *   - Compressor model: "compressor_start" port + "wire_in" control point
 *   - Appliance (fridge, etc): "compressor_end" port
 *
 * Power: 12 units from electrical system.
 * Audio: crossfade loop "assets/sounds/refrigerator_compressor.wav"
 */

#include "../Machine.hpp"
#include "../MachineHost.hpp"
#include "Editor/SceneObject.hpp"
#include <eden/Audio.hpp>
#include <glm/glm.hpp>

#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <iostream>

using eden::SceneObject;
using eden::Audio;

class CompressorMachine : public Machine {
public:
    // ── Machine interface ──────────────────────────────────

    bool onInteract(SceneObject* obj, MachineHost& host) override {
        if (!obj->hasPorts()) return false;

        // Check if this object has a compressor_start port
        bool hasCompressorStart = false;
        const auto& ports = obj->getPorts();
        for (const auto& p : ports) {
            if (p.name.find("compressor") != std::string::npos &&
                p.name.find("start") != std::string::npos) {
                hasCompressorStart = true;
                break;
            }
        }
        if (!hasCompressorStart) return false;

        auto it = m_loops.find(obj);
        if (it != m_loops.end() && it->second >= 0) {
            turnOff(obj, host);
        } else {
            // Require power from electrical system
            if (!host.canPowerReach(obj)) {
                host.showScreenMessage("No power — wire to a running generator", 2.0f);
                return true;
            }
            turnOn(obj, host);
        }
        return true;
    }

    void update(float deltaTime, MachineHost& host) override {
        glm::vec3 camPos = host.getCameraPosition();

        // Check power and turn off if lost
        std::vector<SceneObject*> lostPower;
        for (auto& [obj, loopId] : m_loops) {
            if (loopId < 0) continue;
            if (!host.canPowerReach(obj)) {
                lostPower.push_back(obj);
            }
        }
        for (auto* obj : lostPower) {
            turnOff(obj, host);
        }

        for (auto& [obj, loopId] : m_loops) {
            if (loopId < 0) continue;

            // Distance-based sound attenuation
            glm::vec3 machinePos = obj->getTransform().getPosition();
            float dist = glm::length(machinePos - camPos);
            float vol = 0.0f;
            if (dist < m_innerRadius) {
                vol = m_baseVolume;
            } else if (dist < m_outerRadius) {
                vol = m_baseVolume * (1.0f - (dist - m_innerRadius) / (m_outerRadius - m_innerRadius));
            }
            Audio::getInstance().setLoopVolume(loopId, vol);
            Audio::getInstance().setLoopVolume(loopId + 1, vol);
        }
    }

    void onPickup(SceneObject* obj, MachineHost& host) override {
        auto it = m_loops.find(obj);
        if (it != m_loops.end() && it->second >= 0) {
            Audio::getInstance().stopLoop(it->second);
            Audio::getInstance().stopLoop(it->second + 1);
            m_loops.erase(it);
        }
        m_coolingZones.erase(obj);
    }

    void onDelete(SceneObject* obj) override {
        auto loopIt = m_loops.find(obj);
        if (loopIt != m_loops.end()) {
            if (loopIt->second >= 0) {
                Audio::getInstance().stopLoop(loopIt->second);
                Audio::getInstance().stopLoop(loopIt->second + 1);
            }
            m_loops.erase(loopIt);
        }
        m_coolingZones.erase(obj);
    }

    void shutdown(MachineHost& host) override {
        for (auto& [obj, loopId] : m_loops) {
            if (loopId >= 0) {
                Audio::getInstance().stopLoop(loopId);
                Audio::getInstance().stopLoop(loopId + 1);
            }
        }
        m_loops.clear();
        m_coolingZones.clear();
    }

    bool isRunning(SceneObject* obj) const override {
        auto it = m_loops.find(obj);
        return it != m_loops.end() && it->second >= 0;
    }

    // ── Public API for perishable system ──────────────────

    // Check if a world position is inside any active cooling zone
    bool isInsideCoolingZone(const glm::vec3& pos) const {
        for (const auto& [compressor, zone] : m_coolingZones) {
            auto loopIt = m_loops.find(compressor);
            if (loopIt == m_loops.end() || loopIt->second < 0) continue;
            if (pos.x >= zone.min.x && pos.x <= zone.max.x &&
                pos.y >= zone.min.y && pos.y <= zone.max.y &&
                pos.z >= zone.min.z && pos.z <= zone.max.z) {
                return true;
            }
        }
        return false;
    }

    static constexpr float POWER_REQUIRED = 12.0f;

private:
    struct CoolingZone {
        glm::vec3 min;
        glm::vec3 max;
    };

    std::unordered_map<SceneObject*, int> m_loops;           // compressor → audio loop ID
    std::unordered_map<SceneObject*, CoolingZone> m_coolingZones; // compressor → cooling AABB

    static constexpr float m_baseVolume = 0.15f;   // Quieter than generator
    static constexpr float m_innerRadius = 5.0f;
    static constexpr float m_outerRadius = 25.0f;

    void turnOn(SceneObject* obj, MachineHost& host) {
        int loopId = Audio::getInstance().startCrossfadeLoop(
            "assets/sounds/refrigerator_compressor.wav", m_baseVolume);
        if (loopId < 0) return;

        m_loops[obj] = loopId;

        // Find the connected appliance (nearest object with compressor_end port)
        // and use its AABB as the cooling zone
        glm::mat4 compMat = obj->getTransform().getMatrix();
        glm::vec3 compPortWorld(0);
        for (const auto& p : obj->getPorts()) {
            if (p.name.find("compressor") != std::string::npos &&
                p.name.find("start") != std::string::npos) {
                compPortWorld = glm::vec3(compMat * glm::vec4(p.position, 1.0f));
                break;
            }
        }

        SceneObject* appliance = nullptr;
        float bestDist = 1.0f;  // Max 1m snap distance
        for (auto& so : host.getSceneObjects()) {
            if (!so || so.get() == obj || !so->hasPorts()) continue;
            for (const auto& p : so->getPorts()) {
                if (p.name.find("compressor") != std::string::npos &&
                    p.name.find("end") != std::string::npos) {
                    glm::vec3 pWorld = glm::vec3(so->getTransform().getMatrix() *
                                                  glm::vec4(p.position, 1.0f));
                    float d = glm::length(pWorld - compPortWorld);
                    if (d < bestDist) {
                        bestDist = d;
                        appliance = so.get();
                    }
                }
            }
        }

        if (appliance) {
            auto bounds = appliance->getWorldBounds();
            CoolingZone zone;
            zone.min = bounds.min;
            zone.max = bounds.max;
            m_coolingZones[obj] = zone;
            host.showScreenMessage("Compressor ON — cooling active", 2.0f);
        } else {
            host.showScreenMessage("Compressor ON — no appliance connected", 2.0f);
        }
    }

    void turnOff(SceneObject* obj, MachineHost& host) {
        auto it = m_loops.find(obj);
        if (it == m_loops.end()) return;

        Audio::getInstance().stopLoop(it->second);
        Audio::getInstance().stopLoop(it->second + 1);
        m_loops.erase(it);
        m_coolingZones.erase(obj);

        host.showScreenMessage("Compressor OFF", 2.0f);
    }
};
