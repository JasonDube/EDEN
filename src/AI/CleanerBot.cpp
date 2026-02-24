#include "CleanerBot.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include "../Editor/GLBLoader.hpp"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <chrono>

namespace eden {

// ── Extension helpers ──────────────────────────────────────────────────

bool CleanerBot::isImageExtension(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".webp" || ext == ".bmp" || ext == ".gif";
}

bool CleanerBot::isVideoExtension(const std::string& ext) {
    return ext == ".mp4" || ext == ".avi" || ext == ".mkv" ||
           ext == ".webm" || ext == ".mov" || ext == ".flv" || ext == ".wmv";
}

// ── Timestamp helper ───────────────────────────────────────────────────

std::string CleanerBot::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_r(&time, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// ── Work Log Persistence ───────────────────────────────────────────────

std::string CleanerBot::getLogPath() {
    const char* home = getenv("HOME");
    std::string configDir = std::string(home ? home : "/tmp") + "/.config/eden";
    std::filesystem::create_directories(configDir);
    return configDir + "/cleanerbot_log.json";
}

void CleanerBot::loadWorkLog() {
    m_workLog.clear();
    std::string path = getLogPath();
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_array()) return;

        for (auto& entry : j) {
            CleanerLogEntry log;
            log.timestamp = entry.value("timestamp", "");
            log.destination = entry.value("destination", "");
            if (entry.contains("files") && entry["files"].is_array()) {
                for (auto& file : entry["files"]) {
                    log.filesMoved.push_back(file.get<std::string>());
                }
            }
            if (!log.timestamp.empty()) {
                m_workLog.push_back(std::move(log));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[CleanerBot] Failed to load log: " << e.what() << std::endl;
    }
}

void CleanerBot::saveWorkLog() {
    nlohmann::json j = nlohmann::json::array();
    for (auto& entry : m_workLog) {
        nlohmann::json e;
        e["timestamp"] = entry.timestamp;
        e["destination"] = entry.destination;
        e["files"] = entry.filesMoved;
        j.push_back(e);
    }

    std::string path = getLogPath();
    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2);
    }
}

// ── Init / Spawn / Despawn ─────────────────────────────────────────────

void CleanerBot::init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                      ModelRenderer* renderer) {
    m_sceneObjects = sceneObjects;
    m_renderer = renderer;
    loadWorkLog();
}

void CleanerBot::spawn(const glm::vec3& homePos, ModelRenderer* renderer,
                       const std::string& modelPath) {
    if (m_spawned || !m_sceneObjects || !renderer) return;

    m_renderer = renderer;
    m_homePos = homePos;

    std::unique_ptr<SceneObject> obj;

    if (!modelPath.empty()) {
        // Load custom .glb model
        auto result = GLBLoader::load(modelPath);
        if (result.success && !result.meshes.empty()) {
            // Merge all meshes
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

            // Normalize: center XZ, but sit bottom on Y=0
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

            obj = std::make_unique<SceneObject>("CleanerBot");
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(allIndices.size()));
            obj->setVertexCount(static_cast<uint32_t>(allVerts.size()));
            obj->setLocalBounds({bmin, bmax});
            obj->setMeshData(allVerts, allIndices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setBuildingType("cleanerbot");
            obj->setDescription("Cleaner Bot");
        }
    }

    if (!obj) {
        // Fallback: create cyan cylinder mesh
        glm::vec4 color{0.3f, 0.8f, 1.0f, 1.0f};
        auto mesh = PrimitiveMeshBuilder::createCylinder(0.3f, 1.2f, 16, color);
        uint32_t handle = renderer->createModel(
            mesh.vertices, mesh.indices, nullptr, 0, 0);

        obj = std::make_unique<SceneObject>("CleanerBot");
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cylinder);
        obj->setPrimitiveSize(0.3f);
        obj->setPrimitiveColor(color);
        obj->setBuildingType("cleanerbot");
        obj->setDescription("Cleaner Bot");
    }

    obj->getTransform().setPosition(homePos);
    obj->getTransform().setScale({1.0f, 1.0f, 1.0f});

    m_sceneObject = obj.get();
    m_sceneObjects->push_back(std::move(obj));
    m_spawned = true;
    m_state = CleanerBotState::IDLE;
    m_stateTimer = 0.0f;
}

void CleanerBot::despawn() {
    if (!m_spawned || !m_sceneObjects || !m_renderer) return;

    // Find and remove bot from scene
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (it->get() == m_sceneObject) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) {
                m_renderer->destroyModel(handle);
            }
            it = m_sceneObjects->erase(it);
            break;
        } else {
            ++it;
        }
    }

    m_sceneObject = nullptr;
    m_spawned = false;
    m_state = CleanerBotState::IDLE;
    m_stateTimer = 0.0f;
    m_targets.clear();
    m_targetIndex = 0;
    m_showMenu = false;
    m_showReport = false;
}

// ── Activation ─────────────────────────────────────────────────────────

void CleanerBot::activate() {
    if (!m_spawned || m_state != CleanerBotState::IDLE) return;
    m_state = CleanerBotState::SCANNING;
    m_stateTimer = 0.0f;
    m_targets.clear();
    m_targetIndex = 0;
    m_sessionFilesMoved.clear();
    m_sessionDestination.clear();
}

bool CleanerBot::isActive() const {
    return m_spawned && m_state != CleanerBotState::IDLE;
}

// ── Menu UI ────────────────────────────────────────────────────────────

bool CleanerBot::renderMenuUI() {
    if (!m_showMenu) return false;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!m_showReport) {
        // Main menu
        ImGui::SetNextWindowSize(ImVec2(280, 160), ImGuiCond_Appearing);
        bool open = true;
        if (ImGui::Begin("Cleaner Bot##Menu", &open,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

            if (m_state == CleanerBotState::IDLE) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
                if (ImGui::Button("Activate", ImVec2(-1, 30))) {
                    activate();
                    m_showMenu = false;
                    ImGui::PopStyleColor();
                    ImGui::End();
                    return false;
                }
                ImGui::PopStyleColor();
            } else {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Status: %s", getStateName());
            }

            ImGui::Separator();

            if (ImGui::Button("View Report", ImVec2(-1, 30))) {
                m_showReport = true;
            }

            ImGui::TextDisabled("%zu sessions logged", m_workLog.size());
        }
        ImGui::End();

        if (!open) {
            m_showMenu = false;
            return false;
        }
    } else {
        // Report view
        ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_Appearing);
        bool open = true;
        if (ImGui::Begin("Cleaner Bot Report##Report", &open,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

            if (ImGui::Button("Back")) {
                m_showReport = false;
            }

            ImGui::Separator();

            if (m_workLog.empty()) {
                ImGui::TextDisabled("No work sessions recorded yet.");
            } else {
                // Show newest first
                ImGui::BeginChild("##logScroll", ImVec2(-1, -1), ImGuiChildFlags_Borders);
                for (int i = static_cast<int>(m_workLog.size()) - 1; i >= 0; --i) {
                    auto& entry = m_workLog[i];

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
                    ImGui::Text("%s", entry.timestamp.c_str());
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    ImGui::Text("  Moved %zu file(s) to %s",
                                entry.filesMoved.size(), entry.destination.c_str());

                    // Show file list indented
                    ImGui::Indent(20.0f);
                    for (auto& file : entry.filesMoved) {
                        ImGui::BulletText("%s", file.c_str());
                    }
                    ImGui::Unindent(20.0f);

                    if (i > 0) ImGui::Separator();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!open) {
            m_showReport = false;
            m_showMenu = false;
            return false;
        }
    }

    return m_showMenu;
}

// ── State names ────────────────────────────────────────────────────────

const char* CleanerBot::getStateName() const {
    switch (m_state) {
        case CleanerBotState::IDLE:            return "Idle";
        case CleanerBotState::SCANNING:        return "Scanning...";
        case CleanerBotState::WALKING_TO_FILE: return "Walking to file";
        case CleanerBotState::PICKING_UP:      return "Picking up";
        case CleanerBotState::WALKING_HOME:    return "Returning home";
        case CleanerBotState::DONE:            return "Done!";
    }
    return "Unknown";
}

int CleanerBot::getFilesRemaining() const {
    return static_cast<int>(m_targets.size()) - m_targetIndex;
}

int CleanerBot::getTotalFiles() const {
    return static_cast<int>(m_targets.size());
}

// ── Scanning ───────────────────────────────────────────────────────────

void CleanerBot::scanForFiles() {
    m_targets.clear();

    const char* home = getenv("HOME");
    if (!home) return;
    std::string homeDir = home;
    std::string picturesDir = homeDir + "/Pictures";
    std::string videosDir = homeDir + "/Videos";

    for (auto& objPtr : *m_sceneObjects) {
        if (!objPtr) continue;
        if (objPtr->getBuildingType() != "filesystem") continue;
        if (objPtr->isDoor()) continue;

        std::string targetLevel = objPtr->getTargetLevel();
        if (targetLevel.rfind("fs://", 0) != 0) continue;
        std::string filePath = targetLevel.substr(5);

        // Get extension
        std::filesystem::path p(filePath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::string destDir;
        if (isImageExtension(ext)) {
            destDir = picturesDir;
        } else if (isVideoExtension(ext)) {
            destDir = videosDir;
        } else {
            continue; // not an image or video
        }

        CleanerTarget target;
        target.sourcePath = filePath;
        target.destDir = destDir;
        target.position = objPtr->getTransform().getPosition();
        target.objName = objPtr->getName();
        m_targets.push_back(std::move(target));
    }
}

// ── File move ──────────────────────────────────────────────────────────

std::string CleanerBot::resolveDestPath(const std::string& filename,
                                        const std::string& destDir) {
    namespace fs = std::filesystem;
    std::string base = destDir + "/" + filename;
    if (!fs::exists(base)) return base;

    // Extract stem and extension
    fs::path p(filename);
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    for (int i = 1; i < 1000; ++i) {
        std::string candidate = destDir + "/" + stem + "(" + std::to_string(i) + ")" + ext;
        if (!fs::exists(candidate)) return candidate;
    }
    return base; // fallback
}

void CleanerBot::performFileMove(const CleanerTarget& target) {
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::create_directories(target.destDir, ec);
    if (ec) {
        std::cerr << "[CleanerBot] Failed to create dir: " << target.destDir
                  << " — " << ec.message() << std::endl;
        return;
    }

    std::string filename = fs::path(target.sourcePath).filename().string();
    std::string dest = resolveDestPath(filename, target.destDir);

    fs::rename(target.sourcePath, dest, ec);
    if (ec) {
        // rename fails across filesystems — fall back to copy+delete
        fs::copy_file(target.sourcePath, dest, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            fs::remove(target.sourcePath, ec);
        }
        if (ec) {
            std::cerr << "[CleanerBot] Failed to move " << target.sourcePath
                      << " → " << dest << " — " << ec.message() << std::endl;
            return;
        }
    }

    std::cout << "[CleanerBot] Moved: " << target.sourcePath
              << " → " << dest << std::endl;

    // Track for session log
    m_sessionFilesMoved.push_back(filename);
    // Use a friendly destination name
    const char* home = getenv("HOME");
    if (home) {
        std::string homeStr = home;
        if (target.destDir == homeStr + "/Pictures") {
            m_sessionDestination = "~/Pictures";
        } else if (target.destDir == homeStr + "/Videos") {
            m_sessionDestination = "~/Videos";
        } else {
            m_sessionDestination = target.destDir;
        }
    } else {
        m_sessionDestination = target.destDir;
    }
}

// ── Scene object removal ───────────────────────────────────────────────

void CleanerBot::removeSceneObject(const std::string& objName) {
    if (!m_sceneObjects || !m_renderer) return;

    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (*it && (*it)->getName() == objName) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) {
                m_renderer->destroyModel(handle);
            }
            it = m_sceneObjects->erase(it);
            return;
        }
        ++it;
    }
}

// ── State machine update ───────────────────────────────────────────────

void CleanerBot::update(float deltaTime) {
    if (!m_spawned || !m_sceneObject) return;
    if (m_state == CleanerBotState::IDLE) return;

    m_stateTimer += deltaTime;

    switch (m_state) {
    case CleanerBotState::SCANNING: {
        if (m_stateTimer >= SCAN_DURATION) {
            scanForFiles();
            if (m_targets.empty()) {
                // Nothing to clean — go straight to DONE
                m_state = CleanerBotState::DONE;
                m_stateTimer = 0.0f;
            } else {
                // Start walking to first target
                m_targetIndex = 0;
                glm::vec3 from = m_sceneObject->getTransform().getPosition();
                glm::vec3 to = m_targets[0].position;
                float dist = glm::length(to - from);
                float duration = std::max(dist / MOVE_SPEED, 0.3f);
                m_sceneObject->startMoveTo(from, to, duration, true);
                m_state = CleanerBotState::WALKING_TO_FILE;
                m_stateTimer = 0.0f;
            }
        }
        break;
    }

    case CleanerBotState::WALKING_TO_FILE: {
        m_sceneObject->updateMoveTo(deltaTime);
        if (!m_sceneObject->isMovingTo()) {
            m_state = CleanerBotState::PICKING_UP;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case CleanerBotState::PICKING_UP: {
        if (m_stateTimer >= PICKUP_DURATION) {
            // Perform the actual file move and remove 3D object
            if (m_targetIndex < static_cast<int>(m_targets.size())) {
                performFileMove(m_targets[m_targetIndex]);
                removeSceneObject(m_targets[m_targetIndex].objName);
            }
            m_targetIndex++;

            if (m_targetIndex < static_cast<int>(m_targets.size())) {
                // Walk to next target
                glm::vec3 from = m_sceneObject->getTransform().getPosition();
                glm::vec3 to = m_targets[m_targetIndex].position;
                float dist = glm::length(to - from);
                float duration = std::max(dist / MOVE_SPEED, 0.3f);
                m_sceneObject->startMoveTo(from, to, duration, true);
                m_state = CleanerBotState::WALKING_TO_FILE;
                m_stateTimer = 0.0f;
            } else {
                // All done — walk home
                glm::vec3 from = m_sceneObject->getTransform().getPosition();
                float dist = glm::length(m_homePos - from);
                float duration = std::max(dist / MOVE_SPEED, 0.3f);
                m_sceneObject->startMoveTo(from, m_homePos, duration, true);
                m_state = CleanerBotState::WALKING_HOME;
                m_stateTimer = 0.0f;
            }
        }
        break;
    }

    case CleanerBotState::WALKING_HOME: {
        m_sceneObject->updateMoveTo(deltaTime);
        if (!m_sceneObject->isMovingTo()) {
            // Save work log entry before going to DONE
            if (!m_sessionFilesMoved.empty()) {
                CleanerLogEntry entry;
                entry.timestamp = currentTimestamp();
                entry.filesMoved = m_sessionFilesMoved;
                entry.destination = m_sessionDestination;
                m_workLog.push_back(std::move(entry));
                saveWorkLog();
                std::cout << "[CleanerBot] Logged session: " << m_sessionFilesMoved.size()
                          << " files moved" << std::endl;
                m_sessionFilesMoved.clear();
                m_sessionDestination.clear();
            }

            m_state = CleanerBotState::DONE;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case CleanerBotState::DONE: {
        if (m_stateTimer >= DONE_DURATION) {
            m_state = CleanerBotState::IDLE;
            m_stateTimer = 0.0f;
            m_targets.clear();
            m_targetIndex = 0;
        }
        break;
    }

    case CleanerBotState::IDLE:
        break;
    }
}

} // namespace eden
