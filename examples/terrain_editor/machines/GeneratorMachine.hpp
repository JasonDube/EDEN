#pragma once
/**
 * GeneratorMachine.hpp — Generator machine type.
 *
 * Handles: crossfade audio loop, fan spinning, smoke particles.
 * Fans only spin if they are CP-attached to this generator.
 */

#include "../Machine.hpp"
#include "../MachineHost.hpp"
#include "Editor/SceneObject.hpp"
#include "Renderer/ParticleRenderer.hpp"
#include <eden/Audio.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cfloat>
#include <iostream>

using eden::SceneObject;
using eden::Audio;
using eden::ParticleRenderer;

class GeneratorMachine : public Machine {
public:
    // ── Machine interface ──────────────────────────────────

    bool onInteract(SceneObject* obj, MachineHost& host) override {
        std::string name = obj->getName();
        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find("generator") == std::string::npos) return false;
        // Don't activate from attached parts that have "generator" in their name
        if (nameLower.find("fan") != std::string::npos) return false;
        if (nameLower.find("outlet") != std::string::npos) return false;

        auto it = m_loops.find(obj);
        if (it != m_loops.end() && it->second >= 0) {
            turnOff(obj, host);
        } else {
            turnOn(obj, host);
        }
        return true;
    }

    void update(float deltaTime, MachineHost& host) override {
        // Drain fuel from running generators
        for (auto it = m_state.begin(); it != m_state.end(); ++it) {
            auto loopIt = m_loops.find(it->first);
            if (loopIt == m_loops.end() || loopIt->second < 0) continue;

            // Burn fuel
            it->second.fuelLevel -= it->second.fuelBurnRate * deltaTime / 60.0f;
            if (it->second.fuelLevel <= 0.0f) {
                it->second.fuelLevel = 0.0f;
                // Out of fuel — shut down
                turnOff(it->first, host);
                host.showScreenMessage("Generator out of fuel!", 3.0f);
                break;  // Iterator invalidated by turnOff
            }
        }

        // Spin fans
        if (m_fans.empty()) return;
        float spinSpeed = 360.0f; // degrees per second
        for (auto& sf : m_fans) {
            sf.angle += spinSpeed * deltaTime;
            if (sf.angle > 360.0f) sf.angle -= 360.0f;
            // Spin around the centroid, not the local origin
            glm::quat spinQ = glm::angleAxis(glm::radians(sf.angle), sf.axis);
            glm::quat finalQ = sf.baseQuat * spinQ;
            sf.obj->getTransform().setRotation(finalQ);
            // Adjust position so rotation happens around centroid
            glm::vec3 offset = sf.baseQuat * sf.center - finalQ * sf.center;
            sf.obj->getTransform().setPosition(sf.basePos + offset);
        }
    }

    void onPickup(SceneObject* obj, MachineHost& host) override {
        auto it = m_loops.find(obj);
        if (it != m_loops.end() && it->second >= 0) {
            // Stop audio
            Audio::getInstance().stopLoop(it->second);
            Audio::getInstance().stopLoop(it->second + 1);
            m_loops.erase(it);

            // Stop smoke
            removeSmokeFor(obj, host);

            // Stop fans attached to this generator (restore their base transforms)
            stopFansFor(obj, host);
        }
    }

    void onDelete(SceneObject* obj) override {
        // If this generator was running, stop its audio
        auto loopIt = m_loops.find(obj);
        if (loopIt != m_loops.end()) {
            if (loopIt->second >= 0) {
                Audio::getInstance().stopLoop(loopIt->second);
                Audio::getInstance().stopLoop(loopIt->second + 1);
            }
            m_loops.erase(loopIt);
        }
        m_state.erase(obj);

        // Remove any fans that reference this object (either as the fan itself or its generator)
        m_fans.erase(
            std::remove_if(m_fans.begin(), m_fans.end(),
                [obj](const SpinningFan& sf) { return sf.obj == obj || sf.generator == obj; }),
            m_fans.end());
    }

    void shutdown(MachineHost& host) override {
        // Stop all audio loops
        for (auto& [obj, loopId] : m_loops) {
            if (loopId >= 0) {
                Audio::getInstance().stopLoop(loopId);
                Audio::getInstance().stopLoop(loopId + 1);
            }
        }
        // Remove all smoke emitters
        for (auto& [obj, loopId] : m_loops) {
            removeSmokeFor(obj, host);
        }
        m_loops.clear();

        // Restore all fan transforms
        for (auto& sf : m_fans) {
            sf.obj->setEulerRotation(sf.baseEuler);
            sf.obj->getTransform().setPosition(sf.basePos);
        }
        m_fans.clear();
    }

    bool isRunning(SceneObject* obj) const override {
        auto it = m_loops.find(obj);
        return it != m_loops.end() && it->second >= 0;
    }

private:
    // ── Internal types ─────────────────────────────────────

    struct SpinningFan {
        SceneObject* obj;           // The fan SceneObject
        SceneObject* generator;     // The generator it belongs to
        float angle;                // Accumulated degrees
        glm::vec3 baseEuler;        // Euler rotation before spinning started
        glm::quat baseQuat;         // Base quaternion
        glm::vec3 axis;             // Local-space spin axis
        glm::vec3 center;           // Local-space centroid of mesh
        glm::vec3 basePos;          // World position before spinning started
    };

    // Generator power/fuel state (per generator instance)
    struct GeneratorState {
        float maxPower = 100.0f;        // Total power units this generator produces
        float usedPower = 0.0f;         // Power consumed by connected devices
        float fuelCapacity = 50.0f;     // Fuel capacity in gallons
        float fuelLevel = 50.0f;        // Current fuel in gallons
        float fuelBurnRate = 0.5f;      // Gallons per minute while running
    };

    // ── State ──────────────────────────────────────────────

    std::unordered_map<SceneObject*, int> m_loops;            // generator → audio loop ID
    std::unordered_map<SceneObject*, GeneratorState> m_state; // generator → power/fuel state
    std::vector<SpinningFan> m_fans;                          // all spinning fans

    // ── Helpers ────────────────────────────────────────────

public:
    // Get generator state for HUD display (returns nullptr if not tracked)
    const GeneratorState* getState(SceneObject* obj) const {
        auto it = m_state.find(obj);
        return (it != m_state.end()) ? &it->second : nullptr;
    }
private:

    void turnOn(SceneObject* obj, MachineHost& host) {
        int loopId = Audio::getInstance().startCrossfadeLoop("assets/sounds/generator.wav", 0.3f);
        if (loopId < 0) return;

        m_loops[obj] = loopId;

        // Initialize power/fuel state if not already tracked
        if (m_state.find(obj) == m_state.end()) {
            m_state[obj] = GeneratorState{};
        }

        // Find attached fans by port proximity (survives save/load)
        // Generator has "fan_end" port, fan has "fan_start" port — find fans whose
        // start port is near the generator's fan_end port
        glm::mat4 genMat = obj->getTransform().getMatrix();
        glm::vec3 genFanPortWorld(0);
        bool hasGenFanPort = false;
        if (obj->hasPorts()) {
            for (const auto& gp : obj->getPorts()) {
                if (gp.name.find("fan") != std::string::npos) {
                    genFanPortWorld = glm::vec3(genMat * glm::vec4(gp.position, 1.0f));
                    hasGenFanPort = true;
                    break;
                }
            }
        }

        for (auto& so : host.getSceneObjects()) {
            if (!so || so.get() == obj) continue;
            std::string nameLower = so->getName();
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find("fan") == std::string::npos) continue;

            // Check port proximity if generator has a fan port
            if (hasGenFanPort && so->hasPorts()) {
                glm::mat4 fanMat = so->getTransform().getMatrix();
                bool portMatch = false;
                for (const auto& fp : so->getPorts()) {
                    glm::vec3 fpWorld = glm::vec3(fanMat * glm::vec4(fp.position, 1.0f));
                    if (glm::length(fpWorld - genFanPortWorld) < 0.5f) {
                        portMatch = true;
                        break;
                    }
                }
                if (!portMatch) continue;
            }
            // Fallback for fans without ports: use CP attachment (legacy)
            else if (hasGenFanPort) {
                auto& attachments = host.getCPAttachments();
                auto attachIt = attachments.find(so.get());
                if (attachIt == attachments.end() || attachIt->second != obj) continue;
            }

            // Use the fan's port direction as spin axis (much more reliable than bbox guessing)
            glm::vec3 spinAxis(0, 1, 0);
            glm::vec3 center(0);
            glm::mat4 fanMat = so->getTransform().getMatrix();
            if (so->hasPorts()) {
                for (const auto& fp : so->getPorts()) {
                    if (fp.name.find("start") != std::string::npos || fp.name.find("fan") != std::string::npos) {
                        // Port direction in local space IS the spin axis
                        spinAxis = fp.forward;
                        center = fp.position;
                        break;
                    }
                }
            } else if (so->hasMeshData()) {
                // Legacy fallback: guess from bounding box
                const auto& verts = so->getVertices();
                glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
                for (const auto& v : verts) {
                    mn = glm::min(mn, v.position);
                    mx = glm::max(mx, v.position);
                }
                center = (mn + mx) * 0.5f;
                glm::vec3 ext = mx - mn;
                float areaXY = ext.x * ext.y;
                float areaXZ = ext.x * ext.z;
                float areaYZ = ext.y * ext.z;
                if (areaXY >= areaXZ && areaXY >= areaYZ)
                    spinAxis = glm::vec3(0, 0, 1);
                else if (areaXZ >= areaXY && areaXZ >= areaYZ)
                    spinAxis = glm::vec3(0, 1, 0);
                else
                    spinAxis = glm::vec3(1, 0, 0);
            }

            glm::quat bq = glm::quat(glm::radians(so->getEulerRotation()));
            glm::vec3 basePos = so->getTransform().getPosition();
            m_fans.push_back({so.get(), obj, 0.0f, so->getEulerRotation(), bq, spinAxis, center, basePos});
        }

        // Add smoke emitter
        auto* particles = host.getParticleRenderer();
        if (particles) {
            glm::vec3 smokePos;
            glm::vec3 genPos = obj->getTransform().getPosition();
            if (!host.getCPWorldPos(obj, "smoke", smokePos)) {
                smokePos = genPos + glm::vec3(0.0f, 2.0f, 0.0f);
            }
            particles->addEmitter(smokePos);
        }

        host.showScreenMessage("Generator ON", 3.0f);
    }

    void turnOff(SceneObject* obj, MachineHost& host) {
        auto it = m_loops.find(obj);
        if (it == m_loops.end()) return;

        // Stop audio
        Audio::getInstance().stopLoop(it->second);
        Audio::getInstance().stopLoop(it->second + 1);
        m_loops.erase(it);

        // Stop fans
        stopFansFor(obj, host);

        // Remove smoke
        removeSmokeFor(obj, host);

        host.showScreenMessage("Generator OFF", 3.0f);
    }

    void stopFansFor(SceneObject* generator, MachineHost& host) {
        auto& attachments = host.getCPAttachments();
        for (auto fanIt = m_fans.begin(); fanIt != m_fans.end(); ) {
            if (fanIt->generator == generator) {
                fanIt->obj->setEulerRotation(fanIt->baseEuler);
                fanIt->obj->getTransform().setPosition(fanIt->basePos);
                fanIt = m_fans.erase(fanIt);
            } else {
                ++fanIt;
            }
        }
    }

    void removeSmokeFor(SceneObject* obj, MachineHost& host) {
        auto* particles = host.getParticleRenderer();
        if (!particles) return;
        glm::vec3 smokePos;
        glm::vec3 genPos = obj->getTransform().getPosition();
        if (!host.getCPWorldPos(obj, "smoke", smokePos)) {
            smokePos = genPos + glm::vec3(0.0f, 2.0f, 0.0f);
        }
        particles->removeEmitter(smokePos);
    }
};
