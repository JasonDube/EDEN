#pragma once

#include "../Editor/SceneObject.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace eden {

enum class CullRobotState {
    IDLE,
    WALKING_TO_WALL,
    PICKING_UP,
    WALKING_TO_PRESENT,
    PRESENTING,
    RETURNING,
    DROPPING
};

struct CullTarget {
    std::string sourcePath;   // disk path from "fs://..."
    std::string objName;      // SceneObject name
    glm::vec3   wallPos;      // original position (to restore on accept)
    glm::vec3   wallScale;    // original scale
    float       wallYaw;      // original rotation
};

class CullRobot {
public:
    CullRobot() = default;

    void init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
              ModelRenderer* renderer);

    void spawn(const glm::vec3& homePos, ModelRenderer* renderer,
               const std::string& modelPath = "");
    void despawn();

    void assignTarget(const CullTarget& target);
    void update(float deltaTime);

    void acceptItem();
    void rejectItem();

    bool isPresenting() const { return m_state == CullRobotState::PRESENTING; }
    bool isIdle() const { return m_state == CullRobotState::IDLE; }
    bool isSpawned() const { return m_spawned; }
    SceneObject* getSceneObject() const { return m_sceneObject; }
    const CullTarget& getCurrentTarget() const { return m_currentTarget; }

    void setPresentPos(const glm::vec3& pos) { m_presentPos = pos; }

    // Interaction menu
    void showMenu() { m_showMenu = true; }
    bool isMenuOpen() const { return m_showMenu; }
    bool renderMenuUI();

private:
    void setCarriedItemPosition();
    void moveToTrash(const std::string& sourcePath);
    SceneObject* findSceneObject(const std::string& objName);
    void removeSceneObject(const std::string& objName);

    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;
    ModelRenderer* m_renderer = nullptr;

    SceneObject* m_sceneObject = nullptr;
    bool m_spawned = false;

    CullRobotState m_state = CullRobotState::IDLE;
    float m_stateTimer = 0.0f;

    glm::vec3 m_homePos{0.0f};
    glm::vec3 m_presentPos{0.0f};
    CullTarget m_currentTarget;
    SceneObject* m_carriedObject = nullptr;

    bool m_showMenu = false;

    static constexpr float PICKUP_DURATION = 0.5f;
    static constexpr float MOVE_SPEED = 4.0f;
    static constexpr float CARRY_Y_OFFSET = 2.0f;
};

} // namespace eden
