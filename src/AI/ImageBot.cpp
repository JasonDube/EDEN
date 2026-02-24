#include "ImageBot.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include "../Editor/GLBLoader.hpp"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <chrono>

namespace eden {

// ── Extension helper ───────────────────────────────────────────────────

bool ImageBot::isImageExtension(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".webp" || ext == ".bmp" || ext == ".gif";
}

// ── Timestamp helper ───────────────────────────────────────────────────

std::string ImageBot::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_r(&time, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// ── Work Log Persistence ───────────────────────────────────────────────

std::string ImageBot::getLogPath() {
    const char* home = getenv("HOME");
    std::string configDir = std::string(home ? home : "/tmp") + "/.config/eden";
    std::filesystem::create_directories(configDir);
    return configDir + "/imagebot_log.json";
}

void ImageBot::loadWorkLog() {
    m_workLog.clear();
    std::string path = getLogPath();
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_array()) return;

        for (auto& entry : j) {
            ImageBotLogEntry log;
            log.timestamp = entry.value("timestamp", "");
            log.totalDescribed = entry.value("totalDescribed", 0);
            if (entry.contains("files") && entry["files"].is_array()) {
                for (auto& file : entry["files"]) {
                    log.filesDescribed.push_back(file.get<std::string>());
                }
            }
            if (!log.timestamp.empty()) {
                m_workLog.push_back(std::move(log));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ImageBot] Failed to load log: " << e.what() << std::endl;
    }
}

void ImageBot::saveWorkLog() {
    nlohmann::json j = nlohmann::json::array();
    for (auto& entry : m_workLog) {
        nlohmann::json e;
        e["timestamp"] = entry.timestamp;
        e["totalDescribed"] = entry.totalDescribed;
        e["files"] = entry.filesDescribed;
        j.push_back(e);
    }

    std::string path = getLogPath();
    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2);
    }
}

// ── Init / Spawn / Despawn ─────────────────────────────────────────────

void ImageBot::init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                    ModelRenderer* renderer) {
    m_sceneObjects = sceneObjects;
    m_renderer = renderer;
    loadWorkLog();
}

void ImageBot::spawn(const glm::vec3& homePos, ModelRenderer* renderer,
                     const std::string& modelPath) {
    if (m_spawned || !m_sceneObjects || !renderer) return;

    m_renderer = renderer;
    m_homePos = homePos;

    std::unique_ptr<SceneObject> obj;

    if (!modelPath.empty()) {
        // Load custom .glb model
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

            // Normalize: center XZ, sit bottom on Y=0
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

            obj = std::make_unique<SceneObject>("ImageBot");
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(allIndices.size()));
            obj->setVertexCount(static_cast<uint32_t>(allVerts.size()));
            obj->setLocalBounds({bmin, bmax});
            obj->setMeshData(allVerts, allIndices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setBuildingType("imagebot");
            obj->setDescription("Image Bot");
        }
    }

    if (!obj) {
        // Fallback: green cylinder (distinct from CleanerBot's cyan)
        glm::vec4 color{0.3f, 0.9f, 0.4f, 1.0f};
        auto mesh = PrimitiveMeshBuilder::createCylinder(0.3f, 1.2f, 16, color);
        uint32_t handle = renderer->createModel(
            mesh.vertices, mesh.indices, nullptr, 0, 0);

        obj = std::make_unique<SceneObject>("ImageBot");
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cylinder);
        obj->setPrimitiveSize(0.3f);
        obj->setPrimitiveColor(color);
        obj->setBuildingType("imagebot");
        obj->setDescription("Image Bot");
    }

    obj->getTransform().setPosition(homePos);
    obj->getTransform().setScale({1.0f, 1.0f, 1.0f});

    m_sceneObject = obj.get();
    m_sceneObjects->push_back(std::move(obj));
    m_spawned = true;
    m_state = ImageBotState::IDLE;
    m_stateTimer = 0.0f;
}

void ImageBot::despawn() {
    if (!m_spawned || !m_sceneObjects || !m_renderer) return;

    // Wait for any in-flight describe thread
    if (m_describeThread.joinable()) {
        m_describeThread.join();
    }

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
    m_state = ImageBotState::IDLE;
    m_stateTimer = 0.0f;
    m_targets.clear();
    m_targetIndex = 0;
    m_showMenu = false;
    m_showReport = false;
}

// ── Activation ─────────────────────────────────────────────────────────

void ImageBot::activate() {
    if (!m_spawned || m_state != ImageBotState::IDLE) return;
    if (!m_smolvlmReady) return;
    m_state = ImageBotState::SCANNING;
    m_stateTimer = 0.0f;
    m_targets.clear();
    m_targetIndex = 0;
    m_sessionFilesDescribed.clear();
}

bool ImageBot::isActive() const {
    return m_spawned && m_state != ImageBotState::IDLE;
}

// ── Menu UI ────────────────────────────────────────────────────────────

bool ImageBot::renderMenuUI() {
    if (!m_showMenu) return false;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!m_showReport) {
        // Main menu
        ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_Appearing);
        bool open = true;
        if (ImGui::Begin("Image Bot##Menu", &open,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

            if (m_state == ImageBotState::IDLE) {
                if (m_smolvlmReady) {
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
                    ImGui::BeginDisabled();
                    ImGui::Button("Activate", ImVec2(-1, 30));
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                       "SmolVLM server not running");
                }
            } else {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Status: %s", getStateName());
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
        if (ImGui::Begin("Image Bot Report##Report", &open,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

            if (ImGui::Button("Back")) {
                m_showReport = false;
            }

            ImGui::Separator();

            if (m_workLog.empty()) {
                ImGui::TextDisabled("No work sessions recorded yet.");
            } else {
                ImGui::BeginChild("##logScroll", ImVec2(-1, -1), ImGuiChildFlags_Borders);
                for (int i = static_cast<int>(m_workLog.size()) - 1; i >= 0; --i) {
                    auto& entry = m_workLog[i];

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
                    ImGui::Text("%s", entry.timestamp.c_str());
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    ImGui::Text("  Described %zu file(s)",
                                entry.filesDescribed.size());

                    ImGui::Indent(20.0f);
                    for (auto& file : entry.filesDescribed) {
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

const char* ImageBot::getStateName() const {
    switch (m_state) {
        case ImageBotState::IDLE:             return "Idle";
        case ImageBotState::SCANNING:         return "Scanning...";
        case ImageBotState::WALKING_TO_IMAGE: return "Walking to image";
        case ImageBotState::DESCRIBING:       return "Describing...";
        case ImageBotState::WALKING_HOME:     return "Returning home";
        case ImageBotState::DONE:             return "Done!";
    }
    return "Unknown";
}

int ImageBot::getFilesRemaining() const {
    return static_cast<int>(m_targets.size()) - m_targetIndex;
}

int ImageBot::getTotalFiles() const {
    return static_cast<int>(m_targets.size());
}

// ── Scanning ───────────────────────────────────────────────────────────

void ImageBot::scanForImages() {
    m_targets.clear();

    for (auto& objPtr : *m_sceneObjects) {
        if (!objPtr) continue;
        if (objPtr->getBuildingType() != "filesystem") continue;

        std::string targetLevel = objPtr->getTargetLevel();
        if (targetLevel.rfind("fs://", 0) != 0) continue;
        std::string filePath = targetLevel.substr(5);

        std::filesystem::path p(filePath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (!isImageExtension(ext)) continue;

        // Skip if sidecar already exists
        if (std::filesystem::exists(filePath + ".desc.txt")) continue;

        ImageTarget target;
        target.imagePath = filePath;
        target.position = objPtr->getTransform().getPosition();
        target.objName = objPtr->getName();
        m_targets.push_back(std::move(target));
    }
}

// ── SmolVLM HTTP call ──────────────────────────────────────────────────

std::string ImageBot::callSmolVLM(const std::string& imagePath) {
    try {
        httplib::Client cli("localhost", 8082);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(30);

        nlohmann::json body;
        body["image_path"] = imagePath;

        auto res = cli.Post("/describe", body.dump(), "application/json");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body);
            if (j.contains("description")) {
                return j["description"].get<std::string>();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ImageBot] SmolVLM call failed: " << e.what() << std::endl;
    }
    return "";
}

void ImageBot::startDescribeAsync(const ImageTarget& target) {
    m_describeComplete = false;
    m_describeResult.clear();

    if (m_describeThread.joinable()) {
        m_describeThread.join();
    }

    m_describeThread = std::thread([this, path = target.imagePath]() {
        std::string result = callSmolVLM(path);
        m_describeResult = std::move(result);
        m_describeComplete = true;
    });
}

// ── Apply description ──────────────────────────────────────────────────

void ImageBot::applyDescription(const ImageTarget& target, const std::string& desc) {
    if (desc.empty()) return;

    writeSidecarFile(target.imagePath, desc);

    std::string filename = std::filesystem::path(target.imagePath).filename().string();
    spawnDescCube(target.position, desc, filename);

    // Track for session log
    m_sessionFilesDescribed.push_back(filename);

    std::cout << "[ImageBot] Described: " << target.imagePath << std::endl;
}

void ImageBot::writeSidecarFile(const std::string& imagePath, const std::string& desc) {
    std::string sidecarPath = imagePath + ".desc.txt";
    std::ofstream f(sidecarPath);
    if (f.is_open()) {
        f << desc;
    } else {
        std::cerr << "[ImageBot] Failed to write sidecar: " << sidecarPath << std::endl;
    }
}

void ImageBot::spawnDescCube(const glm::vec3& imagePos, const std::string& desc,
                             const std::string& imageName) {
    if (!m_sceneObjects || !m_renderer) return;

    // Small gold cube pinned just below the image panel
    glm::vec4 color{1.0f, 0.85f, 0.2f, 1.0f};
    float cubeSize = 0.3f;
    auto mesh = PrimitiveMeshBuilder::createCube(cubeSize, color);
    uint32_t handle = m_renderer->createModel(
        mesh.vertices, mesh.indices, nullptr, 0, 0);

    auto obj = std::make_unique<SceneObject>("DescCube_" + imageName);
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(cubeSize);
    obj->setPrimitiveColor(color);
    obj->setBuildingType("image_desc");
    obj->setDescription(imageName + "\n\n" + desc);

    // Position: tucked right under the image panel, same XZ (stays on curved wall)
    glm::vec3 cubePos = imagePos;
    cubePos.y -= 0.55f;
    obj->getTransform().setPosition(cubePos);
    obj->getTransform().setScale({1.0f, 1.0f, 1.0f});

    m_sceneObjects->push_back(std::move(obj));
}

// ── State machine update ───────────────────────────────────────────────

void ImageBot::update(float deltaTime) {
    if (!m_spawned || !m_sceneObject) return;
    if (m_state == ImageBotState::IDLE) return;

    m_stateTimer += deltaTime;

    switch (m_state) {
    case ImageBotState::SCANNING: {
        if (m_stateTimer >= SCAN_DURATION) {
            scanForImages();
            if (m_targets.empty()) {
                m_state = ImageBotState::DONE;
                m_stateTimer = 0.0f;
            } else {
                m_targetIndex = 0;
                glm::vec3 from = m_sceneObject->getTransform().getPosition();
                glm::vec3 to = m_targets[0].position;
                float dist = glm::length(to - from);
                float duration = std::max(dist / MOVE_SPEED, 0.3f);
                m_sceneObject->startMoveTo(from, to, duration, true);
                m_state = ImageBotState::WALKING_TO_IMAGE;
                m_stateTimer = 0.0f;
            }
        }
        break;
    }

    case ImageBotState::WALKING_TO_IMAGE: {
        m_sceneObject->updateMoveTo(deltaTime);
        if (!m_sceneObject->isMovingTo()) {
            // Arrived at image — start async describe
            if (m_targetIndex < static_cast<int>(m_targets.size())) {
                startDescribeAsync(m_targets[m_targetIndex]);
            }
            m_state = ImageBotState::DESCRIBING;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case ImageBotState::DESCRIBING: {
        // Poll for completion
        if (m_describeComplete.load()) {
            if (m_describeThread.joinable()) {
                m_describeThread.join();
            }

            // Apply the description
            if (m_targetIndex < static_cast<int>(m_targets.size())) {
                applyDescription(m_targets[m_targetIndex], m_describeResult);
            }
            m_targetIndex++;

            if (m_targetIndex < static_cast<int>(m_targets.size())) {
                // Walk to next target
                glm::vec3 from = m_sceneObject->getTransform().getPosition();
                glm::vec3 to = m_targets[m_targetIndex].position;
                float dist = glm::length(to - from);
                float duration = std::max(dist / MOVE_SPEED, 0.3f);
                m_sceneObject->startMoveTo(from, to, duration, true);
                m_state = ImageBotState::WALKING_TO_IMAGE;
                m_stateTimer = 0.0f;
            } else {
                // All done — walk home
                glm::vec3 from = m_sceneObject->getTransform().getPosition();
                float dist = glm::length(m_homePos - from);
                float duration = std::max(dist / MOVE_SPEED, 0.3f);
                m_sceneObject->startMoveTo(from, m_homePos, duration, true);
                m_state = ImageBotState::WALKING_HOME;
                m_stateTimer = 0.0f;
            }
        }
        break;
    }

    case ImageBotState::WALKING_HOME: {
        m_sceneObject->updateMoveTo(deltaTime);
        if (!m_sceneObject->isMovingTo()) {
            // Save work log entry
            if (!m_sessionFilesDescribed.empty()) {
                ImageBotLogEntry entry;
                entry.timestamp = currentTimestamp();
                entry.filesDescribed = m_sessionFilesDescribed;
                entry.totalDescribed = static_cast<int>(m_sessionFilesDescribed.size());
                m_workLog.push_back(std::move(entry));
                saveWorkLog();
                std::cout << "[ImageBot] Logged session: " << m_sessionFilesDescribed.size()
                          << " files described" << std::endl;
                m_sessionFilesDescribed.clear();
            }

            m_state = ImageBotState::DONE;
            m_stateTimer = 0.0f;
        }
        break;
    }

    case ImageBotState::DONE: {
        if (m_stateTimer >= DONE_DURATION) {
            m_state = ImageBotState::IDLE;
            m_stateTimer = 0.0f;
            m_targets.clear();
            m_targetIndex = 0;
        }
        break;
    }

    case ImageBotState::IDLE:
        break;
    }
}

} // namespace eden
