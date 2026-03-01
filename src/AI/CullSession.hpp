#pragma once

#include "CullRobot.hpp"
#include "../Editor/SceneObject.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <string>
#include <vector>
#include <memory>

namespace eden {

class CullSession {
public:
    CullSession() = default;

    void init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
              ModelRenderer* renderer,
              const glm::vec3& siloCenter, float baseY);

    void spawnRobots(const glm::vec3& center, float baseY,
                     ModelRenderer* renderer,
                     const std::string& modelPath = "");
    void despawn();

    void startCulling();
    void update(float deltaTime);

    // Input / UI
    void handleInput();
    void renderDecisionUI();

    bool isActive() const { return m_active; }
    bool isSessionComplete() const { return m_sessionComplete; }

    // Robot access for E-key interaction
    CullRobot* getRobotAt(SceneObject* obj);
    bool hasRobots() const { return m_robotCount > 0; }

private:
    void scanForTargets();
    void assignNextTarget();
    void spawnDoors(const glm::vec3& center, float baseY);
    void despawnDoors();
    void openDoors();
    void closeDoors();

    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;
    ModelRenderer* m_renderer = nullptr;

    static constexpr int MAX_ROBOTS = 3;
    CullRobot m_robots[MAX_ROBOTS];
    int m_robotCount = 0;

    std::vector<CullTarget> m_allTargets;
    int m_nextTargetIdx = 0;

    // Sliding doors over the basement hole
    SceneObject* m_doorLeft = nullptr;
    SceneObject* m_doorRight = nullptr;
    glm::vec3 m_doorClosedLeft{0.0f};
    glm::vec3 m_doorClosedRight{0.0f};
    glm::vec3 m_doorOpenLeft{0.0f};
    glm::vec3 m_doorOpenRight{0.0f};
    bool m_doorsOpen = false;
    float m_doorTimer = 0.0f;
    bool m_doorAnimating = false;

    glm::vec3 m_presentPos{0.0f};
    glm::vec3 m_siloCenter{0.0f};
    float m_baseY = 0.0f;

    bool m_active = false;
    bool m_sessionComplete = false;

    // Track which robot is currently presenting (for sequential pipeline)
    int m_presentingRobotIdx = -1;

    // Track door close pending after reject
    bool m_pendingDoorClose = false;
    float m_doorCloseDelay = 0.0f;

    static constexpr float DOOR_ANIM_DURATION = 0.4f;
    static constexpr float DOOR_WIDTH = 4.0f;
    static constexpr float DOOR_DEPTH = 8.0f;
    static constexpr float DOOR_THICKNESS = 0.15f;
};

} // namespace eden
