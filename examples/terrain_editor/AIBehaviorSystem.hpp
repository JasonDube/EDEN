#pragma once
/**
 * AIBehaviorSystem.hpp — Extracted AI NPC behavior module.
 *
 * Owns all state and logic for:
 *   - Heartbeat (passive periodic perception)
 *   - AI motor actions (look_around, turn_to, move_to, follow, pickup, place, drop, stop)
 *   - NPC follow mode (multi-NPC)
 *   - Auto-face-player for sentient NPCs
 *   - TTS (text-to-speech) request + playback
 *   - PTT / STT (push-to-talk voice → text)
 *   - Voice message dispatch (handleVoiceMessage)
 *   - Perception scan cone
 *   - Action complete callback loop
 *
 * The host (TerrainEditor, EDEN OS, etc.) implements AIBehaviorHost to wire in
 * its camera, scene objects, HTTP client, audio, UI, and scripting engine.
 */

#include "Editor/SceneObject.hpp"
#include "Network/AsyncHttpClient.hpp"
#include <eden/Camera.hpp>
#include <eden/Audio.hpp>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <functional>

// ─────────────────────────────────────────────────────────
//  Host interface — the embedding application implements this
// ─────────────────────────────────────────────────────────
struct AIBehaviorHost {
    virtual ~AIBehaviorHost() = default;

    // Scene access
    virtual std::vector<std::unique_ptr<eden::SceneObject>>& getSceneObjects() = 0;
    virtual eden::Camera& getCamera() = 0;

    // HTTP client (owned by host)
    virtual eden::AsyncHttpClient* getHttpClient() = 0;

    // Chat / UI
    virtual void addChatMessage(const std::string& sender, const std::string& message) = 0;

    // Model renderer — for expression texture updates
    virtual void updateNPCTexture(eden::SceneObject* npc) = 0;

    // Grove scripting (optional — return false if not supported)
    virtual bool evalGroveScript(const std::string& script, std::string& output, std::string& error) { return false; }

    // AlgoBot programming (optional)
    virtual void programAlgoBot(eden::SceneObject* bot) {}

    // EditorUI toggles (optional)
    virtual void setShowMindMap(bool show) {}

    // Spatial analysis update (optional)
    virtual void updateSpatialGrid(const nlohmann::json& data) {}

    // Is in play mode?
    virtual bool isPlayMode() const = 0;

    // Is in conversation?
    virtual bool isInConversation() const = 0;
    virtual eden::SceneObject* getCurrentInteractObject() const = 0;
    virtual void setCurrentInteractObject(eden::SceneObject* obj) = 0;

    // Player context — selected object and hotbar
    virtual std::string getSelectedObjectInfo() const { return ""; }
    virtual std::string getSelectedImagePath() const { return ""; }
    virtual std::string getHotbarSlotInfo(int slot) const { return ""; }
};

// ─────────────────────────────────────────────────────────
//  Data types
// ─────────────────────────────────────────────────────────

struct AIFollowState {
    eden::SceneObject* npc = nullptr;
    float distance = 4.0f;
    float speed = 5.0f;
};

// ─────────────────────────────────────────────────────────
//  AIBehaviorSystem
// ─────────────────────────────────────────────────────────
class AIBehaviorSystem {
public:
    explicit AIBehaviorSystem(AIBehaviorHost& host) : m_host(host) {}

    // ── Per-frame update (call from main loop — runs all subsystems) ──
    void update(float deltaTime) {
        tickTimers(deltaTime);
        updateHeartbeat(deltaTime);
        updateAutoFacePlayer(deltaTime);
        updateAIAction(deltaTime);
        updateAIFollow(deltaTime);
    }

    // ── Timer ticks (TTS cooldown etc.) — call even outside play mode ──
    void tickTimers(float deltaTime) {
        if (m_ttsCooldown > 0.0f) m_ttsCooldown -= deltaTime;
    }

    // ── Heartbeat: passive perception for sentient AI NPCs ──
    void updateHeartbeat(float deltaTime) {
        auto* http = m_host.getHttpClient();
        if (!m_heartbeatEnabled || !m_host.isPlayMode() || !http || !http->isConnected()
            || m_heartbeatInFlight || m_ttsInFlight || m_ttsCooldown > 0.0f
            || m_host.isInConversation())  // Skip heartbeat during active chat — saves tokens
            return;

        m_heartbeatTimer += deltaTime;
        if (m_heartbeatTimer < m_heartbeatInterval) return;
        m_heartbeatTimer = 0.0f;

        // Find first sentient AI NPC in scene
        eden::SceneObject* companion = nullptr;
        for (auto& obj : m_host.getSceneObjects()) {
            if (!obj) continue;
            auto bt = obj->getBeingType();
            if (bt == eden::BeingType::EDEN_COMPANION || bt == eden::BeingType::AI_ARCHITECT
                || bt == eden::BeingType::EVE || bt == eden::BeingType::ROBOT) {
                companion = obj.get();
                break;
            }
        }
        if (!companion) return;

        m_heartbeatInFlight = true;
        eden::PerceptionData perception = performScanCone(companion, 360.0f, 100.0f);
        std::string npcName = companion->getName();
        int beingType = static_cast<int>(companion->getBeingType());

        std::string sessionId = m_quickChatSessionIds.count(npcName)
                              ? m_quickChatSessionIds[npcName] : "";

        http->sendHeartbeat(sessionId, npcName, beingType, perception,
            [this, npcName, companion](const eden::AsyncHttpClient::Response& resp) {
                m_heartbeatInFlight = false;
                if (!resp.success) return;

                try {
                    auto json = nlohmann::json::parse(resp.body);

                    if (json.contains("session_id") && !json["session_id"].is_null())
                        m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();

                    std::string responseText;
                    if (json.contains("response") && !json["response"].is_null())
                        responseText = json["response"].get<std::string>();

                    // Parse emotion from response
                    std::string emotion = json.value("emotion", "neutral");
                    m_npcEmotions[npcName] = emotion;

                    if (!responseText.empty()) {
                        std::string chatMsg = "[" + emotion + "] " + responseText;
                        m_host.addChatMessage(npcName, chatMsg);

                        if (m_host.isInConversation() && m_host.getCurrentInteractObject() == companion) {
                            m_conversationHistory.push_back({npcName, chatMsg, false});
                            m_scrollToBottom = true;
                        }

                        std::cout << "[" << npcName << "] emotion: " << emotion << std::endl;
                        speakTTS(responseText, npcName);
                        cycleExpression(companion);
                    }

                    if (json.contains("action") && !json["action"].is_null()) {
                        auto* prev = m_host.getCurrentInteractObject();
                        m_host.setCurrentInteractObject(companion);
                        executeAIAction(json["action"]);
                        if (!m_host.isInConversation())
                            m_host.setCurrentInteractObject(prev);
                    }

                    if (json.contains("spatial_analysis") && !json["spatial_analysis"].is_null())
                        m_host.updateSpatialGrid(json["spatial_analysis"]);

                } catch (const std::exception& e) {
                    std::cerr << "[Heartbeat] Parse error: " << e.what() << std::endl;
                }
            });
    }

    // ── Auto-face player: sentient NPCs rotate to look at camera ──
    void updateAutoFacePlayer(float deltaTime) {
        glm::vec3 playerPos = m_host.getCamera().getPosition();
        for (auto& obj : m_host.getSceneObjects()) {
            if (!obj) continue;
            auto bt = obj->getBeingType();
            if (bt != eden::BeingType::EDEN_COMPANION && bt != eden::BeingType::AI_ARCHITECT
                && bt != eden::BeingType::EVE && bt != eden::BeingType::ROBOT)
                continue;

            // Skip if NPC is doing a motor action
            if (m_aiActionActive && m_host.getCurrentInteractObject() == obj.get()) continue;

            glm::vec3 npcPos = obj->getTransform().getPosition();
            glm::vec3 toPlayer = playerPos - npcPos;
            toPlayer.y = 0.0f;
            if (glm::length(toPlayer) < 0.1f) continue;

            float targetYaw = glm::degrees(atan2(toPlayer.x, toPlayer.z));
            glm::vec3 euler = obj->getEulerRotation();

            float diff = targetYaw - euler.y;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;

            float step = 90.0f * deltaTime;
            if (std::abs(diff) < step)
                euler.y = targetYaw;
            else
                euler.y += (diff > 0 ? step : -step);

            obj->setEulerRotation(euler);
        }
    }

    // ── Perception scan cone ──
    eden::PerceptionData performScanCone(eden::SceneObject* npc, float fovDegrees = 120.0f, float range = 100.0f) {
        eden::PerceptionData perception;
        if (!npc) return perception;

        glm::vec3 npcPos = npc->getTransform().getPosition();
        perception.posX = npcPos.x;
        perception.posY = npcPos.y;
        perception.posZ = npcPos.z;
        perception.fov = fovDegrees;
        perception.range = range;

        glm::vec3 camPos = m_host.getCamera().getPosition();
        perception.playerX = camPos.x;
        perception.playerY = camPos.y;
        perception.playerZ = camPos.z;

        glm::vec3 euler = npc->getEulerRotation();
        float yawRad = glm::radians(euler.y);
        glm::vec3 facing(sin(yawRad), 0.0f, cos(yawRad));
        facing = glm::normalize(facing);
        perception.facingX = facing.x;
        perception.facingY = facing.y;
        perception.facingZ = facing.z;

        float halfFov = fovDegrees * 0.5f;

        for (const auto& obj : m_host.getSceneObjects()) {
            if (!obj || !obj->isVisible()) continue;
            if (obj.get() == npc) continue;

            glm::vec3 objPos = obj->getTransform().getPosition();
            glm::vec3 toObj = objPos - npcPos;
            float dist = glm::length(toObj);
            if (dist > range || dist < 0.1f) continue;

            glm::vec3 toObjNorm = glm::normalize(toObj);
            float dotProduct = glm::dot(facing, glm::vec3(toObjNorm.x, 0, toObjNorm.z));
            dotProduct = glm::clamp(dotProduct, -1.0f, 1.0f);
            float angleDeg = glm::degrees(acos(dotProduct));
            if (angleDeg > halfFov) continue;

            glm::vec3 right(facing.z, 0, -facing.x);
            float rightDot = glm::dot(right, glm::vec3(toObjNorm.x, 0, toObjNorm.z));

            std::string bearing;
            if (angleDeg < 15.0f) bearing = "directly ahead";
            else if (angleDeg < 45.0f) bearing = rightDot > 0 ? "ahead-right" : "ahead-left";
            else bearing = rightDot > 0 ? "right" : "left";

            std::string objType;
            switch (obj->getPrimitiveType()) {
                case eden::PrimitiveType::Cube: objType = "cube"; break;
                case eden::PrimitiveType::Cylinder: objType = "cylinder"; break;
                case eden::PrimitiveType::SpawnMarker: objType = "spawn_marker"; break;
                case eden::PrimitiveType::Door: objType = "door"; break;
                default: objType = "model"; break;
            }

            eden::VisibleObject visObj;
            visObj.name = obj->getName();
            visObj.type = objType;
            visObj.distance = dist;
            visObj.angle = angleDeg;
            visObj.bearing = bearing;
            visObj.posX = objPos.x;
            visObj.posY = objPos.y;
            visObj.posZ = objPos.z;
            visObj.isSentient = obj->isSentient();
            if (visObj.isSentient)
                visObj.beingType = eden::getBeingTypeName(obj->getBeingType());
            visObj.description = obj->getDescription();

            if (obj->hasControlPoints()) {
                std::string cpInfo = "CPs: ";
                for (size_t ci = 0; ci < obj->getControlPoints().size(); ci++) {
                    if (ci > 0) cpInfo += ", ";
                    cpInfo += obj->getControlPoints()[ci].name;
                }
                if (visObj.description.empty()) visObj.description = cpInfo;
                else visObj.description += " | " + cpInfo;
            }

            perception.visibleObjects.push_back(visObj);
        }

        // Always include the player
        {
            glm::vec3 toPlayer = camPos - npcPos;
            float playerDist = glm::length(toPlayer);
            if (playerDist > 0.1f && playerDist <= range) {
                glm::vec3 toPlayerNorm = glm::normalize(toPlayer);
                float rightDot = glm::dot(glm::vec3(facing.z, 0, -facing.x),
                                           glm::vec3(toPlayerNorm.x, 0, toPlayerNorm.z));
                float dotP = glm::dot(facing, glm::vec3(toPlayerNorm.x, 0, toPlayerNorm.z));
                dotP = glm::clamp(dotP, -1.0f, 1.0f);
                float angleDeg = glm::degrees(acos(dotP));

                std::string bearing;
                if (angleDeg < 15.0f) bearing = "directly ahead";
                else if (angleDeg < 45.0f) bearing = rightDot > 0 ? "ahead-right" : "ahead-left";
                else bearing = rightDot > 0 ? "right" : "left";

                eden::VisibleObject playerObj;
                playerObj.name = "Player";
                playerObj.type = "player";
                playerObj.distance = playerDist;
                playerObj.angle = angleDeg;
                playerObj.bearing = bearing;
                playerObj.posX = camPos.x;
                playerObj.posY = camPos.y;
                playerObj.posZ = camPos.z;
                playerObj.isSentient = true;
                playerObj.beingType = "human";
                perception.visibleObjects.push_back(playerObj);
            }
        }

        std::sort(perception.visibleObjects.begin(), perception.visibleObjects.end(),
            [](const eden::VisibleObject& a, const eden::VisibleObject& b) {
                return a.distance < b.distance;
            });

        return perception;
    }

    // ── Execute an AI action from JSON ──
    void executeAIAction(const nlohmann::json& action) {
        auto* npc = m_host.getCurrentInteractObject();
        if (!npc) return;

        std::string actionType = action.value("type", "");
        float duration = action.value("duration", 2.0f);

        std::cout << "[AI Action] Type: " << actionType << ", Duration: " << duration << "s" << std::endl;

        if (actionType == "look_around") {
            m_aiActionActive = true;
            m_aiActionType = "look_around";
            m_aiActionDuration = duration;
            m_aiActionTimer = 0.0f;
            m_aiActionStartYaw = npc->getEulerRotation().y;
            std::cout << "[AI Action] Starting 360-degree scan from yaw " << m_aiActionStartYaw << std::endl;
        }
        else if (actionType == "turn_to") {
            if (action.contains("target") && action["target"].is_array()) {
                auto target = action["target"];
                glm::vec3 targetPos(target[0].get<float>(), target[1].get<float>(), target[2].get<float>());
                glm::vec3 npcPos = npc->getTransform().getPosition();
                glm::vec3 toTarget = targetPos - npcPos;
                toTarget.y = 0;
                if (glm::length(toTarget) > 0.01f) {
                    toTarget = glm::normalize(toTarget);
                    float targetYaw = glm::degrees(atan2(toTarget.x, toTarget.z));
                    m_aiActionActive = true;
                    m_aiActionType = "turn_to";
                    m_aiActionDuration = duration;
                    m_aiActionTimer = 0.0f;
                    m_aiActionStartYaw = npc->getEulerRotation().y;
                    m_aiActionTargetYaw = targetYaw;
                    std::cout << "[AI Action] Turning from " << m_aiActionStartYaw << " to " << targetYaw << std::endl;
                }
            } else if (action.contains("angle")) {
                float targetYaw = action["angle"].get<float>();
                m_aiActionActive = true;
                m_aiActionType = "turn_to";
                m_aiActionDuration = duration;
                m_aiActionTimer = 0.0f;
                m_aiActionStartYaw = npc->getEulerRotation().y;
                m_aiActionTargetYaw = targetYaw;
            }
        }
        else if (actionType == "teleport_to") {
            // Instant teleport — no walking animation
            glm::vec3 targetPos = npc->getTransform().getPosition();
            if (action.contains("target")) {
                auto& target = action["target"];
                targetPos.x = target.value("x", targetPos.x);
                targetPos.y = target.value("y", targetPos.y);
                targetPos.z = target.value("z", targetPos.z);
            } else if (action.value("to_player", false)) {
                // Teleport to player position (offset slightly so not inside them)
                glm::vec3 playerPos = m_host.getCamera().getPosition();
                glm::vec3 camFacing;
                float yaw = glm::radians(m_host.getCamera().getYaw());
                camFacing.x = sin(yaw);
                camFacing.z = cos(yaw);
                targetPos = playerPos + glm::vec3(camFacing.x * 3.0f, 0.0f, camFacing.z * 3.0f);
            }
            npc->getTransform().setPosition(targetPos);
            // Face the player
            glm::vec3 toPlayer = m_host.getCamera().getPosition() - targetPos;
            float faceYaw = glm::degrees(atan2(toPlayer.x, toPlayer.z));
            glm::vec3 euler = npc->getEulerRotation();
            euler.y = faceYaw;
            npc->setEulerRotation(euler);
            std::cout << "[AI Action] Teleported " << npc->getName() << " to ("
                      << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")" << std::endl;
        }
        else if (actionType == "move_to") {
            if (action.contains("target")) {
                auto& target = action["target"];
                float x = target.value("x", 0.0f);
                float y = target.value("y", npc->getTransform().getPosition().y);
                float z = target.value("z", 0.0f);
                float speed = action.value("speed", 5.0f);

                m_aiActionStartPos = npc->getTransform().getPosition();
                m_aiActionTargetPos = glm::vec3(x, y, z);
                m_aiActionSpeed = speed;

                float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                m_aiActionDuration = distance / speed;

                if (m_aiActionDuration > 0.01f) {
                    m_aiActionActive = true;
                    m_aiActionType = "move_to";
                    m_aiActionTimer = 0.0f;

                    glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                    m_aiActionStartYaw = npc->getEulerRotation().y;

                    std::cout << "[AI Action] Moving from (" << m_aiActionStartPos.x << ", " << m_aiActionStartPos.z
                              << ") to (" << x << ", " << z << ") at speed " << speed
                              << " (ETA: " << m_aiActionDuration << "s)" << std::endl;
                }
            }
        }
        else if (actionType == "follow") {
            float dist = action.value("distance", 4.0f);
            float spd = action.value("speed", 5.0f);
            bool found = false;
            for (auto& fs : m_aiFollowers) {
                if (fs.npc == npc) { fs.distance = dist; fs.speed = spd; found = true; break; }
            }
            if (!found) m_aiFollowers.push_back({npc, dist, spd});
            std::cout << "[AI Action] Follow mode activated for " << npc->getName()
                      << " (distance: " << dist << ", speed: " << spd
                      << ", total followers: " << m_aiFollowers.size() << ")" << std::endl;
        }
        else if (actionType == "pickup") {
            std::string targetName = action.value("target", "");
            if (targetName.empty()) {
                std::cout << "[AI Action] pickup: no target specified" << std::endl;
            } else if (npc->isCarrying()) {
                std::cout << "[AI Action] pickup: already carrying " << npc->getCarriedItemName() << std::endl;
            } else {
                eden::SceneObject* target = nullptr;
                for (auto& obj : m_host.getSceneObjects()) {
                    if (obj && obj->getName() == targetName && obj->isVisible()) { target = obj.get(); break; }
                }
                if (!target) {
                    std::cout << "[AI Action] pickup: target '" << targetName << "' not found" << std::endl;
                } else {
                    glm::vec3 targetPos = target->getTransform().getPosition();
                    m_aiActionStartPos = npc->getTransform().getPosition();
                    m_aiActionTargetPos = targetPos;
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;
                    m_aiActionSpeed = 5.0f;

                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / m_aiActionSpeed;

                    if (m_aiActionDuration > 0.01f) {
                        m_aiActionActive = true;
                        m_aiActionType = "pickup";
                        m_aiActionTimer = 0.0f;

                        glm::vec3 dir = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(dir.x, dir.z));
                        m_aiActionStartYaw = npc->getEulerRotation().y;

                        m_aiPickupTarget = target;
                        m_aiPickupTargetName = targetName;
                    } else {
                        // Already at target — pick up immediately
                        target->setVisible(false);
                        npc->setCarriedItem(targetName, target);
                        std::cout << "[AI Action] Picked up '" << targetName << "'" << std::endl;
                    }
                }
            }
        }
        else if (actionType == "drop") {
            if (!npc->isCarrying()) {
                std::cout << "[AI Action] drop: not carrying anything" << std::endl;
            } else {
                eden::SceneObject* carried = npc->getCarriedItemObject();
                if (carried) {
                    glm::vec3 npcPos = npc->getTransform().getPosition();
                    glm::vec3 fwd = npc->getEulerRotation();
                    float yRad = glm::radians(fwd.y);
                    glm::vec3 dropPos = npcPos + glm::vec3(sin(yRad), 0, cos(yRad)) * 2.0f;
                    carried->getTransform().setPosition(dropPos);
                    carried->setVisible(true);
                    std::string itemName = npc->getCarriedItemName();
                    npc->clearCarriedItem();
                    std::cout << "[AI Action] Dropped '" << itemName << "'" << std::endl;
                }
            }
        }
        else if (actionType == "place") {
            std::string targetName = action.value("target", "");
            if (targetName.empty() || !npc->isCarrying()) {
                std::cout << "[AI Action] place: missing target or not carrying" << std::endl;
            } else {
                eden::SceneObject* target = nullptr;
                for (auto& obj : m_host.getSceneObjects()) {
                    if (obj && obj->getName() == targetName && obj->isVisible()) { target = obj.get(); break; }
                }
                if (!target) {
                    std::cout << "[AI Action] place: target '" << targetName << "' not found" << std::endl;
                } else {
                    glm::vec3 targetPos = target->getTransform().getPosition();
                    m_aiActionStartPos = npc->getTransform().getPosition();
                    m_aiActionTargetPos = targetPos;
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;
                    m_aiActionSpeed = 5.0f;

                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / m_aiActionSpeed;

                    if (m_aiActionDuration > 0.01f) {
                        m_aiActionActive = true;
                        m_aiActionType = "place";
                        m_aiActionTimer = 0.0f;

                        glm::vec3 dir = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(dir.x, dir.z));
                        m_aiActionStartYaw = npc->getEulerRotation().y;

                        m_aiPlaceTarget = target;
                        m_aiPlaceTargetName = targetName;
                    } else {
                        placeCarriedItemAt(npc, target);
                    }
                }
            }
        }
        else if (actionType == "run_script") {
            std::string script = action.value("script", "");
            if (script.empty()) {
                std::cout << "[AI Action] run_script: no script provided" << std::endl;
            } else {
                std::cout << "[AI Action] run_script: executing " << script.size() << " bytes" << std::endl;
                std::string output, error;
                bool ok = m_host.evalGroveScript(script, output, error);
                if (!ok && !error.empty()) {
                    std::cout << "[AI Action] " << error << std::endl;
                    m_host.addChatMessage("System", error);
                } else if (!output.empty()) {
                    while (!output.empty() && output.back() == '\n') output.pop_back();
                    m_host.addChatMessage(npc ? npc->getName() : "System", output);
                }
            }
        }
        else if (actionType == "program_bot") {
            std::string targetName = action.value("target", "");
            std::string script = action.value("script", "");
            if (targetName.empty() || script.empty()) {
                std::cout << "[AI Action] program_bot: missing target or script" << std::endl;
            } else {
                eden::SceneObject* targetBot = nullptr;
                for (auto& obj : m_host.getSceneObjects()) {
                    if (obj && obj->getName() == targetName) { targetBot = obj.get(); break; }
                }
                if (!targetBot) {
                    std::cout << "[AI Action] program_bot: target '" << targetName << "' not found" << std::endl;
                } else {
                    std::string output, error;
                    bool ok = m_host.evalGroveScript(script, output, error);
                    if (!ok && !error.empty()) {
                        std::cout << "[AI Action] " << error << std::endl;
                        m_host.addChatMessage("System", error);
                    } else {
                        m_host.programAlgoBot(targetBot);
                    }
                }
            }
        }
        else if (actionType == "stop") {
            m_aiFollowers.erase(
                std::remove_if(m_aiFollowers.begin(), m_aiFollowers.end(),
                    [npc](const AIFollowState& fs) { return fs.npc == npc; }),
                m_aiFollowers.end());
            m_aiActionActive = false;
            std::cout << "[AI Action] Stopped for " << npc->getName()
                      << " (remaining followers: " << m_aiFollowers.size() << ")" << std::endl;
        }
        else if (actionType == "set_expression") {
            std::string exprName = action.value("expression", "");
            if (exprName.empty() || npc->getExpressionCount() == 0) {
                std::cout << "[AI Action] set_expression: invalid" << std::endl;
            } else if (npc->setExpressionByName(exprName)) {
                m_host.updateNPCTexture(npc);
                std::cout << "[AI Action] Expression changed to '" << exprName << "'" << std::endl;
            }
        }
        else if (actionType == "show_mind_map") {
            m_host.setShowMindMap(true);
        }
        else if (actionType == "hide_mind_map") {
            m_host.setShowMindMap(false);
        }
        else if (actionType == "read_file") {
            std::string targetName = action.value("target", "");
            if (targetName.empty()) {
                std::cout << "[AI Action] read_file: no target specified" << std::endl;
            } else {
                // Find the FSFile scene object
                eden::SceneObject* target = nullptr;
                for (auto& obj : m_host.getSceneObjects()) {
                    if (obj && obj->getName() == targetName && obj->isVisible()) { target = obj.get(); break; }
                }
                if (!target) {
                    std::cout << "[AI Action] read_file: '" << targetName << "' not found" << std::endl;
                } else {
                    // Extract real path from targetLevel (strip "fs://" prefix)
                    std::string tl = target->getTargetLevel();
                    std::string realPath = tl;
                    if (tl.rfind("fs://", 0) == 0) realPath = tl.substr(5);

                    if (realPath.empty()) {
                        std::cout << "[AI Action] read_file: no file path for '" << targetName << "'" << std::endl;
                    } else {
                        // Read up to 500 chars preview
                        std::ifstream file(realPath);
                        std::string preview;
                        if (file.is_open()) {
                            preview.resize(500);
                            file.read(&preview[0], 500);
                            preview.resize(file.gcount());
                            if (file.gcount() == 500) preview += "\n... (truncated)";
                            file.close();
                        } else {
                            preview = "(cannot read file — binary or permission denied)";
                        }

                        std::cout << "[AI Action] read_file: '" << realPath << "' (" << preview.size() << " chars)" << std::endl;

                        // Feed the file contents back as a system message in the conversation
                        auto* http = m_host.getHttpClient();
                        std::string sessionId = m_quickChatSessionIds.count(npc->getName())
                            ? m_quickChatSessionIds[npc->getName()] : "";
                        if (http && !sessionId.empty()) {
                            std::string fileMsg = "[File contents of " + realPath + "]\n" + preview;
                            http->sendChatMessage(sessionId, fileMsg,
                                npc->getName(), "", static_cast<int>(npc->getBeingType()),
                                [this, npcName = npc->getName()](const eden::AsyncHttpClient::Response& resp) {
                                    if (resp.success) {
                                        try {
                                            auto json = nlohmann::json::parse(resp.body);
                                            std::string response = json.value("response", "...");
                                            std::string emotion = json.value("emotion", "neutral");
                                            std::string model = json.value("model", "?");
                                            m_npcEmotions[npcName] = emotion;
                                            m_host.addChatMessage(npcName, "[" + emotion + "] " + response);
                                            std::cout << "[" << npcName << "] (" << emotion << ") [" << model << "] " << response << std::endl;
                                            speakTTS(response, npcName);
                                        } catch (...) {}
                                    }
                                });
                        }
                    }
                }
            }
        }
        else {
            std::cout << "[AI Action] Unknown action type: '" << actionType << "'" << std::endl;
        }
    }

    // ── Handle voice (PTT) message — find nearest NPC, send chat with perception ──
    void handleVoiceMessage(const std::string& text) {
        auto* http = m_host.getHttpClient();
        if (!http) return;

        glm::vec3 playerPos = m_host.getCamera().getPosition();

        eden::SceneObject* nearestNPC = nullptr;
        float nearestDist = 100.0f;
        for (auto& obj : m_host.getSceneObjects()) {
            if (!obj || !obj->isVisible() || !obj->isSentient()) continue;
            // skip player avatar
            float dist = glm::length(obj->getTransform().getPosition() - playerPos);
            if (dist < nearestDist) { nearestDist = dist; nearestNPC = obj.get(); }
        }

        if (!nearestNPC) {
            m_host.addChatMessage("System", "No one nearby to hear you.");
            return;
        }

        m_host.addChatMessage("You", text);

        std::string npcName = nearestNPC->getName();
        int beingType = static_cast<int>(nearestNPC->getBeingType());
        m_host.setCurrentInteractObject(nearestNPC);

        std::string sessionId;
        auto it = m_quickChatSessionIds.find(npcName);
        if (it != m_quickChatSessionIds.end()) sessionId = it->second;

        // Enrich message with player context when they reference selection or hotbar
        std::string enrichedText = text;
        std::string lowerText = text;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

        // "look at what I selected" / "what did I select" / "my selection"
        if (lowerText.find("select") != std::string::npos || lowerText.find("holding") != std::string::npos) {
            std::string selInfo = m_host.getSelectedObjectInfo();
            if (!selInfo.empty())
                enrichedText += "\n" + selInfo;
        }

        // Check if player wants image description — send image to vision model
        std::string imagePath;
        if (lowerText.find("describe") != std::string::npos || lowerText.find("image") != std::string::npos ||
            lowerText.find("picture") != std::string::npos || lowerText.find("photo") != std::string::npos ||
            lowerText.find("look at") != std::string::npos || lowerText.find("what is") != std::string::npos ||
            lowerText.find("what do you see") != std::string::npos) {
            imagePath = m_host.getSelectedImagePath();
            if (!imagePath.empty())
                std::cout << "[AI] Sending image to vision model: " << imagePath << std::endl;
        }

        eden::PerceptionData perception = performScanCone(nearestNPC, 120.0f, 100.0f);
        auto responseHandler = [this, npcName, nearestNPC](const eden::AsyncHttpClient::Response& resp) {
                if (resp.success) {
                    try {
                        auto json = nlohmann::json::parse(resp.body);
                        if (json.contains("session_id"))
                            m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                        std::string response = json.value("response", "...");
                        std::string emotion = json.value("emotion", "neutral");
                        m_npcEmotions[npcName] = emotion;
                        std::cout << "[" << npcName << "] (" << emotion << ") " << response << std::endl;
                        m_host.addChatMessage(npcName, "[" + emotion + "] " + response);
                        speakTTS(response, npcName);
                        m_host.setCurrentInteractObject(nearestNPC);
                        if (json.contains("action") && !json["action"].is_null())
                            executeAIAction(json["action"]);
                    } catch (...) {
                        m_host.addChatMessage(npcName, "...");
                    }
                } else {
                    std::cout << "[" << npcName << "] (No response)" << std::endl;
                    m_host.addChatMessage(npcName, "(No response)");
                }
            };

        if (!imagePath.empty()) {
            http->sendChatMessageWithPerception(sessionId, enrichedText,
                npcName, "", beingType, perception, imagePath, responseHandler);
        } else {
            http->sendChatMessageWithPerception(sessionId, enrichedText,
                npcName, "", beingType, perception, responseHandler);
        }
    }

    // ── TTS: request speech and play via Audio ──
    void speakTTS(const std::string& text, const std::string& npcName) {
        if (!m_ttsEnabled) return;
        auto* http = m_host.getHttpClient();
        if (!http || text.empty()) return;

        if (m_ttsInFlight || m_ttsCooldown > 0.0f) return;

        // Voice selection
        std::string voice = "en-US-AvaNeural";  // default (Liora)
        bool robot = false;
        if (npcName.find("Eve") != std::string::npos) voice = "en-GB-SoniaNeural";
        else if (npcName.find("Xenk") != std::string::npos) voice = "en-US-GuyNeural";
        else if (npcName.find("Robot") != std::string::npos) { voice = "en-US-GuyNeural"; robot = true; }

        std::string truncated = text.substr(0, 500);  // TTS length limit
        std::cout << "[TTS] Requesting: \"" << truncated.substr(0, 60) << "...\" (" << voice << ")" << std::endl;

        m_ttsInFlight = true;
        http->requestTTS(truncated, voice,
            [this](const eden::AsyncHttpClient::Response& resp) {
                m_ttsInFlight = false;
                if (!resp.success || resp.body.empty()) return;

                // Determine file type from response
                bool isWav = resp.body.size() > 4 && resp.body[0] == 'R' && resp.body[1] == 'I'
                             && resp.body[2] == 'F' && resp.body[3] == 'F';
                std::string ext = isWav ? ".wav" : ".mp3";

                float estimatedDuration = isWav
                    ? static_cast<float>(resp.body.size()) / 32000.0f
                    : static_cast<float>(resp.body.size()) / 16000.0f;

                std::string tempPath = "/tmp/eden_tts_" + std::to_string(m_ttsFileCounter++) + ext;

                {
                    std::ofstream out(tempPath, std::ios::binary);
                    out.write(resp.body.data(), resp.body.size());
                }

                // Delete previous TTS file
                if (!m_lastTTSFile.empty()) std::remove(m_lastTTSFile.c_str());
                m_lastTTSFile = tempPath;

                std::cout << "[TTS] Playing: " << tempPath << " (~" << estimatedDuration << "s)" << std::endl;
                eden::Audio::getInstance().playSound(tempPath, 0.8f);
                m_ttsCooldown = estimatedDuration + 0.5f;
            }, "", robot);
    }

    // ── Cycle NPC expression ──
    void cycleExpression(eden::SceneObject* npc) {
        if (!npc || npc->getExpressionCount() == 0) return;
        int next = (npc->getCurrentExpression() + 1) % npc->getExpressionCount();
        if (npc->setExpression(next))
            m_host.updateNPCTexture(npc);
    }

    // ── Send completion callback after move_to finishes ──
    void sendActionCompleteCallback(eden::SceneObject* npc, const std::string& actionType, float x, float z) {
        auto* http = m_host.getHttpClient();
        if (!npc || !http) return;

        std::string npcName = npc->getName();
        int beingType = static_cast<int>(npc->getBeingType());

        std::string sessionId;
        auto it = m_quickChatSessionIds.find(npcName);
        if (it != m_quickChatSessionIds.end()) sessionId = it->second;
        if (sessionId.empty()) return;

        eden::PerceptionData perception = performScanCone(npc, 360.0f, 100.0f);

        std::string msg = "[ACTION COMPLETE] " + actionType + " finished at ("
                        + std::to_string(x) + ", " + std::to_string(z)
                        + "). If you have a pending task (e.g. a return trip), "
                          "issue the next action now. If not, simply acknowledge.";

        http->sendChatMessageWithPerception(sessionId, msg,
            npcName, "", beingType, perception,
            [this, npcName, npc](const eden::AsyncHttpClient::Response& resp) {
                if (!resp.success) return;
                try {
                    auto json = nlohmann::json::parse(resp.body);
                    if (json.contains("session_id"))
                        m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                    std::string response = json.value("response", "");
                    std::string emotion = json.value("emotion", "neutral");
                    m_npcEmotions[npcName] = emotion;
                    if (!response.empty()) {
                        m_host.addChatMessage(npcName, response);
                        speakTTS(response, npcName);
                    }
                    if (json.contains("action") && !json["action"].is_null()) {
                        m_host.setCurrentInteractObject(npc);
                        executeAIAction(json["action"]);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[ActionComplete] Parse error: " << e.what() << std::endl;
                }
            });
    }

    // ── Place carried item vertically into a target ──
    void placeCarriedItemAt(eden::SceneObject* npc, eden::SceneObject* target) {
        eden::SceneObject* carried = npc->getCarriedItemObject();
        if (!carried) return;

        eden::AABB localBounds = carried->getLocalBounds();
        glm::vec3 localSize = localBounds.getSize();
        if (localSize.x <= 0 && localSize.y <= 0 && localSize.z <= 0 && carried->hasMeshData()) {
            const auto& verts = carried->getVertices();
            glm::vec3 vmin(INFINITY), vmax(-INFINITY);
            for (const auto& v : verts) {
                vmin = glm::min(vmin, v.position);
                vmax = glm::max(vmax, v.position);
            }
            localBounds = {vmin, vmax};
            carried->setLocalBounds(localBounds);
            localSize = localBounds.getSize();
        }

        glm::vec3 scale = carried->getTransform().getScale();
        glm::vec3 scaledSize = localSize * glm::abs(scale);

        int longestAxis = 0;
        float longestLen = scaledSize[0];
        for (int i = 1; i < 3; i++) {
            if (scaledSize[i] > longestLen) { longestLen = scaledSize[i]; longestAxis = i; }
        }

        glm::vec3 rotation(0.0f);
        if (longestAxis == 0) rotation.z = 90.0f;
        else if (longestAxis == 2) rotation.x = 90.0f;

        glm::vec3 postholePos = target->getTransform().getPosition();
        eden::AABB postholeBounds = target->getWorldBounds();
        float postholeBottom = postholeBounds.min.y;
        float timberHalfLength = longestLen * 0.5f;
        glm::vec3 placePos(postholePos.x, postholeBottom + timberHalfLength, postholePos.z);

        carried->setVisible(true);
        carried->setEulerRotation(rotation);
        carried->getTransform().setPosition(placePos);

        std::string itemName = npc->getCarriedItemName();
        npc->clearCarriedItem();

        std::cout << "[AI Action] Placed '" << itemName << "' in '" << target->getName() << "'" << std::endl;
    }

    // ── Public accessors (for main.cpp to wire into existing code paths) ──
    bool isActionActive() const { return m_aiActionActive; }
    const std::string& actionType() const { return m_aiActionType; }
    std::vector<AIFollowState>& followers() { return m_aiFollowers; }
    std::unordered_map<std::string, std::string>& sessionIds() { return m_quickChatSessionIds; }

    // NPC emotion state
    std::unordered_map<std::string, std::string>& npcEmotions() { return m_npcEmotions; }
    std::string getEmotion(const std::string& npcName) const {
        auto it = m_npcEmotions.find(npcName);
        return it != m_npcEmotions.end() ? it->second : "neutral";
    }

    // Conversation history (moved from TerrainEditor)
    struct ChatMessage {
        std::string sender;
        std::string text;
        bool isPlayer;
    };
    std::vector<ChatMessage>& conversationHistory() { return m_conversationHistory; }
    bool& scrollToBottom() { return m_scrollToBottom; }

    // PTT state
    bool& pttRecording() { return m_pttRecording; }
    bool& pttProcessing() { return m_pttProcessing; }

    // TTS state
    bool& ttsEnabled() { return m_ttsEnabled; }
    bool isTTSInFlight() const { return m_ttsInFlight; }
    float ttsCooldown() const { return m_ttsCooldown; }

    // Heartbeat config
    bool& heartbeatEnabled() { return m_heartbeatEnabled; }
    float& heartbeatInterval() { return m_heartbeatInterval; }
    void resetHeartbeat() { m_heartbeatTimer = 0.0f; m_heartbeatInFlight = false; }

    // Reset all state — call when clearing the level to avoid dangling pointers
    void resetAll() {
        m_aiFollowers.clear();
        m_aiActionActive = false;
        m_aiPickupTarget = nullptr;
        m_aiPickupTargetName.clear();
        m_aiPlaceTarget = nullptr;
        m_aiPlaceTargetName.clear();
        m_hasFullScanResult = false;
        m_quickChatSessionIds.clear();
        m_npcEmotions.clear();
        m_conversationHistory.clear();
        m_waitingForAIResponse = false;
        m_ttsInFlight = false;
        m_ttsCooldown = 0.0f;
        resetHeartbeat();
    }

    // Full scan result (shared with quick chat)
    eden::PerceptionData& lastFullScanResult() { return m_lastFullScanResult; }
    bool& hasFullScanResult() { return m_hasFullScanResult; }

    // Waiting state
    bool& waitingForAIResponse() { return m_waitingForAIResponse; }

    // ── Update active AI action (per-frame) ──
    void updateAIAction(float deltaTime) {
        auto* npc = m_host.getCurrentInteractObject();
        if (!m_aiActionActive || !npc) return;

        m_aiActionTimer += deltaTime;
        float t = std::min(m_aiActionTimer / m_aiActionDuration, 1.0f);
        float easedT = t * t * (3.0f - 2.0f * t);

        if (m_aiActionType == "look_around") {
            float currentYaw = m_aiActionStartYaw + easedT * 360.0f;
            glm::vec3 euler = npc->getEulerRotation();
            euler.y = currentYaw;
            npc->setEulerRotation(euler);

            if (t >= 1.0f) {
                m_aiActionActive = false;
                std::cout << "[AI Action] look_around complete" << std::endl;
                m_lastFullScanResult = performScanCone(npc, 360.0f, 100.0f);
                m_hasFullScanResult = true;
                std::cout << "[AI Action] Full scan found " << m_lastFullScanResult.visibleObjects.size() << " objects" << std::endl;
            }
        }
        else if (m_aiActionType == "turn_to") {
            float currentYaw = glm::mix(m_aiActionStartYaw, m_aiActionTargetYaw, easedT);
            glm::vec3 euler = npc->getEulerRotation();
            euler.y = currentYaw;
            npc->setEulerRotation(euler);
            if (t >= 1.0f) { m_aiActionActive = false; std::cout << "[AI Action] turn_to complete" << std::endl; }
        }
        else if (m_aiActionType == "move_to") {
            updateMoveAction(npc, t, false);
        }
        else if (m_aiActionType == "pickup") {
            updateMoveAction(npc, t, false);
            if (t >= 1.0f && m_aiPickupTarget && !npc->isCarrying()) {
                m_aiPickupTarget->setVisible(false);
                npc->setCarriedItem(m_aiPickupTargetName, m_aiPickupTarget);
                std::cout << "[AI Action] Picked up '" << m_aiPickupTargetName << "'" << std::endl;
                m_aiPickupTarget = nullptr;
                m_aiPickupTargetName.clear();
            }
        }
        else if (m_aiActionType == "place") {
            updateMoveAction(npc, t, false);
            if (t >= 1.0f && m_aiPlaceTarget && npc->isCarrying()) {
                placeCarriedItemAt(npc, m_aiPlaceTarget);
                m_aiPlaceTarget = nullptr;
                m_aiPlaceTargetName.clear();
            }
        }
    }

    // Shared move/turn logic for move_to, pickup, place
    void updateMoveAction(eden::SceneObject* npc, float t, bool skipComplete = false) {
        const float turnPhase = 0.15f;

        if (t < turnPhase) {
            float turnT = t / turnPhase;
            float turnEased = turnT * turnT * (3.0f - 2.0f * turnT);
            float currentYaw = glm::mix(m_aiActionStartYaw, m_aiActionTargetYaw, turnEased);
            glm::vec3 euler = npc->getEulerRotation();
            euler.y = currentYaw;
            npc->setEulerRotation(euler);
        } else {
            float moveT = (t - turnPhase) / (1.0f - turnPhase);
            glm::vec3 currentPos = glm::mix(m_aiActionStartPos, m_aiActionTargetPos, moveT);
            npc->getTransform().setPosition(currentPos);
            glm::vec3 euler = npc->getEulerRotation();
            euler.y = m_aiActionTargetYaw;
            npc->setEulerRotation(euler);
        }

        if (t >= 1.0f) {
            npc->getTransform().setPosition(m_aiActionTargetPos);
            m_aiActionActive = false;

            if (m_aiActionType == "move_to") {
                std::cout << "[AI Action] move_to complete at ("
                          << m_aiActionTargetPos.x << ", " << m_aiActionTargetPos.z << ")" << std::endl;
                sendActionCompleteCallback(npc, "move_to", m_aiActionTargetPos.x, m_aiActionTargetPos.z);
            }
        }
    }

    // ── Update follower NPCs (per-frame) ──
    void updateAIFollow(float deltaTime) {
        if (m_aiFollowers.empty()) return;

        static int followDebugCount = 0;
        if (followDebugCount++ % 120 == 0)
            std::cout << "[AI Follow] " << m_aiFollowers.size() << " NPC(s) following" << std::endl;

        glm::vec3 playerPos = m_host.getCamera().getPosition();
        float yawRad = glm::radians(m_host.getCamera().getYaw());
        glm::vec3 camDir(sin(yawRad), 0.0f, cos(yawRad));
        glm::vec3 camRight(camDir.z, 0.0f, -camDir.x);

        for (size_t i = 0; i < m_aiFollowers.size(); i++) {
            auto& fs = m_aiFollowers[i];
            if (!fs.npc) continue;

            glm::vec3 npcPlayerPos = playerPos;
            npcPlayerPos.y = fs.npc->getTransform().getPosition().y;

            glm::vec3 lateralOffset(0.0f);
            if (m_aiFollowers.size() > 1) {
                float spread = 2.5f;
                float side = (i % 2 == 0) ? -1.0f : 1.0f;
                float idx = static_cast<float>((i + 1) / 2);
                lateralOffset = camRight * side * idx * spread;
            }

            glm::vec3 targetPos = npcPlayerPos - camDir * fs.distance + lateralOffset;
            glm::vec3 npcPos = fs.npc->getTransform().getPosition();
            glm::vec3 toTarget = targetPos - npcPos;
            toTarget.y = 0;
            float dist = glm::length(toTarget);

            if (dist > 1.0f) {
                glm::vec3 moveDir = glm::normalize(toTarget);
                float moveAmount = std::min(fs.speed * deltaTime, dist);
                fs.npc->getTransform().setPosition(npcPos + moveDir * moveAmount);

                float targetYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
                glm::vec3 euler = fs.npc->getEulerRotation();
                float yawDiff = targetYaw - euler.y;
                while (yawDiff > 180.0f) yawDiff -= 360.0f;
                while (yawDiff < -180.0f) yawDiff += 360.0f;
                euler.y += yawDiff * std::min(deltaTime * 8.0f, 1.0f);
                fs.npc->setEulerRotation(euler);
            } else {
                glm::vec3 euler = fs.npc->getEulerRotation();
                float targetYaw = glm::degrees(atan2(camDir.x, camDir.z));
                float yawDiff = targetYaw - euler.y;
                while (yawDiff > 180.0f) yawDiff -= 360.0f;
                while (yawDiff < -180.0f) yawDiff += 360.0f;
                euler.y += yawDiff * std::min(deltaTime * 4.0f, 1.0f);
                fs.npc->setEulerRotation(euler);
            }
        }
    }

private:
    // ── Host reference ──
    AIBehaviorHost& m_host;

    // ── Heartbeat state ──
    float m_heartbeatTimer = 0.0f;
    float m_heartbeatInterval = 15.0f;
    bool m_heartbeatEnabled = true;
    bool m_heartbeatInFlight = false;

    // ── TTS state ──
    int m_ttsFileCounter = 0;
    std::string m_lastTTSFile;
    float m_ttsCooldown = 0.0f;
    bool m_ttsEnabled = false;   // TTS off by default — toggle in UI
    bool m_ttsInFlight = false;

    // ── PTT state ──
    bool m_pttRecording = false;
    bool m_pttProcessing = false;

    // ── AI action state ──
    bool m_aiActionActive = false;
    std::string m_aiActionType;
    float m_aiActionDuration = 2.0f;
    float m_aiActionTimer = 0.0f;
    float m_aiActionStartYaw = 0.0f;
    float m_aiActionTargetYaw = 0.0f;
    glm::vec3 m_aiActionStartPos{0.0f};
    glm::vec3 m_aiActionTargetPos{0.0f};
    float m_aiActionSpeed = 5.0f;

    // Pickup/place targets
    eden::SceneObject* m_aiPickupTarget = nullptr;
    std::string m_aiPickupTargetName;
    eden::SceneObject* m_aiPlaceTarget = nullptr;
    std::string m_aiPlaceTargetName;

    // ── Follow mode ──
    std::vector<AIFollowState> m_aiFollowers;

    // ── Perception cache ──
    eden::PerceptionData m_lastFullScanResult;
    bool m_hasFullScanResult = false;

    // ── Session tracking ──
    std::unordered_map<std::string, std::string> m_quickChatSessionIds;

    // ── NPC emotions (from LLM response tags) ──
    std::unordered_map<std::string, std::string> m_npcEmotions;

    // ── Conversation state ──
    std::vector<ChatMessage> m_conversationHistory;
    bool m_scrollToBottom = false;
    bool m_waitingForAIResponse = false;
};
