#include "OS/PlatformGrid.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <cstdlib>

namespace eden {

void PlatformGridBuilder::init(ModelRenderer* renderer,
                                std::vector<std::unique_ptr<SceneObject>>* sceneObjects) {
    m_renderer = renderer;
    m_sceneObjects = sceneObjects;
}

// ── Config path ────────────────────────────────────────────────────────

std::string PlatformGridBuilder::configPath(const std::string& folderPath) {
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/.config/eden/rooms";
    std::filesystem::create_directories(dir);

    // Hash folder path for unique filename
    std::hash<std::string> hasher;
    size_t h = hasher(folderPath);
    return dir + "/" + std::to_string(h) + ".json";
}

bool PlatformGridBuilder::hasConfig(const std::string& folderPath) const {
    return std::filesystem::exists(configPath(folderPath));
}

// ── Save / Load ────────────────────────────────────────────────────────

bool PlatformGridBuilder::saveConfig(const std::string& folderPath) {
    std::string path = configPath(folderPath);
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"width\": " << m_grid.width << ",\n";
    f << "  \"depth\": " << m_grid.depth << ",\n";
    f << "  \"cellSize\": " << m_grid.cellSize << ",\n";
    f << "  \"platformY\": " << m_grid.savedPlatformY << ",\n";
    f << "  \"walls\": [";
    for (int i = 0; i < static_cast<int>(m_grid.walls.size()); ++i) {
        if (i > 0) f << ",";
        f << (m_grid.walls[i] ? "1" : "0");
    }
    f << "],\n";

    // Serialize frames
    f << "  \"frames\": [";
    for (size_t i = 0; i < m_grid.frames.size(); ++i) {
        const auto& fr = m_grid.frames[i];
        if (i > 0) f << ",";
        f << "\n    {\"wx\":" << fr.worldX << ",\"wy\":" << fr.worldY
          << ",\"wz\":" << fr.worldZ << ",\"size\":" << fr.size
          << ",\"nx\":" << fr.normalX << ",\"nz\":" << fr.normalZ
          << ",\"yaw\":" << fr.yawDeg << ",\"file\":\"";
        // Escape quotes in filePath
        for (char c : fr.filePath) {
            if (c == '"') f << "\\\"";
            else if (c == '\\') f << "\\\\";
            else f << c;
        }
        f << "\",\"ft\":" << static_cast<int>(fr.frameType);
        if (!fr.machineName.empty()) {
            f << ",\"mn\":\"";
            for (char c : fr.machineName) {
                if (c == '"') f << "\\\"";
                else if (c == '\\') f << "\\\\";
                else f << c;
            }
            f << "\"";
        }
        if (!fr.paramName.empty()) {
            f << ",\"pn\":\"";
            for (char c : fr.paramName) {
                if (c == '"') f << "\\\"";
                else if (c == '\\') f << "\\\\";
                else f << c;
            }
            f << "\"";
        }
        f << "}";
    }
    f << "\n  ],\n";

    // Serialize wires
    f << "  \"wires\": [";
    for (size_t i = 0; i < m_grid.wires.size(); ++i) {
        if (i > 0) f << ",";
        f << "{\"from\":" << m_grid.wires[i].fromFrame
          << ",\"to\":" << m_grid.wires[i].toFrame << "}";
    }
    f << "]\n}\n";
    return f.good();
}

bool PlatformGridBuilder::loadConfig(const std::string& folderPath) {
    std::string path = configPath(folderPath);
    std::ifstream f(path);
    if (!f.is_open()) {
        // No saved config — reset to empty grid
        m_grid = PlatformGrid();
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Minimal JSON parse for {"width":N, "depth":N, "cellSize":F, "walls":[...]}
    auto extractInt = [&](const std::string& key) -> int {
        std::string needle = "\"" + key + "\"";
        auto pos = content.find(needle);
        if (pos == std::string::npos) return -1;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return -1;
        pos++;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        return std::stoi(content.substr(pos));
    };

    auto extractFloat = [&](const std::string& key) -> float {
        std::string needle = "\"" + key + "\"";
        auto pos = content.find(needle);
        if (pos == std::string::npos) return -1.0f;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return -1.0f;
        pos++;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        return std::stof(content.substr(pos));
    };

    int w = extractInt("width");
    int d = extractInt("depth");
    float cs = extractFloat("cellSize");
    if (w <= 0 || d <= 0 || cs <= 0.0f) {
        m_grid = PlatformGrid();
        return false;
    }

    m_grid.width = w;
    m_grid.depth = d;
    m_grid.cellSize = cs;
    m_grid.savedPlatformY = extractFloat("platformY");
    m_grid.walls.assign(w * d, false);

    // Parse walls array (first '[' in content)
    auto arrStart = content.find('[');
    if (arrStart != std::string::npos) {
        size_t pos = arrStart + 1;
        int idx = 0;
        while (pos < content.size() && idx < w * d) {
            // Skip whitespace and commas
            while (pos < content.size() && (content[pos] == ' ' || content[pos] == ',' ||
                   content[pos] == '\n' || content[pos] == '\r' || content[pos] == '\t'))
                pos++;
            if (pos >= content.size() || content[pos] == ']') break;
            m_grid.walls[idx] = (content[pos] == '1');
            idx++;
            pos++;
        }
    }

    // Parse frames array
    m_grid.frames.clear();
    auto framesKey = content.find("\"frames\"");
    if (framesKey != std::string::npos) {
        auto framesArr = content.find('[', framesKey);
        if (framesArr != std::string::npos) {
            size_t pos = framesArr + 1;
            // Parse each frame object { ... }
            while (pos < content.size()) {
                // Skip whitespace/commas
                while (pos < content.size() && (content[pos] == ' ' || content[pos] == ',' ||
                       content[pos] == '\n' || content[pos] == '\r' || content[pos] == '\t'))
                    pos++;
                if (pos >= content.size() || content[pos] == ']') break;
                if (content[pos] != '{') break;

                // Find matching '}'
                auto objEnd = content.find('}', pos);
                if (objEnd == std::string::npos) break;
                std::string obj = content.substr(pos, objEnd - pos + 1);
                pos = objEnd + 1;

                // Parse frame fields from the object string
                auto parseField = [&](const std::string& key) -> float {
                    std::string needle = "\"" + key + "\"";
                    auto p = obj.find(needle);
                    if (p == std::string::npos) return 0.0f;
                    p = obj.find(':', p);
                    if (p == std::string::npos) return 0.0f;
                    p++;
                    while (p < obj.size() && obj[p] == ' ') p++;
                    return std::stof(obj.substr(p));
                };

                WallFrame fr;
                fr.worldX = parseField("wx");
                fr.worldY = parseField("wy");
                fr.worldZ = parseField("wz");
                fr.size = static_cast<int>(parseField("size"));
                fr.normalX = parseField("nx");
                fr.normalZ = parseField("nz");
                fr.yawDeg = parseField("yaw");

                // Parse "file" string
                auto fileKey = obj.find("\"file\"");
                if (fileKey != std::string::npos) {
                    auto q1 = obj.find('"', obj.find(':', fileKey) + 1);
                    if (q1 != std::string::npos) {
                        std::string fp;
                        for (size_t fi = q1 + 1; fi < obj.size(); fi++) {
                            if (obj[fi] == '\\' && fi + 1 < obj.size()) {
                                fp += obj[fi + 1];
                                fi++;
                            } else if (obj[fi] == '"') {
                                break;
                            } else {
                                fp += obj[fi];
                            }
                        }
                        fr.filePath = fp;
                    }
                }

                // Parse frame type
                fr.frameType = static_cast<FrameType>(static_cast<int>(parseField("ft")));

                // Parse machineName string
                auto mnKey = obj.find("\"mn\"");
                if (mnKey != std::string::npos) {
                    auto mq1 = obj.find('"', obj.find(':', mnKey) + 1);
                    if (mq1 != std::string::npos) {
                        std::string mn;
                        for (size_t mi = mq1 + 1; mi < obj.size(); mi++) {
                            if (obj[mi] == '\\' && mi + 1 < obj.size()) { mn += obj[mi + 1]; mi++; }
                            else if (obj[mi] == '"') break;
                            else mn += obj[mi];
                        }
                        fr.machineName = mn;
                    }
                }

                // Parse paramName string
                auto pnKey = obj.find("\"pn\"");
                if (pnKey != std::string::npos) {
                    auto pq1 = obj.find('"', obj.find(':', pnKey) + 1);
                    if (pq1 != std::string::npos) {
                        std::string pn;
                        for (size_t pi = pq1 + 1; pi < obj.size(); pi++) {
                            if (obj[pi] == '\\' && pi + 1 < obj.size()) { pn += obj[pi + 1]; pi++; }
                            else if (obj[pi] == '"') break;
                            else pn += obj[pi];
                        }
                        fr.paramName = pn;
                    }
                }

                m_grid.frames.push_back(fr);
            }
        }
    }

    // Parse wires array
    m_grid.wires.clear();
    auto wiresKey = content.find("\"wires\"");
    if (wiresKey != std::string::npos) {
        auto wiresArr = content.find('[', wiresKey);
        if (wiresArr != std::string::npos) {
            size_t pos = wiresArr + 1;
            while (pos < content.size()) {
                while (pos < content.size() && (content[pos] == ' ' || content[pos] == ',' ||
                       content[pos] == '\n' || content[pos] == '\r' || content[pos] == '\t'))
                    pos++;
                if (pos >= content.size() || content[pos] == ']') break;
                if (content[pos] != '{') break;
                auto objEnd = content.find('}', pos);
                if (objEnd == std::string::npos) break;
                std::string obj = content.substr(pos, objEnd - pos + 1);
                pos = objEnd + 1;

                auto parseWF = [&](const std::string& key) -> int {
                    std::string needle = "\"" + key + "\"";
                    auto p = obj.find(needle);
                    if (p == std::string::npos) return -1;
                    p = obj.find(':', p);
                    if (p == std::string::npos) return -1;
                    p++;
                    while (p < obj.size() && obj[p] == ' ') p++;
                    return std::stoi(obj.substr(p));
                };

                WireConnection wire;
                wire.fromFrame = parseWF("from");
                wire.toFrame = parseWF("to");
                if (wire.fromFrame >= 0 && wire.toFrame >= 0) {
                    m_grid.wires.push_back(wire);
                }
            }
        }
    }

    return true;
}

// ── Spawn platform slab ────────────────────────────────────────────────

void PlatformGridBuilder::spawnPlatform(const glm::vec3& center, float baseY,
                                         float holeHalfSize, float wallHeight) {
    if (!m_renderer || !m_sceneObjects) return;

    // Remove existing platform slab if any
    if (m_platformSlab) {
        auto it = std::find_if(m_sceneObjects->begin(), m_sceneObjects->end(),
            [this](const auto& p) { return p.get() == m_platformSlab; });
        if (it != m_sceneObjects->end()) {
            uint32_t h = (*it)->getBufferHandle();
            m_sceneObjects->erase(it);
            if (h) m_renderer->destroyModel(h);
        }
        m_platformSlab = nullptr;
    }

    float platW = m_grid.width * m_grid.cellSize;
    float platD = m_grid.depth * m_grid.cellSize;
    glm::vec4 platColor{0.2f, 0.2f, 0.22f, 1.0f};

    // Spawn 4 strips around the center hole to create the platform with a hole
    // Hole is centered on the platform
    float halfW = platW / 2.0f;
    float halfD = platD / 2.0f;
    float hh = holeHalfSize; // half-size of hole

    struct Strip {
        glm::vec3 pos;
        glm::vec3 scale;
    };

    // 4 rectangular strips forming a frame with a gap in the center
    std::vector<Strip> strips;

    // North strip (full width, from hole edge to north edge)
    if (halfD > hh) {
        float stripDepth = halfD - hh;
        strips.push_back({{center.x, baseY, center.z + hh + stripDepth / 2.0f},
                           {platW, 1.0f, stripDepth}});
    }
    // South strip
    if (halfD > hh) {
        float stripDepth = halfD - hh;
        strips.push_back({{center.x, baseY, center.z - hh - stripDepth / 2.0f},
                           {platW, 1.0f, stripDepth}});
    }
    // East strip (between hole edges in Z, from hole edge to east edge in X)
    if (halfW > hh) {
        float stripWidth = halfW - hh;
        float stripDepth = hh * 2.0f;
        strips.push_back({{center.x + hh + stripWidth / 2.0f, baseY, center.z},
                           {stripWidth, 1.0f, stripDepth}});
    }
    // West strip
    if (halfW > hh) {
        float stripWidth = halfW - hh;
        float stripDepth = hh * 2.0f;
        strips.push_back({{center.x - hh - stripWidth / 2.0f, baseY, center.z},
                           {stripWidth, 1.0f, stripDepth}});
    }

    int panelNum = 0;
    for (auto& strip : strips) {
        auto mesh = PrimitiveMeshBuilder::createCube(1.0f, platColor);
        uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices);

        auto obj = std::make_unique<SceneObject>("FSPlatform_" + std::to_string(panelNum++));
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(1.0f);
        obj->setPrimitiveColor(platColor);
        obj->setBuildingType("platform_slab");
        obj->setAABBCollision(true);
        obj->getTransform().setPosition(strip.pos);
        obj->getTransform().setScale(strip.scale);

        if (panelNum == 1) m_platformSlab = obj.get(); // track first strip
        m_sceneObjects->push_back(std::move(obj));
    }
}

// ── Build walls from grid ──────────────────────────────────────────────

void PlatformGridBuilder::buildWallsFromGrid(const glm::vec3& center, float baseY,
                                              float wallHeight, const glm::vec4& wallColor) {
    if (!m_renderer || !m_sceneObjects) return;

    clearWalls();

    float platW = m_grid.width * m_grid.cellSize;
    float platD = m_grid.depth * m_grid.cellSize;
    float originX = center.x - platW / 2.0f;
    float originZ = center.z - platD / 2.0f;

    // Greedy horizontal merge: scan each row, merge adjacent wall cells into
    // single long wall segments to reduce object count
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, wallColor);

    int wallNum = 0;
    for (int z = 0; z < m_grid.depth; ++z) {
        int x = 0;
        while (x < m_grid.width) {
            if (!m_grid.getCell(x, z)) { x++; continue; }

            // Find run of adjacent wall cells in this row
            int startX = x;
            while (x < m_grid.width && m_grid.getCell(x, z)) x++;
            int runLen = x - startX;

            // Spawn a single wall segment for the run
            float segW = runLen * m_grid.cellSize;
            float segCenterX = originX + (startX + runLen / 2.0f) * m_grid.cellSize;
            float segCenterZ = originZ + (z + 0.5f) * m_grid.cellSize;

            uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices);

            auto obj = std::make_unique<SceneObject>(
                "FSPlatWall_" + std::to_string(wallNum++));
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
            obj->setLocalBounds(mesh.bounds);
            obj->setMeshData(mesh.vertices, mesh.indices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveSize(1.0f);
            obj->setPrimitiveColor(wallColor);
            obj->setBuildingType("platform_wall");
            obj->setAABBCollision(true);
            // Tag as wall so hotbar drops work
            obj->setDescription("wall_other");

            obj->getTransform().setPosition({segCenterX, baseY + 1.0f, segCenterZ});
            obj->getTransform().setScale({segW, wallHeight, m_grid.cellSize});

            SceneObject* raw = obj.get();
            m_sceneObjects->push_back(std::move(obj));
            m_spawnedWalls.push_back(raw);
        }
    }
}

// ── Clear walls ────────────────────────────────────────────────────────

void PlatformGridBuilder::clearWalls() {
    if (!m_renderer || !m_sceneObjects) return;

    std::vector<uint32_t> handles;
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (*it && (*it)->getBuildingType() == "platform_wall") {
            uint32_t h = (*it)->getBufferHandle();
            if (h) handles.push_back(h);
            it = m_sceneObjects->erase(it);
        } else {
            ++it;
        }
    }
    if (!handles.empty()) m_renderer->destroyModels(handles);
    m_spawnedWalls.clear();
}

// ── Frame objects ──────────────────────────────────────────────────────

void PlatformGridBuilder::spawnFrameObjects(const glm::vec3& center, float baseY) {
    if (!m_renderer || !m_sceneObjects) return;

    clearFrames();

    int frameNum = 0;
    for (auto& fr : m_grid.frames) {
        // Skip frames that have a file placed on them
        if (!fr.filePath.empty()) continue;

        // Color-code by frame type
        glm::vec4 frameColor;
        switch (fr.frameType) {
            case FrameType::Machine:  frameColor = {1.0f, 0.5f, 0.0f, 0.9f}; break; // orange
            case FrameType::Input:    frameColor = {0.2f, 0.5f, 1.0f, 0.9f}; break; // blue
            case FrameType::Output:   frameColor = {1.0f, 0.85f, 0.0f, 0.9f}; break; // gold
            case FrameType::Button:   frameColor = {1.0f, 0.2f, 0.2f, 0.9f}; break; // red
            case FrameType::Checkbox: frameColor = {0.2f, 0.8f, 0.2f, 0.9f}; break; // green
            case FrameType::Slider:   frameColor = {0.6f, 0.2f, 0.8f, 0.9f}; break; // purple
            case FrameType::Log:      frameColor = {0.5f, 0.5f, 0.5f, 0.9f}; break; // gray
            default:                  frameColor = {0.3f, 0.5f, 0.7f, 0.9f}; break; // blue-gray
        }
        auto mesh = PrimitiveMeshBuilder::createCube(1.0f, frameColor);

        uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices);

        auto obj = std::make_unique<SceneObject>(
            "FSWallFrame_" + std::to_string(frameNum++));
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(1.0f);
        obj->setPrimitiveColor(frameColor);
        obj->setBuildingType("wall_frame");
        obj->setAABBCollision(false);

        // Position flush against wall surface
        float s = static_cast<float>(fr.size);
        obj->getTransform().setPosition({fr.worldX, fr.worldY, fr.worldZ});
        obj->getTransform().setScale({s, s, 0.1f});
        obj->setEulerRotation({0.0f, fr.yawDeg, 0.0f});

        SceneObject* raw = obj.get();
        m_sceneObjects->push_back(std::move(obj));
        m_spawnedFrames.push_back(raw);
    }
}

void PlatformGridBuilder::clearFrames() {
    if (!m_renderer || !m_sceneObjects) return;

    std::vector<uint32_t> handles;
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (*it && (*it)->getBuildingType() == "wall_frame") {
            uint32_t h = (*it)->getBufferHandle();
            if (h) handles.push_back(h);
            it = m_sceneObjects->erase(it);
        } else {
            ++it;
        }
    }
    if (!handles.empty()) m_renderer->destroyModels(handles);
    m_spawnedFrames.clear();
}

void PlatformGridBuilder::clearAll() {
    clearWalls();
    clearFrames();

    if (!m_renderer || !m_sceneObjects) return;

    // Remove platform slab pieces
    std::vector<uint32_t> handles;
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (*it && (*it)->getBuildingType() == "platform_slab") {
            uint32_t h = (*it)->getBufferHandle();
            if (h) handles.push_back(h);
            it = m_sceneObjects->erase(it);
        } else {
            ++it;
        }
    }
    if (!handles.empty()) m_renderer->destroyModels(handles);
    m_platformSlab = nullptr;
}

// ── Load + Build ───────────────────────────────────────────────────────

void PlatformGridBuilder::loadAndBuild(const std::string& folderPath,
                                        const glm::vec3& center, float baseY,
                                        float wallHeight, const glm::vec4& wallColor,
                                        int defaultSize) {
    if (!loadConfig(folderPath)) {
        // No saved config — use the default size
        m_grid.resize(defaultSize, defaultSize);
    }

    // Platform Y is fixed — just record it for reference
    m_grid.savedPlatformY = baseY;

    // Always spawn platform slab
    float holeHalfSize = 8.0f; // matching basement door opening half-width
    spawnPlatform(center, baseY, holeHalfSize, wallHeight);

    // Build walls if any are set
    bool hasWalls = false;
    for (bool w : m_grid.walls) {
        if (w) { hasWalls = true; break; }
    }
    if (hasWalls) {
        buildWallsFromGrid(center, baseY, wallHeight, wallColor);
    }

    // Spawn wall frame objects (empty frames only — occupied ones get files via FilesystemBrowser)
    if (!m_grid.frames.empty()) {
        spawnFrameObjects(center, baseY);
    }
}

} // namespace eden
