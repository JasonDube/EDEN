#include "CullSession.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include <imgui.h>
#include <eden/Input.hpp>
#include <algorithm>
#include <iostream>

namespace eden {

// ── Init ────────────────────────────────────────────────────────────────

void CullSession::init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                       ModelRenderer* renderer,
                       const glm::vec3& siloCenter, float baseY) {
    m_sceneObjects = sceneObjects;
    m_renderer = renderer;
    m_siloCenter = siloCenter;
    m_baseY = baseY;
    // Present position: above the basement hole center
    m_presentPos = {siloCenter.x, baseY + 2.0f, siloCenter.z};
}

// ── Spawn / Despawn Robots ──────────────────────────────────────────────

void CullSession::spawnRobots(const glm::vec3& center, float baseY,
                               ModelRenderer* renderer,
                               const std::string& modelPath) {
    m_robotCount = MAX_ROBOTS;
    for (int i = 0; i < MAX_ROBOTS; ++i) {
        m_robots[i].init(m_sceneObjects, renderer);
        // Stagger robot positions around the silo center
        glm::vec3 pos = center;
        pos.y = baseY;
        pos.x += (i - 1) * 2.0f; // -2, 0, +2 offset
        pos.z += 3.0f;            // slightly in front
        m_robots[i].spawn(pos, renderer, modelPath);
        m_robots[i].setPresentPos(m_presentPos);
    }
}

void CullSession::despawn() {
    for (int i = 0; i < MAX_ROBOTS; ++i) {
        m_robots[i].despawn();
    }
    m_robotCount = 0;
    despawnDoors();
    m_active = false;
    m_sessionComplete = false;
    m_allTargets.clear();
    m_nextTargetIdx = 0;
    m_presentingRobotIdx = -1;
}

// ── Start Culling Session ───────────────────────────────────────────────

void CullSession::startCulling() {
    if (m_active) return;

    scanForTargets();
    if (m_allTargets.empty()) {
        std::cout << "[CullSession] No objects to cull." << std::endl;
        return;
    }

    m_active = true;
    m_sessionComplete = false;
    m_nextTargetIdx = 0;
    m_presentingRobotIdx = -1;

    // Spawn the sliding doors
    spawnDoors(m_siloCenter, m_baseY);

    // Set present position on all robots
    for (int i = 0; i < m_robotCount; ++i) {
        // CullRobot stores presentPos internally — we set it via a direct approach
        // For now the presentPos is communicated through the CullTarget workflow
    }

    // Assign first target to first robot
    assignNextTarget();

    std::cout << "[CullSession] Started with " << m_allTargets.size() << " objects to review." << std::endl;
}

// ── Scanning ────────────────────────────────────────────────────────────

void CullSession::scanForTargets() {
    m_allTargets.clear();
    if (!m_sceneObjects) return;

    for (auto& objPtr : *m_sceneObjects) {
        if (!objPtr) continue;
        if (objPtr->getBuildingType() != "filesystem") continue;
        if (objPtr->isDoor()) continue; // skip folders

        std::string targetLevel = objPtr->getTargetLevel();
        if (targetLevel.rfind("fs://", 0) != 0) continue;
        std::string filePath = targetLevel.substr(5);

        CullTarget target;
        target.sourcePath = filePath;
        target.objName = objPtr->getName();
        target.wallPos = objPtr->getTransform().getPosition();
        target.wallScale = objPtr->getTransform().getScale();
        target.wallYaw = objPtr->getEulerRotation().y;
        m_allTargets.push_back(std::move(target));
    }
}

// ── Assign Next Target ──────────────────────────────────────────────────

void CullSession::assignNextTarget() {
    if (m_nextTargetIdx >= static_cast<int>(m_allTargets.size())) return;

    // Find an idle robot
    for (int i = 0; i < m_robotCount; ++i) {
        if (m_robots[i].isIdle() && m_robots[i].isSpawned()) {
            m_robots[i].assignTarget(m_allTargets[m_nextTargetIdx]);
            m_nextTargetIdx++;
            return;
        }
    }
}

// ── Sliding Doors ───────────────────────────────────────────────────────

void CullSession::spawnDoors(const glm::vec3& center, float baseY) {
    if (!m_sceneObjects || !m_renderer) return;

    glm::vec4 doorColor{0.3f, 0.3f, 0.35f, 1.0f};
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, doorColor);

    // Closed positions: centered over hole
    m_doorClosedLeft = {center.x - DOOR_WIDTH / 2.0f, baseY - 0.5f, center.z};
    m_doorClosedRight = {center.x + DOOR_WIDTH / 2.0f, baseY - 0.5f, center.z};

    // Open positions: each slides outward by DOOR_WIDTH
    m_doorOpenLeft = {center.x - DOOR_WIDTH / 2.0f - DOOR_WIDTH, baseY - 0.5f, center.z};
    m_doorOpenRight = {center.x + DOOR_WIDTH / 2.0f + DOOR_WIDTH, baseY - 0.5f, center.z};

    // Left door
    {
        uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices);
        auto obj = std::make_unique<SceneObject>("CullDoorLeft");
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(1.0f);
        obj->setPrimitiveColor(doorColor);
        obj->setBuildingType("cull_door");
        obj->getTransform().setPosition(m_doorClosedLeft);
        obj->getTransform().setScale({DOOR_WIDTH, DOOR_THICKNESS, DOOR_DEPTH});
        m_doorLeft = obj.get();
        m_sceneObjects->push_back(std::move(obj));
    }

    // Right door
    {
        uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices);
        auto obj = std::make_unique<SceneObject>("CullDoorRight");
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(1.0f);
        obj->setPrimitiveColor(doorColor);
        obj->setBuildingType("cull_door");
        obj->getTransform().setPosition(m_doorClosedRight);
        obj->getTransform().setScale({DOOR_WIDTH, DOOR_THICKNESS, DOOR_DEPTH});
        m_doorRight = obj.get();
        m_sceneObjects->push_back(std::move(obj));
    }

    m_doorsOpen = false;
}

void CullSession::despawnDoors() {
    if (!m_sceneObjects || !m_renderer) return;

    auto removeDoor = [&](SceneObject*& door) {
        if (!door) return;
        auto it = m_sceneObjects->begin();
        while (it != m_sceneObjects->end()) {
            if (it->get() == door) {
                uint32_t handle = (*it)->getBufferHandle();
                if (handle != 0) m_renderer->destroyModel(handle);
                it = m_sceneObjects->erase(it);
                break;
            } else {
                ++it;
            }
        }
        door = nullptr;
    };

    removeDoor(m_doorLeft);
    removeDoor(m_doorRight);
}

void CullSession::openDoors() {
    if (m_doorsOpen || !m_doorLeft || !m_doorRight) return;
    m_doorLeft->startMoveTo(m_doorClosedLeft, m_doorOpenLeft, DOOR_ANIM_DURATION, true);
    m_doorRight->startMoveTo(m_doorClosedRight, m_doorOpenRight, DOOR_ANIM_DURATION, true);
    m_doorsOpen = true;
    m_doorAnimating = true;
    m_doorTimer = 0.0f;
}

void CullSession::closeDoors() {
    if (!m_doorsOpen || !m_doorLeft || !m_doorRight) return;
    m_doorLeft->startMoveTo(m_doorOpenLeft, m_doorClosedLeft, DOOR_ANIM_DURATION, true);
    m_doorRight->startMoveTo(m_doorOpenRight, m_doorClosedRight, DOOR_ANIM_DURATION, true);
    m_doorsOpen = false;
    m_doorAnimating = true;
    m_doorTimer = 0.0f;
}

// ── Robot Access ────────────────────────────────────────────────────────

CullRobot* CullSession::getRobotAt(SceneObject* obj) {
    if (!obj) return nullptr;
    for (int i = 0; i < m_robotCount; ++i) {
        if (m_robots[i].getSceneObject() == obj) return &m_robots[i];
    }
    return nullptr;
}

// ── Update ──────────────────────────────────────────────────────────────

void CullSession::update(float deltaTime) {
    // Update all robots regardless of session state
    for (int i = 0; i < m_robotCount; ++i) {
        m_robots[i].update(deltaTime);
    }

    // Animate doors
    if (m_doorAnimating) {
        if (m_doorLeft) m_doorLeft->updateMoveTo(deltaTime);
        if (m_doorRight) m_doorRight->updateMoveTo(deltaTime);
        m_doorTimer += deltaTime;
        if (m_doorTimer >= DOOR_ANIM_DURATION) {
            m_doorAnimating = false;
        }
    }

    // Handle pending door close after reject
    if (m_pendingDoorClose) {
        m_doorCloseDelay -= deltaTime;
        if (m_doorCloseDelay <= 0.0f) {
            closeDoors();
            m_pendingDoorClose = false;
        }
    }

    if (!m_active) return;

    // Pipeline logic: find the first presenting robot
    m_presentingRobotIdx = -1;
    for (int i = 0; i < m_robotCount; ++i) {
        if (m_robots[i].isPresenting()) {
            m_presentingRobotIdx = i;
            break;
        }
    }

    // When a robot reaches PRESENTING, assign the next target to an idle robot
    // This creates the pipeline: while player decides, next robot is fetching
    if (m_presentingRobotIdx >= 0 && m_nextTargetIdx < static_cast<int>(m_allTargets.size())) {
        assignNextTarget();
    }

    // Also try to assign if no robot is presenting yet (initial startup)
    if (m_presentingRobotIdx < 0 && m_nextTargetIdx < static_cast<int>(m_allTargets.size())) {
        // Check if any robot is not idle (still working on previous assignment)
        bool anyWorking = false;
        for (int i = 0; i < m_robotCount; ++i) {
            if (!m_robots[i].isIdle()) { anyWorking = true; break; }
        }
        if (!anyWorking) {
            assignNextTarget();
        }
    }

    // Check for session completion: all targets exhausted and all robots idle
    if (m_nextTargetIdx >= static_cast<int>(m_allTargets.size())) {
        bool allIdle = true;
        for (int i = 0; i < m_robotCount; ++i) {
            if (!m_robots[i].isIdle()) { allIdle = false; break; }
        }
        if (allIdle) {
            m_active = false;
            m_sessionComplete = true;
            despawnDoors();
            std::cout << "[CullSession] Session complete." << std::endl;
        }
    }
}

// ── Input Handling ──────────────────────────────────────────────────────

void CullSession::handleInput() {
    if (!m_active || m_presentingRobotIdx < 0) return;

    if (Input::isKeyPressed(Input::KEY_UP)) {
        // Accept
        m_robots[m_presentingRobotIdx].acceptItem();
    } else if (Input::isKeyPressed(Input::KEY_DOWN)) {
        // Reject — open doors, then drop
        openDoors();
        m_robots[m_presentingRobotIdx].rejectItem();
        // Schedule door close after a brief delay
        m_pendingDoorClose = true;
        m_doorCloseDelay = 0.8f; // enough time for the drop
    }
}

// ── Decision UI ─────────────────────────────────────────────────────────

void CullSession::renderDecisionUI() {
    if (!m_active || m_presentingRobotIdx < 0) return;
    if (!m_robots[m_presentingRobotIdx].isPresenting()) return;

    const auto& target = m_robots[m_presentingRobotIdx].getCurrentTarget();
    std::string filename = std::filesystem::path(target.sourcePath).filename().string();

    float windowW = ImGui::GetIO().DisplaySize.x;
    float windowH = ImGui::GetIO().DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(windowW * 0.5f, windowH - 80.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.8f);

    if (ImGui::Begin("##CullDecision", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize)) {

        // Filename centered
        ImVec2 textSize = ImGui::CalcTextSize(filename.c_str());
        float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((availW - textSize.x) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", filename.c_str());

        ImGui::Separator();

        // Accept / Reject buttons
        float buttonW = (availW - 20.0f) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        if (ImGui::Button("Accept (Up)", ImVec2(buttonW, 35))) {
            m_robots[m_presentingRobotIdx].acceptItem();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0, 20.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Reject (Down)", ImVec2(buttonW, 35))) {
            openDoors();
            m_robots[m_presentingRobotIdx].rejectItem();
            m_pendingDoorClose = true;
            m_doorCloseDelay = 0.8f;
        }
        ImGui::PopStyleColor(2);

        // Progress indicator
        int remaining = static_cast<int>(m_allTargets.size()) - m_nextTargetIdx;
        // Add back robots that are still working (not idle, not yet decided)
        for (int i = 0; i < m_robotCount; ++i) {
            if (!m_robots[i].isIdle() && i != m_presentingRobotIdx) remaining++;
        }
        ImGui::TextDisabled("(%d/%zu remaining)", remaining + 1, m_allTargets.size());
    }
    ImGui::End();
}

} // namespace eden
