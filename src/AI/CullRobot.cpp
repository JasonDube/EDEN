#include "CullRobot.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include "../Editor/GLBLoader.hpp"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdlib>

namespace eden {

// ── Init / Spawn / Despawn ─────────────────────────────────────────────

void CullRobot::init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                     ModelRenderer* renderer) {
    m_sceneObjects = sceneObjects;
    m_renderer = renderer;
}

void CullRobot::spawn(const glm::vec3& homePos, ModelRenderer* renderer,
                       const std::string& modelPath) {
    if (m_spawned || !m_sceneObjects || !renderer) return;

    m_renderer = renderer;
    m_homePos = homePos;

    std::unique_ptr<SceneObject> obj;

    if (!modelPath.empty()) {
        auto result = GLBLoader::load(modelPath);
        if (result.success && !result.meshes.empty()) {
            std::vector<ModelVertex> allVerts;
            std::vector<uint32_t> allIndices;
            bool hasTex = false;
            std::vector<unsigned char> texData;
            int texW = 0, texH = 0;

            for (auto& m : result.meshes) {
                uint32_t baseIdx = static_cast<uint32_t>(allVerts.size());
                allVerts.insert(allVerts.end(), m.vertices.begin(), m.vertices.end());
                for (auto idx : m.indices) allIndices.push_back(baseIdx + idx);
                if (!hasTex && m.hasTexture) {
                    texData = m.texture.data;
                    texW = m.texture.width;
                    texH = m.texture.height;
                    hasTex = true;
                }
            }

            glm::vec3 bmin(FLT_MAX), bmax(-FLT_MAX);
            for (auto& v : allVerts) {
                bmin = glm::min(bmin, v.position);
                bmax = glm::max(bmax, v.position);
            }
            glm::vec3 offset = {(bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f};
            for (auto& v : allVerts) v.position -= offset;
            bmin -= offset; bmax -= offset;

            float maxExtent = std::max({bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z});
            if (maxExtent > 0.0f) {
                float scaleFactor = 1.2f / maxExtent;
                for (auto& v : allVerts) v.position *= scaleFactor;
                bmin *= scaleFactor; bmax *= scaleFactor;
            }

            uint32_t handle;
            if (hasTex && !texData.empty()) {
                handle = renderer->createModel(allVerts, allIndices, texData.data(), texW, texH);
            } else {
                handle = renderer->createModel(allVerts, allIndices, nullptr, 0, 0);
            }

            obj = std::make_unique<SceneObject>("CullRobot");
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(allIndices.size()));
            obj->setVertexCount(static_cast<uint32_t>(allVerts.size()));
            obj->setLocalBounds({bmin, bmax});
            obj->setMeshData(allVerts, allIndices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setBuildingType("cullrobot");
            obj->setDescription("Cull Robot");
        }
    }

    if (!obj) {
        // Fallback: create orange cylinder mesh
        glm::vec4 color{1.0f, 0.5f, 0.1f, 1.0f};
        auto mesh = PrimitiveMeshBuilder::createCylinder(0.3f, 1.2f, 16, color);
        uint32_t handle = renderer->createModel(
            mesh.vertices, mesh.indices, nullptr, 0, 0);

        obj = std::make_unique<SceneObject>("CullRobot");
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cylinder);
        obj->setPrimitiveSize(0.3f);
        obj->setPrimitiveColor(color);
        obj->setBuildingType("cullrobot");
        obj->setDescription("Cull Robot");
    }

    obj->getTransform().setPosition(homePos);
    obj->getTransform().setScale({1.0f, 1.0f, 1.0f});

    m_sceneObject = obj.get();
    m_sceneObjects->push_back(std::move(obj));
    m_spawned = true;
    m_state = CullRobotState::IDLE;
    m_stateTimer = 0.0f;
}

void CullRobot::despawn() {
    if (!m_spawned || !m_sceneObjects || !m_renderer) return;

    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (it->get() == m_sceneObject) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) m_renderer->destroyModel(handle);
            it = m_sceneObjects->erase(it);
            break;
        } else {
            ++it;
        }
    }

    m_sceneObject = nullptr;
    m_spawned = false;
    m_state = CullRobotState::IDLE;
    m_stateTimer = 0.0f;
    m_carriedObject = nullptr;
    m_showMenu = false;
}

// ── Target Assignment ──────────────────────────────────────────────────

void CullRobot::assignTarget(const CullTarget& target) {
    if (!m_spawned || m_state != CullRobotState::IDLE) return;

    m_currentTarget = target;

    // Start walking to the wall object
    glm::vec3 from = m_sceneObject->getTransform().getPosition();
    glm::vec3 to = target.wallPos;
    to.y = from.y; // stay at ground level
    float dist = glm::length(to - from);
    float duration = std::max(dist / MOVE_SPEED, 0.3f);
    m_sceneObject->startMoveTo(from, to, duration, true);
    m_state = CullRobotState::WALKING_TO_WALL;
    m_stateTimer = 0.0f;
}

// ── Accept / Reject ────────────────────────────────────────────────────

void CullRobot::acceptItem() {
    if (m_state != CullRobotState::PRESENTING) return;

    // Walk back to wall position to return the object
    glm::vec3 from = m_sceneObject->getTransform().getPosition();
    glm::vec3 to = m_currentTarget.wallPos;
    to.y = from.y;
    float dist = glm::length(to - from);
    float duration = std::max(dist / MOVE_SPEED, 0.3f);
    m_sceneObject->startMoveTo(from, to, duration, true);
    m_state = CullRobotState::RETURNING;
    m_stateTimer = 0.0f;
}

void CullRobot::rejectItem() {
    if (m_state != CullRobotState::PRESENTING) return;
    m_state = CullRobotState::DROPPING;
    m_stateTimer = 0.0f;
}

// ── Menu UI ────────────────────────────────────────────────────────────

bool CullRobot::renderMenuUI() {
    if (!m_showMenu) return false;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(280, 120), ImGuiCond_Appearing);

    bool open = true;
    if (ImGui::Begin("Cull Robot##Menu", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

        const char* stateStr = "Idle";
        switch (m_state) {
            case CullRobotState::IDLE:               stateStr = "Idle"; break;
            case CullRobotState::WALKING_TO_WALL:    stateStr = "Fetching object"; break;
            case CullRobotState::PICKING_UP:          stateStr = "Picking up"; break;
            case CullRobotState::WALKING_TO_PRESENT: stateStr = "Carrying to hole"; break;
            case CullRobotState::PRESENTING:          stateStr = "Awaiting decision"; break;
            case CullRobotState::RETURNING:           stateStr = "Returning object"; break;
            case CullRobotState::DROPPING:            stateStr = "Dropping to trash"; break;
        }
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.1f, 1.0f), "Status: %s", stateStr);
    }
    ImGui::End();

    if (!open) {
        m_showMenu = false;
        return false;
    }
    return m_showMenu;
}

// ── Carried Item Position ──────────────────────────────────────────────

void CullRobot::setCarriedItemPosition() {
    if (!m_carriedObject || !m_sceneObject) return;
    glm::vec3 botPos = m_sceneObject->getTransform().getPosition();
    m_carriedObject->getTransform().setPosition({botPos.x, botPos.y + CARRY_Y_OFFSET, botPos.z});
}

// ── Scene Object Helpers ───────────────────────────────────────────────

SceneObject* CullRobot::findSceneObject(const std::string& objName) {
    if (!m_sceneObjects) return nullptr;
    for (auto& objPtr : *m_sceneObjects) {
        if (objPtr && objPtr->getName() == objName) return objPtr.get();
    }
    return nullptr;
}

void CullRobot::removeSceneObject(const std::string& objName) {
    if (!m_sceneObjects || !m_renderer) return;
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (*it && (*it)->getName() == objName) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) m_renderer->destroyModel(handle);
            it = m_sceneObjects->erase(it);
            return;
        }
        ++it;
    }
}

// ── Trash (FreeDesktop spec) ───────────────────────────────────────────

void CullRobot::moveToTrash(const std::string& sourcePath) {
    namespace fs = std::filesystem;

    const char* home = getenv("HOME");
    if (!home) return;

    std::string trashDir = std::string(home) + "/.local/share/Trash";
    std::string filesDir = trashDir + "/files";
    std::string infoDir = trashDir + "/info";

    std::error_code ec;
    fs::create_directories(filesDir, ec);
    fs::create_directories(infoDir, ec);

    std::string filename = fs::path(sourcePath).filename().string();

    // Resolve unique name in trash
    std::string destPath = filesDir + "/" + filename;
    std::string infoPath = infoDir + "/" + filename + ".trashinfo";
    if (fs::exists(destPath)) {
        std::string stem = fs::path(filename).stem().string();
        std::string ext = fs::path(filename).extension().string();
        for (int i = 1; i < 1000; ++i) {
            std::string candidate = stem + "(" + std::to_string(i) + ")" + ext;
            destPath = filesDir + "/" + candidate;
            infoPath = infoDir + "/" + candidate + ".trashinfo";
            if (!fs::exists(destPath)) break;
        }
    }

    // Move file
    fs::rename(sourcePath, destPath, ec);
    if (ec) {
        fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::remove(sourcePath, ec);
        if (ec) {
            std::cerr << "[CullRobot] Failed to trash " << sourcePath << ": " << ec.message() << std::endl;
            return;
        }
    }

    // Write .trashinfo metadata
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_r(&time, &tm);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &tm);

    std::ofstream info(infoPath);
    if (info.is_open()) {
        info << "[Trash Info]\n";
        info << "Path=" << sourcePath << "\n";
        info << "DeletionDate=" << timeBuf << "\n";
    }

    std::cout << "[CullRobot] Trashed: " << sourcePath << " -> " << destPath << std::endl;
}

// ── State Machine Update ───────────────────────────────────────────────

void CullRobot::update(float deltaTime) {
    if (!m_spawned || !m_sceneObject) return;
    if (m_state == CullRobotState::IDLE) return;

    m_stateTimer += deltaTime;

    switch (m_state) {
    case CullRobotState::WALKING_TO_WALL: {
        m_sceneObject->updateMoveTo(deltaTime);
        if (!m_sceneObject->isMovingTo()) {
            m_state = CullRobotState::PICKING_UP;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case CullRobotState::PICKING_UP: {
        if (m_stateTimer >= PICKUP_DURATION) {
            // Pick up the object — hide it at wall, carry it
            m_carriedObject = findSceneObject(m_currentTarget.objName);
            if (m_carriedObject) {
                m_carriedObject->setVisible(false);
                setCarriedItemPosition();
            }

            // Walk to presentation position (above the hole)
            glm::vec3 from = m_sceneObject->getTransform().getPosition();
            glm::vec3 to = m_presentPos;
            to.y = from.y;
            float dist = glm::length(to - from);
            float duration = std::max(dist / MOVE_SPEED, 0.3f);
            m_sceneObject->startMoveTo(from, to, duration, true);
            m_state = CullRobotState::WALKING_TO_PRESENT;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case CullRobotState::WALKING_TO_PRESENT: {
        m_sceneObject->updateMoveTo(deltaTime);
        // Keep carried item following the bot
        setCarriedItemPosition();
        if (!m_sceneObject->isMovingTo()) {
            // Show the carried item at the presentation position
            if (m_carriedObject) {
                m_carriedObject->setVisible(true);
                setCarriedItemPosition();
            }
            m_state = CullRobotState::PRESENTING;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case CullRobotState::PRESENTING: {
        // Wait for accept/reject decision — keep item hovering
        if (m_carriedObject) {
            setCarriedItemPosition();
        }
        break;
    }

    case CullRobotState::RETURNING: {
        m_sceneObject->updateMoveTo(deltaTime);
        setCarriedItemPosition();
        if (!m_sceneObject->isMovingTo()) {
            // Restore object to its original wall position
            if (m_carriedObject) {
                m_carriedObject->getTransform().setPosition(m_currentTarget.wallPos);
                m_carriedObject->getTransform().setScale(m_currentTarget.wallScale);
                m_carriedObject->setEulerRotation({0.0f, m_currentTarget.wallYaw, 0.0f});
                m_carriedObject->setVisible(true);
                m_carriedObject = nullptr;
            }
            m_state = CullRobotState::IDLE;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case CullRobotState::DROPPING: {
        // Object removed from scene, file trashed
        if (m_carriedObject) {
            m_carriedObject->setVisible(false);
            m_carriedObject = nullptr;
        }
        removeSceneObject(m_currentTarget.objName);
        moveToTrash(m_currentTarget.sourcePath);
        m_state = CullRobotState::IDLE;
        m_stateTimer = 0.0f;
        break;
    }

    case CullRobotState::IDLE:
        break;
    }
}

} // namespace eden
