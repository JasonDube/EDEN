#include "OS/FilesystemBrowser.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Editor/GLBLoader.hpp"
#include "Editor/LimeLoader.hpp"
#include "Terminal/EdenTerminalFont.inc"

#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <functional>

namespace eden {

// ── Shell escape ───────────────────────────────────────────────────────

static std::string shellEscape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    result += "'";
    return result;
}

// ── Standalone OBJ loader ──────────────────────────────────────────────

static bool loadOBJ(const std::string& filepath,
                    std::vector<ModelVertex>& outVertices,
                    std::vector<uint32_t>& outIndices) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::string line;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "v") {
            glm::vec3 p;
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (token == "vn") {
            glm::vec3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (token == "f") {
            // Collect face vertex indices
            std::vector<uint32_t> faceVerts;
            std::string vertStr;
            while (iss >> vertStr) {
                // Format: v, v/vt, v/vt/vn, v//vn
                int vi = 0, ni = 0;
                if (auto slash1 = vertStr.find('/'); slash1 != std::string::npos) {
                    vi = std::stoi(vertStr.substr(0, slash1));
                    auto slash2 = vertStr.find('/', slash1 + 1);
                    if (slash2 != std::string::npos && slash2 + 1 < vertStr.size()) {
                        ni = std::stoi(vertStr.substr(slash2 + 1));
                    }
                } else {
                    vi = std::stoi(vertStr);
                }

                // OBJ indices are 1-based
                if (vi < 0) vi = static_cast<int>(positions.size()) + vi + 1;

                ModelVertex mv{};
                if (vi > 0 && vi <= static_cast<int>(positions.size()))
                    mv.position = positions[vi - 1];
                if (ni > 0 && ni <= static_cast<int>(normals.size()))
                    mv.normal = normals[ni - 1];
                mv.color = {0.8f, 0.8f, 0.8f, 1.0f};
                mv.texCoord = {0.0f, 0.0f};

                uint32_t idx = static_cast<uint32_t>(outVertices.size());
                outVertices.push_back(mv);
                faceVerts.push_back(idx);
            }

            // Fan-triangulate n-gon
            for (size_t i = 2; i < faceVerts.size(); ++i) {
                outIndices.push_back(faceVerts[0]);
                outIndices.push_back(faceVerts[i - 1]);
                outIndices.push_back(faceVerts[i]);
            }
        }
    }

    return !outVertices.empty() && !outIndices.empty();
}

// ── Disk cache ─────────────────────────────────────────────────────────

std::string FilesystemBrowser::getCachePath(const std::string& videoPath) {
    std::hash<std::string> hasher;
    size_t h = hasher(videoPath);
    std::string cacheDir;
    const char* home = getenv("HOME");
    if (home) {
        cacheDir = std::string(home) + "/.cache/eden/video_thumbs";
    } else {
        cacheDir = "/tmp/eden_video_thumbs";
    }
    std::filesystem::create_directories(cacheDir);
    return cacheDir + "/" + std::to_string(h) + ".bin";
}

bool FilesystemBrowser::loadCachedFrames(const std::string& cachePath,
                                          std::vector<std::vector<unsigned char>>& frames,
                                          int frameSize) {
    std::ifstream f(cachePath, std::ios::binary);
    if (!f.is_open()) return false;

    uint32_t frameCount, frameByteSize;
    f.read(reinterpret_cast<char*>(&frameCount), 4);
    f.read(reinterpret_cast<char*>(&frameByteSize), 4);

    if (!f || frameCount == 0 || frameCount > 100 ||
        frameByteSize != static_cast<uint32_t>(frameSize * frameSize * 4)) {
        return false;
    }

    frames.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        frames[i].resize(frameByteSize);
        f.read(reinterpret_cast<char*>(frames[i].data()), frameByteSize);
        if (!f) { frames.clear(); return false; }
    }

    return true;
}

bool FilesystemBrowser::saveCachedFrames(const std::string& cachePath,
                                          const std::vector<std::vector<unsigned char>>& frames,
                                          int frameSize) {
    std::ofstream f(cachePath, std::ios::binary);
    if (!f.is_open()) return false;

    uint32_t frameCount = static_cast<uint32_t>(frames.size());
    uint32_t frameByteSize = static_cast<uint32_t>(frameSize * frameSize * 4);
    f.write(reinterpret_cast<const char*>(&frameCount), 4);
    f.write(reinterpret_cast<const char*>(&frameByteSize), 4);

    for (const auto& frame : frames) {
        f.write(reinterpret_cast<const char*>(frame.data()), frameByteSize);
    }

    return f.good();
}

// ── Background extraction worker ───────────────────────────────────────

void FilesystemBrowser::extractionWorker(
        std::string filePath, std::string cachePath,
        int labelSize, int maxFrames,
        std::shared_ptr<std::vector<std::vector<unsigned char>>> outFrames,
        std::shared_ptr<std::atomic<bool>> ready,
        std::shared_ptr<std::atomic<bool>> cancelled) {

    size_t frameBytes = labelSize * labelSize * 4;

    std::string escaped = shellEscape(filePath);
    std::string cmd = "ffmpeg -i " + escaped +
                      " -vf 'fps=2,scale=" + std::to_string(labelSize) + ":" + std::to_string(labelSize) +
                      ":force_original_aspect_ratio=decrease,pad=" +
                      std::to_string(labelSize) + ":" + std::to_string(labelSize) +
                      ":(ow-iw)/2:(oh-ih)/2:color=black'" +
                      " -f rawvideo -pix_fmt rgba pipe:1 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { ready->store(true); return; }

    std::vector<std::vector<unsigned char>> frames;
    std::vector<unsigned char> frameBuf(frameBytes);

    while (frames.size() < static_cast<size_t>(maxFrames)) {
        if (cancelled->load()) break;

        size_t totalRead = 0;
        while (totalRead < frameBytes) {
            size_t n = fread(frameBuf.data() + totalRead, 1, frameBytes - totalRead, pipe);
            if (n == 0) break;
            totalRead += n;
        }

        if (totalRead != frameBytes) break;

        std::vector<unsigned char> flipped(frameBytes);
        for (int y = 0; y < labelSize; ++y) {
            int srcRow = (labelSize - 1 - y) * labelSize * 4;
            int dstRow = y * labelSize * 4;
            memcpy(flipped.data() + dstRow, frameBuf.data() + srcRow, labelSize * 4);
        }
        frames.push_back(std::move(flipped));
    }

    pclose(pipe);

    if (!cancelled->load() && !frames.empty()) {
        saveCachedFrames(cachePath, frames, labelSize);
        *outFrames = std::move(frames);
    }

    ready->store(true);
}

void FilesystemBrowser::cancelAllExtractions() {
    if (m_cancelExtraction) {
        m_cancelExtraction->store(true);
    }
    for (auto& t : m_extractionThreads) {
        if (t.joinable()) t.join();
    }
    m_extractionThreads.clear();
    m_pendingExtractions.clear();
    m_cancelExtraction.reset();
}

// ── Folder visit tracking (attention system) ───────────────────────────

void FilesystemBrowser::loadFolderVisits() {
    m_folderVisits.clear();
    const char* home = getenv("HOME");
    if (!home) return;

    std::string path = std::string(home) + "/.config/eden/folder_visits.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    // Minimal JSON parser for {"path": count, ...} format
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Parse key-value pairs from the JSON object
    size_t pos = 0;
    while ((pos = content.find('"', pos)) != std::string::npos) {
        size_t keyStart = pos + 1;
        size_t keyEnd = content.find('"', keyStart);
        if (keyEnd == std::string::npos) break;

        std::string key = content.substr(keyStart, keyEnd - keyStart);

        // Find the colon then the number
        size_t colon = content.find(':', keyEnd);
        if (colon == std::string::npos) break;

        // Skip whitespace after colon
        size_t numStart = colon + 1;
        while (numStart < content.size() && (content[numStart] == ' ' || content[numStart] == '\t'))
            numStart++;

        // Read the integer
        size_t numEnd = numStart;
        while (numEnd < content.size() && (content[numEnd] >= '0' && content[numEnd] <= '9'))
            numEnd++;

        if (numEnd > numStart) {
            int count = std::stoi(content.substr(numStart, numEnd - numStart));
            m_folderVisits[key] = count;
        }

        pos = numEnd;
    }
}

void FilesystemBrowser::saveFolderVisits() {
    const char* home = getenv("HOME");
    if (!home) return;

    std::string dir = std::string(home) + "/.config/eden";
    std::filesystem::create_directories(dir);

    std::string path = dir + "/folder_visits.json";
    std::ofstream f(path);
    if (!f.is_open()) return;

    f << "{\n";
    bool first = true;
    for (const auto& [folder, count] : m_folderVisits) {
        if (!first) f << ",\n";
        // Escape backslashes and quotes in path
        std::string escaped;
        for (char c : folder) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        f << "  \"" << escaped << "\": " << count;
        first = false;
    }
    f << "\n}\n";
}

void FilesystemBrowser::recordVisit(const std::string& path) {
    m_folderVisits[path]++;
    saveFolderVisits();
}

float FilesystemBrowser::getVisitGlow(const std::string& path) const {
    auto it = m_folderVisits.find(path);
    if (it == m_folderVisits.end() || it->second == 0) return 0.0f;

    // Find max visit count
    int maxVisits = 0;
    for (const auto& [_, count] : m_folderVisits) {
        if (count > maxVisits) maxVisits = count;
    }

    if (maxVisits == 0) return 0.0f;
    return static_cast<float>(it->second) / static_cast<float>(maxVisits);
}

// ── Init / Navigate / Clear ────────────────────────────────────────────

FilesystemBrowser::~FilesystemBrowser() {
    cancelSegmentation();
    cancelAllExtractions();
}

void FilesystemBrowser::init(ModelRenderer* modelRenderer,
                             std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                             Terrain* terrain) {
    m_modelRenderer = modelRenderer;
    m_sceneObjects = sceneObjects;
    m_terrain = terrain;
    loadFolderVisits();
}

void FilesystemBrowser::navigate(const std::string& path) {
    m_pendingPath = path;
    m_pendingNavigation = true;
}

void FilesystemBrowser::processNavigation() {
    if (!m_pendingNavigation) return;
    m_pendingNavigation = false;

    // Try spawning into the new path before clearing old objects
    m_spawnFailed = false;

    // Temporarily test if the new path works
    std::string oldPath = m_currentPath;
    clearFilesystemObjects();
    spawnObjects(m_pendingPath);

    if (m_spawnFailed) {
        // Navigation failed — go back to old path
        std::cerr << "[FilesystemBrowser] Navigation failed, staying in: " << oldPath << std::endl;
        spawnObjects(oldPath);
        m_currentPath = oldPath;
    } else {
        m_currentPath = m_pendingPath;
        recordVisit(m_currentPath);
    }
    m_active = true;
}

void FilesystemBrowser::updateAnimations(float deltaTime) {
    if (!m_modelRenderer) return;

    // Drain finished threads and launch queued extractions
    {
        auto it = m_extractionThreads.begin();
        while (it != m_extractionThreads.end()) {
            // Check if thread is done by trying to join with no wait isn't possible,
            // so we track via the ready flags in the animations instead.
            // Just clean up joinable finished threads.
            ++it;
        }
        // Launch pending extractions as slots free up
        while (!m_pendingExtractions.empty() &&
               static_cast<int>(m_extractionThreads.size()) < MAX_CONCURRENT_EXTRACTIONS) {
            auto& pe = m_pendingExtractions.front();
            m_extractionThreads.emplace_back(
                extractionWorker,
                pe.filePath, pe.cachePath,
                LABEL_SIZE, MAX_VIDEO_FRAMES,
                pe.outFrames, pe.ready, m_cancelExtraction);
            m_pendingExtractions.erase(m_pendingExtractions.begin());
        }
    }

    for (auto& anim : m_videoAnimations) {
        if (!anim.loaded && anim.ready && anim.ready->load()) {
            if (anim.pendingFrames && !anim.pendingFrames->empty()) {
                anim.frames = std::move(*anim.pendingFrames);
                anim.currentFrame = 0;
                anim.timer = 0.0f;
                m_modelRenderer->updateTexture(
                    anim.bufferHandle,
                    anim.frames[0].data(),
                    LABEL_SIZE, LABEL_SIZE);
            }
            anim.loaded = true;
            anim.pendingFrames.reset();
            anim.ready.reset();
        }
    }

    // Staggered texture updates — only update one video per frame (round-robin)
    if (!m_videoAnimations.empty()) {
        size_t count = m_videoAnimations.size();
        for (size_t i = 0; i < count; i++) {
            size_t idx = (m_videoUpdateIndex + i) % count;
            auto& anim = m_videoAnimations[idx];
            if (anim.frames.size() <= 1) continue;

            anim.timer += deltaTime;
            if (anim.timer >= VIDEO_FRAME_INTERVAL) {
                anim.timer -= VIDEO_FRAME_INTERVAL;
                anim.currentFrame = (anim.currentFrame + 1) % static_cast<int>(anim.frames.size());
                m_modelRenderer->updateTexture(
                    anim.bufferHandle,
                    anim.frames[anim.currentFrame].data(),
                    LABEL_SIZE, LABEL_SIZE);
                m_videoUpdateIndex = (idx + 1) % count;
                break;  // Only one texture upload per frame
            }
        }
    }

    // Update cleaner bot state machine
    m_cleanerBot.update(deltaTime);

    // Update image bot state machine
    m_imageBot.update(deltaTime);

    // Update cull session
    m_cullSession.update(deltaTime);

    // Spin 3D model objects on their turntable
    for (auto& spin : m_modelSpins) {
        if (!spin.obj) continue;
        spin.angle += MODEL_SPIN_SPEED * deltaTime;
        if (spin.angle >= 360.0f) spin.angle -= 360.0f;
        spin.obj->setEulerRotation({0.0f, spin.baseYaw + spin.angle, 0.0f});
    }

    // Emanation system: spawn expanding wireframe rings from hot folders
    for (auto& em : m_emanations) {
        em.timer += deltaTime;
        // Spawn rate scales with intensity (hot folders emit faster)
        float interval = EMANATION_SPAWN_INTERVAL / em.intensity;
        while (em.timer >= interval) {
            em.timer -= interval;
            size_t idx = static_cast<size_t>(&em - &m_emanations[0]);
            m_emanationRings.push_back({idx, 0.0f});
        }
    }

    // Age rings and remove expired ones
    float maxAge = EMANATION_MAX_DIST / EMANATION_SPEED;
    for (auto& ring : m_emanationRings) {
        ring.age += deltaTime;
    }
    m_emanationRings.erase(
        std::remove_if(m_emanationRings.begin(), m_emanationRings.end(),
            [maxAge](const EmanationRing& r) { return r.age >= maxAge; }),
        m_emanationRings.end());

    // Poll segmentation background task
    pollSegmentation();
}

void FilesystemBrowser::clearFilesystemObjects() {
    if (!m_sceneObjects || !m_modelRenderer) return;

    m_cleanerBot.despawn();
    m_imageBot.despawn();
    m_cullSession.despawn();
    m_forgeRoom.despawn();

    cancelSegmentation();
    cancelAllExtractions();
    m_videoAnimations.clear();
    m_modelSpins.clear();
    m_emanations.clear();
    m_emanationRings.clear();

    // Collect all handles first, then batch destroy (single waitIdle)
    std::vector<uint32_t> handles;
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        const auto& bt = (*it) ? (*it)->getBuildingType() : "";
        if (*it && (bt == "filesystem" || bt == "filesystem_wall" || bt == "image_desc")) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) {
                handles.push_back(handle);
            }
            it = m_sceneObjects->erase(it);
        } else {
            ++it;
        }
    }
    m_modelRenderer->destroyModels(handles);
}

// ── Categorize ─────────────────────────────────────────────────────────

FilesystemBrowser::FileCategory FilesystemBrowser::categorize(
        const std::filesystem::directory_entry& entry) const {
    if (entry.is_directory()) return FileCategory::Folder;

    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" ||
        ext == ".bmp" || ext == ".gif")
        return FileCategory::Image;

    if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".webm" ||
        ext == ".mov" || ext == ".flv" || ext == ".wmv")
        return FileCategory::Video;

    if (ext == ".lime" || ext == ".obj" || ext == ".glb" || ext == ".gltf")
        return FileCategory::Model3D;

    if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".yaml" ||
        ext == ".yml" || ext == ".toml" || ext == ".cfg" || ext == ".ini")
        return FileCategory::Text;

    if (ext == ".cpp" || ext == ".hpp" || ext == ".c" || ext == ".h" ||
        ext == ".py" || ext == ".rs" || ext == ".js" || ext == ".ts" ||
        ext == ".java" || ext == ".go" || ext == ".lua" || ext == ".sh")
        return FileCategory::SourceCode;

    try {
        auto perms = entry.status().permissions();
        if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
            return FileCategory::Executable;
    } catch (...) {}

    return FileCategory::Other;
}

glm::vec4 FilesystemBrowser::colorForCategory(FileCategory cat) {
    switch (cat) {
        case FileCategory::Folder:     return {0.3f, 0.5f, 1.0f, 1.0f};
        case FileCategory::Image:      return {1.0f, 0.6f, 0.8f, 1.0f};
        case FileCategory::Video:      return {0.8f, 0.2f, 0.8f, 1.0f};
        case FileCategory::Text:       return {0.9f, 0.85f, 0.7f, 1.0f};
        case FileCategory::Executable: return {0.3f, 0.9f, 0.3f, 1.0f};
        case FileCategory::SourceCode: return {1.0f, 0.8f, 0.2f, 1.0f};
        case FileCategory::Model3D:   return {0.2f, 0.9f, 0.9f, 1.0f};
        default:                       return {0.6f, 0.6f, 0.6f, 1.0f};
    }
}

// ── Spawn One Object (shared helper) ───────────────────────────────────

void FilesystemBrowser::spawnOneObject(const EntryInfo& entry, size_t index,
                                        const glm::vec3& pos, const glm::vec3& scale,
                                        float yawDegrees) {
    glm::vec4 color = colorForCategory(entry.category);

    PrimitiveMeshBuilder::MeshData meshData;
    PrimitiveType primType;
    bool loadedModel = false;

    if (entry.category == FileCategory::Model3D) {
        // Try to load actual 3D model geometry
        std::string ext = std::filesystem::path(entry.fullPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::vector<ModelVertex> modelVerts;
        std::vector<uint32_t> modelIndices;
        bool hasModelTex = false;
        std::vector<unsigned char> modelTexData;
        int modelTexW = 0, modelTexH = 0;

        if (ext == ".glb" || ext == ".gltf") {
            auto result = GLBLoader::load(entry.fullPath);
            if (result.success && !result.meshes.empty()) {
                // Merge all meshes
                for (auto& m : result.meshes) {
                    uint32_t baseIdx = static_cast<uint32_t>(modelVerts.size());
                    modelVerts.insert(modelVerts.end(), m.vertices.begin(), m.vertices.end());
                    for (auto idx : m.indices) modelIndices.push_back(baseIdx + idx);
                    if (!hasModelTex && m.hasTexture) {
                        modelTexData = m.texture.data;
                        modelTexW = m.texture.width;
                        modelTexH = m.texture.height;
                        hasModelTex = true;
                    }
                }
            }
        } else if (ext == ".lime") {
            auto result = LimeLoader::load(entry.fullPath);
            if (result.success) {
                modelVerts = std::move(result.mesh.vertices);
                modelIndices = std::move(result.mesh.indices);
                if (result.mesh.hasTexture) {
                    modelTexData = std::move(result.mesh.textureData);
                    modelTexW = result.mesh.textureWidth;
                    modelTexH = result.mesh.textureHeight;
                    hasModelTex = true;
                }
            }
        } else if (ext == ".obj") {
            loadOBJ(entry.fullPath, modelVerts, modelIndices);
        }

        if (!modelVerts.empty() && !modelIndices.empty()) {
            // Normalize: compute AABB, center at origin, scale to fit 1.5 units
            glm::vec3 bmin(FLT_MAX), bmax(-FLT_MAX);
            for (auto& v : modelVerts) {
                bmin = glm::min(bmin, v.position);
                bmax = glm::max(bmax, v.position);
            }
            glm::vec3 center = (bmin + bmax) * 0.5f;
            for (auto& v : modelVerts) v.position -= center;
            bmin -= center; bmax -= center;

            float maxExtent = std::max({bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z});
            if (maxExtent > 0.0f) {
                float scaleFactor = 1.5f / maxExtent;
                for (auto& v : modelVerts) v.position *= scaleFactor;
                bmin *= scaleFactor; bmax *= scaleFactor;
            }

            meshData.vertices = std::move(modelVerts);
            meshData.indices = std::move(modelIndices);
            meshData.bounds = {bmin, bmax};
            primType = PrimitiveType::Cube;
            loadedModel = true;

            // Upload with model's own texture or no texture
            uint32_t handle;
            if (hasModelTex && !modelTexData.empty()) {
                handle = m_modelRenderer->createModel(
                    meshData.vertices, meshData.indices,
                    modelTexData.data(), modelTexW, modelTexH);
            } else {
                handle = m_modelRenderer->createModel(
                    meshData.vertices, meshData.indices, nullptr, 0, 0);
            }

            auto obj = std::make_unique<SceneObject>(
                "FSFile_" + entry.name);

            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);
            obj->setModelPath("");
            obj->setMeshData(meshData.vertices, meshData.indices);

            obj->setPrimitiveType(primType);
            obj->setPrimitiveSize(1.5f);
            obj->setPrimitiveColor(color);

            obj->setBuildingType("filesystem");
            obj->setDescription(entry.name);
            obj->setTargetLevel("fs://" + entry.fullPath);

            obj->getTransform().setPosition(pos);
            obj->getTransform().setScale(scale);

            if (yawDegrees != 0.0f) {
                obj->setEulerRotation({0.0f, yawDegrees, 0.0f});
            }

            // Register turntable spin animation
            SceneObject* rawPtr = obj.get();
            m_sceneObjects->push_back(std::move(obj));
            m_modelSpins.push_back({rawPtr, yawDegrees, 0.0f});
            return; // Done — model loaded successfully
        }
        // If loading failed, fall through to colored cube with label
    }

    if (entry.category == FileCategory::Folder) {
        meshData = PrimitiveMeshBuilder::createCube(2.0f, color);
        primType = PrimitiveType::Door;
    } else {
        meshData = PrimitiveMeshBuilder::createCube(1.5f, color);
        primType = PrimitiveType::Cube;
    }

    // Prepare texture
    std::vector<unsigned char> texPixels;
    int texW = LABEL_SIZE, texH = LABEL_SIZE;
    bool usedMedia = false;

    if (entry.category == FileCategory::Image) {
        int imgW, imgH, imgChannels;
        unsigned char* data = stbi_load(entry.fullPath.c_str(), &imgW, &imgH, &imgChannels, 4);
        if (data) {
            texPixels.resize(LABEL_SIZE * LABEL_SIZE * 4);
            for (int py = 0; py < LABEL_SIZE; ++py) {
                int srcY = (LABEL_SIZE - 1 - py) * imgH / LABEL_SIZE;
                for (int px = 0; px < LABEL_SIZE; ++px) {
                    int srcX = px * imgW / LABEL_SIZE;
                    int srcIdx = (srcY * imgW + srcX) * 4;
                    int dstIdx = (py * LABEL_SIZE + px) * 4;
                    texPixels[dstIdx + 0] = data[srcIdx + 0];
                    texPixels[dstIdx + 1] = data[srcIdx + 1];
                    texPixels[dstIdx + 2] = data[srcIdx + 2];
                    texPixels[dstIdx + 3] = data[srcIdx + 3];
                }
            }
            stbi_image_free(data);
            usedMedia = true;
        }
    } else if (entry.category == FileCategory::Video) {
        std::string cachePath = getCachePath(entry.fullPath);
        std::vector<std::vector<unsigned char>> cachedFrames;
        if (loadCachedFrames(cachePath, cachedFrames, LABEL_SIZE)) {
            texPixels = cachedFrames[0];
            usedMedia = true;
        }
    }

    if (!usedMedia) {
        renderLabel(texPixels, entry.name, entry.category, color);
    }

    uint32_t handle = m_modelRenderer->createModel(
        meshData.vertices, meshData.indices,
        texPixels.data(), texW, texH);

    auto obj = std::make_unique<SceneObject>(
        entry.category == FileCategory::Folder ? "FSDoor_" + entry.name : "FSFile_" + entry.name);

    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
    obj->setLocalBounds(meshData.bounds);
    obj->setModelPath("");
    obj->setMeshData(meshData.vertices, meshData.indices);

    obj->setPrimitiveType(primType);
    obj->setPrimitiveSize(entry.category == FileCategory::Folder ? 2.0f : 1.5f);
    obj->setPrimitiveColor(color);

    obj->setBuildingType("filesystem");
    obj->setDescription(entry.name);

    if (entry.category == FileCategory::Folder) {
        obj->setDoorId("fsdoor_" + std::to_string(index));
    }
    obj->setTargetLevel("fs://" + entry.fullPath);

    obj->getTransform().setPosition(pos);
    obj->getTransform().setScale(scale);

    if (yawDegrees != 0.0f) {
        obj->setEulerRotation({0.0f, yawDegrees, 0.0f});
    }

    // Register emanation source for frequently-visited folders
    if (entry.category == FileCategory::Folder) {
        float glow = getVisitGlow(entry.fullPath);
        if (glow > 0.1f) {
            // Subtle brightness boost on the folder itself
            obj->setBrightness(1.0f + glow * 0.3f);

            // Compute inward direction toward gallery center (toward the player)
            glm::vec3 outward = m_spawnOrigin - pos;
            outward.y = 0.0f;
            float len = glm::length(outward);
            if (len > 0.01f) outward /= len;
            else outward = glm::vec3(0.0f, 0.0f, 1.0f);

            // Face axes: right is perpendicular to outward on XZ, up is Y
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::cross(up, outward);

            Emanation em;
            em.center = pos + glm::vec3(0.0f, scale.y, 0.0f);  // visual center of folder
            em.halfExtent = scale;  // scale already represents half-extents of the cube
            em.forward = outward;
            em.up = up;
            em.right = right;
            em.intensity = glow;
            em.timer = static_cast<float>(rand() % 100) / 100.0f * EMANATION_SPAWN_INTERVAL;
            m_emanations.push_back(em);
        }
    }

    // Register video animation
    if (entry.category == FileCategory::Video) {
        std::string cachePath = getCachePath(entry.fullPath);
        std::vector<std::vector<unsigned char>> cachedFrames;

        VideoAnimation anim;
        anim.bufferHandle = handle;

        if (loadCachedFrames(cachePath, cachedFrames, LABEL_SIZE)) {
            anim.frames = std::move(cachedFrames);
            anim.loaded = true;
        } else {
            anim.pendingFrames = std::make_shared<std::vector<std::vector<unsigned char>>>();
            anim.ready = std::make_shared<std::atomic<bool>>(false);
            anim.loaded = false;

            // Throttle concurrent ffmpeg threads
            if (static_cast<int>(m_extractionThreads.size()) < MAX_CONCURRENT_EXTRACTIONS) {
                m_extractionThreads.emplace_back(
                    extractionWorker,
                    entry.fullPath, cachePath,
                    LABEL_SIZE, MAX_VIDEO_FRAMES,
                    anim.pendingFrames, anim.ready, m_cancelExtraction);
            } else {
                m_pendingExtractions.push_back({entry.fullPath, cachePath,
                                                anim.pendingFrames, anim.ready});
            }
        }

        m_videoAnimations.push_back(std::move(anim));
    }

    m_sceneObjects->push_back(std::move(obj));
}

// ── Gallery Ring ───────────────────────────────────────────────────────

int FilesystemBrowser::spawnGalleryRing(const std::vector<EntryInfo>& items,
                                         const glm::vec3& center, float baseY,
                                         int startLevel) {
    if (items.empty()) return startLevel;

    int totalItems = static_cast<int>(items.size());
    int level = startLevel;
    float radius = galleryRadius();
    float segmentAngle = 2.0f * M_PI / gallerySides();
    float segmentWidth = 2.0f * radius * sinf(segmentAngle / 2.0f);

    // Determine scale based on type (all items in this call are same type)
    FileCategory cat = items[0].category;
    bool isDoorType = (cat == FileCategory::Folder);

    for (int placed = 0; placed < totalItems; ) {
        int itemsThisLevel = std::min(gallerySides(), totalItems - placed);
        float levelY = baseY + level * GALLERY_WALL_HEIGHT;

        for (int s = 0; s < gallerySides(); ++s) {
            float angle = s * segmentAngle;
            float wallX = center.x + radius * cosf(angle);
            float wallZ = center.z + radius * sinf(angle);
            float yawDeg = -angle * 180.0f / M_PI + 90.0f;

            // Spawn dark wall segment
            glm::vec4 wallColor = m_siloConfig.wallColor;
            auto wallMesh = PrimitiveMeshBuilder::createCube(1.0f, wallColor);

            uint32_t wallHandle = m_modelRenderer->createModel(
                wallMesh.vertices, wallMesh.indices, nullptr, 0, 0);

            auto wallObj = std::make_unique<SceneObject>("FSWall_" + std::to_string(level) + "_" + std::to_string(s));
            wallObj->setBufferHandle(wallHandle);
            wallObj->setIndexCount(static_cast<uint32_t>(wallMesh.indices.size()));
            wallObj->setVertexCount(static_cast<uint32_t>(wallMesh.vertices.size()));
            wallObj->setLocalBounds(wallMesh.bounds);
            wallObj->setModelPath("");
            wallObj->setMeshData(wallMesh.vertices, wallMesh.indices);
            wallObj->setPrimitiveType(PrimitiveType::Cube);
            wallObj->setPrimitiveSize(1.0f);
            wallObj->setPrimitiveColor(wallColor);
            wallObj->setBuildingType("filesystem_wall");
            wallObj->setAABBCollision(true);
            // Tag wall with its ring category for paste-type matching
            switch (cat) {
                case FileCategory::Folder:     wallObj->setDescription("wall_folder"); break;
                case FileCategory::Image:      wallObj->setDescription("wall_image"); break;
                case FileCategory::Video:      wallObj->setDescription("wall_video"); break;
                case FileCategory::Model3D:   wallObj->setDescription("wall_model"); break;
                default:                       wallObj->setDescription("wall_other"); break;
            }

            wallObj->getTransform().setPosition({wallX, levelY, wallZ});
            wallObj->getTransform().setScale({segmentWidth, GALLERY_WALL_HEIGHT, 0.15f});
            wallObj->setEulerRotation({0.0f, yawDeg, 0.0f});

            // Tag occupied walls with the item's path so context menus can find it
            if (s < itemsThisLevel) {
                wallObj->setTargetLevel("fs://" + items[placed + s].fullPath);
            }

            m_sceneObjects->push_back(std::move(wallObj));

            // Spawn item on this wall segment
            if (s < itemsThisLevel) {
                int idx = placed + s;
                const auto& item = items[idx];

                float inset = 2.4f;
                float itemX = center.x + (radius - inset) * cosf(angle);
                float itemZ = center.z + (radius - inset) * sinf(angle);
                bool isModel = (cat == FileCategory::Model3D);
                float itemY = isModel ? levelY + GALLERY_WALL_HEIGHT * 0.5f : levelY;
                glm::vec3 scale;
                if (isDoorType) {
                    // Doors: tall slab shape, fitting within wall
                    scale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 2.0f};
                } else if (isModel) {
                    // 3D models: uniform scale so they keep their shape
                    float uniformSize = std::min(segmentWidth * 0.7f, GALLERY_WALL_HEIGHT - 1.0f) / 1.5f;
                    scale = glm::vec3(uniformSize);
                } else {
                    // Images/videos: wide panel on the wall
                    // Cube mesh is 1.5 units, so scale = desired / 1.5
                    float w = segmentWidth * 0.85f / 1.5f;
                    float h = (GALLERY_WALL_HEIGHT - 1.0f) / 1.5f;
                    scale = {w, h, 0.03f};
                }

                spawnOneObject(item, idx, {itemX, itemY, itemZ}, scale, yawDeg);
            }
        }

        placed += itemsThisLevel;
        level++;
    }

    return level;
}

// ── Spawn File At Wall (paste-in-place) ────────────────────────────────

void FilesystemBrowser::spawnFileAtWall(const std::string& filePath,
                                         const glm::vec3& wallPos,
                                         const glm::vec3& wallScale,
                                         float wallYawDeg) {
    if (!m_modelRenderer || !m_sceneObjects) return;

    namespace fs = std::filesystem;
    fs::path p(filePath);
    if (!fs::exists(p)) return;

    std::string name = p.filename().string();
    fs::directory_entry dirEntry(p);
    FileCategory cat = categorize(dirEntry);
    EntryInfo entry{name, filePath, cat};

    // Recover the radial angle from the wall yaw: yawDeg = -angle * 180/PI + 90
    float angle = (90.0f - wallYawDeg) * static_cast<float>(M_PI) / 180.0f;

    // Position: inset toward center from wall, raised from wall base
    float inset = 2.4f;
    float itemX = wallPos.x - inset * cosf(angle);
    float itemZ = wallPos.z - inset * sinf(angle);
    bool isModel = (cat == FileCategory::Model3D);
    float itemY = isModel ? wallPos.y + GALLERY_WALL_HEIGHT * 0.5f : wallPos.y;

    // Scale: match gallery ring layout
    bool isDoorType = (cat == FileCategory::Folder);
    float segmentWidth = wallScale.x; // wall scale X == segmentWidth
    glm::vec3 scale;
    if (isDoorType) {
        scale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 2.0f};
    } else if (isModel) {
        float uniformSize = std::min(segmentWidth * 0.7f, GALLERY_WALL_HEIGHT - 1.0f) / 1.5f;
        scale = glm::vec3(uniformSize);
    } else {
        float w = segmentWidth * 0.85f / 1.5f;
        float h = (GALLERY_WALL_HEIGHT - 1.0f) / 1.5f;
        scale = {w, h, 0.03f};
    }

    // Ensure cancel token exists for video extraction threads
    if (!m_cancelExtraction) {
        m_cancelExtraction = std::make_shared<std::atomic<bool>>(false);
    }

    spawnOneObject(entry, m_sceneObjects->size(), {itemX, itemY, itemZ}, scale, wallYawDeg);

    std::cout << "[FilesystemBrowser] Pasted " << name << " at wall slot" << std::endl;
}

// ── App Launcher Ring ──────────────────────────────────────────────────

void FilesystemBrowser::spawnAppRing(const glm::vec3& center, float baseY) {
    if (!m_modelRenderer || !m_sceneObjects) return;

    // App ring is level 0 of the silo (filesystem content starts at level 1)
    float levelY = baseY;
    float radius = galleryRadius();
    float segmentAngle = 2.0f * M_PI / gallerySides();
    float segmentWidth = 2.0f * radius * sinf(segmentAngle / 2.0f);

    glm::vec4 appWallColor{0.12f, 0.08f, 0.18f, 1.0f};
    glm::vec4 appDoorColor{0.6f, 0.15f, 0.7f, 1.0f}; // purple/magenta

    auto wallMesh = PrimitiveMeshBuilder::createCube(1.0f, appWallColor);

    for (int s = 0; s < gallerySides(); ++s) {
        float angle = s * segmentAngle;
        float wallX = center.x + radius * cosf(angle);
        float wallZ = center.z + radius * sinf(angle);
        float yawDeg = -angle * 180.0f / M_PI + 90.0f;

        // Wall segment
        uint32_t wallHandle = m_modelRenderer->createModel(
            wallMesh.vertices, wallMesh.indices, nullptr, 0, 0);

        auto wallObj = std::make_unique<SceneObject>("FSAppWall_" + std::to_string(s));
        wallObj->setBufferHandle(wallHandle);
        wallObj->setIndexCount(static_cast<uint32_t>(wallMesh.indices.size()));
        wallObj->setVertexCount(static_cast<uint32_t>(wallMesh.vertices.size()));
        wallObj->setLocalBounds(wallMesh.bounds);
        wallObj->setMeshData(wallMesh.vertices, wallMesh.indices);
        wallObj->setPrimitiveType(PrimitiveType::Cube);
        wallObj->setPrimitiveSize(1.0f);
        wallObj->setPrimitiveColor(appWallColor);
        wallObj->setBuildingType("filesystem_wall");
        wallObj->setAABBCollision(true);
        wallObj->setDescription("wall_app");

        wallObj->getTransform().setPosition({wallX, levelY, wallZ});
        wallObj->getTransform().setScale({segmentWidth, GALLERY_WALL_HEIGHT, 0.15f});
        wallObj->setEulerRotation({0.0f, yawDeg, 0.0f});

        m_sceneObjects->push_back(std::move(wallObj));

        // Place Forge door on segment 0
        if (s == 0) {
            float inset = 2.4f;
            float doorX = center.x + (radius - inset) * cosf(angle);
            float doorZ = center.z + (radius - inset) * sinf(angle);
            float doorY = levelY;

            auto doorMesh = PrimitiveMeshBuilder::createCube(2.0f, appDoorColor);

            // Load custom forge door image
            std::vector<unsigned char> texPixels;
            int texW = LABEL_SIZE, texH = LABEL_SIZE;
            bool loadedImage = false;
            for (const auto& candidate : {
                "assets/textures/forge_door.jpeg",
                "../../../examples/terrain_editor/assets/textures/forge_door.jpeg",
                "examples/terrain_editor/assets/textures/forge_door.jpeg"
            }) {
                int imgW, imgH, imgChannels;
                unsigned char* data = stbi_load(candidate, &imgW, &imgH, &imgChannels, 4);
                if (data) {
                    texW = imgW; texH = imgH;
                    texPixels.resize(imgW * imgH * 4);
                    // Flip vertically to match Vulkan UV coordinates
                    for (int y = 0; y < imgH; ++y) {
                        memcpy(&texPixels[y * imgW * 4],
                               &data[(imgH - 1 - y) * imgW * 4],
                               imgW * 4);
                    }
                    stbi_image_free(data);
                    loadedImage = true;
                    break;
                }
            }
            if (!loadedImage) {
                renderLabel(texPixels, "Forge", FileCategory::Executable, appDoorColor);
                texW = LABEL_SIZE; texH = LABEL_SIZE;
            }
            uint32_t doorHandle = m_modelRenderer->createModel(
                doorMesh.vertices, doorMesh.indices,
                texPixels.data(), texW, texH);

            auto doorObj = std::make_unique<SceneObject>("FSAppDoor_Forge");
            doorObj->setBufferHandle(doorHandle);
            doorObj->setIndexCount(static_cast<uint32_t>(doorMesh.indices.size()));
            doorObj->setVertexCount(static_cast<uint32_t>(doorMesh.vertices.size()));
            doorObj->setLocalBounds(doorMesh.bounds);
            doorObj->setMeshData(doorMesh.vertices, doorMesh.indices);
            doorObj->setPrimitiveType(PrimitiveType::Door);
            doorObj->setPrimitiveSize(2.0f);
            doorObj->setPrimitiveColor(appDoorColor);
            doorObj->setBuildingType("filesystem");
            doorObj->setDescription("Forge");
            doorObj->setDoorId("appdoor_forge");
            // Navigate to the real assets/models directory where ForgeRoom spawns
            // Try multiple locations relative to the executable
            std::string forgePath;
            for (const auto& candidate : {
                "assets/models",
                "../../../examples/terrain_editor/assets/models",
                "examples/terrain_editor/assets/models"
            }) {
                std::error_code ec;
                auto p = std::filesystem::canonical(candidate, ec);
                if (!ec) { forgePath = p.string(); break; }
            }
            if (forgePath.empty()) {
                // Last resort: create it next to home
                forgePath = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/assets/models";
                std::filesystem::create_directories(forgePath);
            }
            doorObj->setTargetLevel("fs://" + forgePath);

            glm::vec3 doorScale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 2.0f};
            doorObj->getTransform().setPosition({doorX, doorY, doorZ});
            doorObj->getTransform().setScale(doorScale);
            doorObj->setEulerRotation({0.0f, yawDeg, 0.0f});

            m_sceneObjects->push_back(std::move(doorObj));
        }

        // Place Home door on segment 1
        if (s == 1) {
            const char* homeEnv = getenv("HOME");
            if (homeEnv) {
                float inset = 2.4f;
                float doorX = center.x + (radius - inset) * cosf(angle);
                float doorZ = center.z + (radius - inset) * sinf(angle);
                float doorY = levelY;

                glm::vec4 homeColor{0.15f, 0.5f, 0.8f, 1.0f}; // blue
                auto doorMesh = PrimitiveMeshBuilder::createCube(2.0f, homeColor);

                std::vector<unsigned char> texPixels;
                renderLabel(texPixels, "Home", FileCategory::Folder, homeColor);
                uint32_t doorHandle = m_modelRenderer->createModel(
                    doorMesh.vertices, doorMesh.indices,
                    texPixels.data(), LABEL_SIZE, LABEL_SIZE);

                auto doorObj = std::make_unique<SceneObject>("FSAppDoor_Home");
                doorObj->setBufferHandle(doorHandle);
                doorObj->setIndexCount(static_cast<uint32_t>(doorMesh.indices.size()));
                doorObj->setVertexCount(static_cast<uint32_t>(doorMesh.vertices.size()));
                doorObj->setLocalBounds(doorMesh.bounds);
                doorObj->setMeshData(doorMesh.vertices, doorMesh.indices);
                doorObj->setPrimitiveType(PrimitiveType::Door);
                doorObj->setPrimitiveSize(2.0f);
                doorObj->setPrimitiveColor(homeColor);
                doorObj->setBuildingType("filesystem");
                doorObj->setDescription("Home");
                doorObj->setDoorId("appdoor_home");
                doorObj->setTargetLevel("fs://" + std::string(homeEnv));

                glm::vec3 doorScale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 2.0f};
                doorObj->getTransform().setPosition({doorX, doorY, doorZ});
                doorObj->getTransform().setScale(doorScale);
                doorObj->setEulerRotation({0.0f, yawDeg, 0.0f});

                m_sceneObjects->push_back(std::move(doorObj));
            }
        }
    }
}

void FilesystemBrowser::spawnAppSpace(const std::string& appPath, const glm::vec3& center, float baseY) {
    if (!m_modelRenderer || !m_sceneObjects) return;

    if (appPath == "app://forge") {
        // Spawn the ForgeRoom pad at center
        m_forgeRoom.init(m_sceneObjects, m_modelRenderer);
        m_forgeRoom.spawn(center, baseY);
        m_forgeRoom.loadRegistry();

        // Spawn a minimal containment ring
        float radius = galleryRadius();
        float segmentAngle = 2.0f * M_PI / gallerySides();
        float segmentWidth = 2.0f * radius * sinf(segmentAngle / 2.0f);
        glm::vec4 wallColor{0.12f, 0.08f, 0.18f, 1.0f};
        glm::vec4 backDoorColor{0.3f, 0.6f, 0.9f, 1.0f}; // blue "Back" door

        auto wallMesh = PrimitiveMeshBuilder::createCube(1.0f, wallColor);

        for (int s = 0; s < gallerySides(); ++s) {
            float angle = s * segmentAngle;
            float wallX = center.x + radius * cosf(angle);
            float wallZ = center.z + radius * sinf(angle);
            float yawDeg = -angle * 180.0f / M_PI + 90.0f;

            uint32_t wallHandle = m_modelRenderer->createModel(
                wallMesh.vertices, wallMesh.indices, nullptr, 0, 0);

            auto wallObj = std::make_unique<SceneObject>("FSForgeWall_" + std::to_string(s));
            wallObj->setBufferHandle(wallHandle);
            wallObj->setIndexCount(static_cast<uint32_t>(wallMesh.indices.size()));
            wallObj->setVertexCount(static_cast<uint32_t>(wallMesh.vertices.size()));
            wallObj->setLocalBounds(wallMesh.bounds);
            wallObj->setMeshData(wallMesh.vertices, wallMesh.indices);
            wallObj->setPrimitiveType(PrimitiveType::Cube);
            wallObj->setPrimitiveSize(1.0f);
            wallObj->setPrimitiveColor(wallColor);
            wallObj->setBuildingType("filesystem_wall");
            wallObj->setAABBCollision(true);

            wallObj->getTransform().setPosition({wallX, baseY, wallZ});
            wallObj->getTransform().setScale({segmentWidth, GALLERY_WALL_HEIGHT, 0.15f});
            wallObj->setEulerRotation({0.0f, yawDeg, 0.0f});

            m_sceneObjects->push_back(std::move(wallObj));

            // Place "Back" door on segment 0
            if (s == 0) {
                float inset = 2.4f;
                float doorX = center.x + (radius - inset) * cosf(angle);
                float doorZ = center.z + (radius - inset) * sinf(angle);
                float doorY = baseY;

                auto doorMesh = PrimitiveMeshBuilder::createCube(2.0f, backDoorColor);

                std::vector<unsigned char> texPixels;
                renderLabel(texPixels, "Back", FileCategory::Folder, backDoorColor);
                uint32_t doorHandle = m_modelRenderer->createModel(
                    doorMesh.vertices, doorMesh.indices,
                    texPixels.data(), LABEL_SIZE, LABEL_SIZE);

                const char* homeEnv = getenv("HOME");
                std::string homePath = homeEnv ? std::string(homeEnv) : "/";

                auto doorObj = std::make_unique<SceneObject>("FSAppDoor_Back");
                doorObj->setBufferHandle(doorHandle);
                doorObj->setIndexCount(static_cast<uint32_t>(doorMesh.indices.size()));
                doorObj->setVertexCount(static_cast<uint32_t>(doorMesh.vertices.size()));
                doorObj->setLocalBounds(doorMesh.bounds);
                doorObj->setMeshData(doorMesh.vertices, doorMesh.indices);
                doorObj->setPrimitiveType(PrimitiveType::Door);
                doorObj->setPrimitiveSize(2.0f);
                doorObj->setPrimitiveColor(backDoorColor);
                doorObj->setBuildingType("filesystem");
                doorObj->setDescription("Back");
                doorObj->setDoorId("appdoor_back");
                doorObj->setTargetLevel("fs://" + homePath);

                glm::vec3 doorScale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 2.0f};
                doorObj->getTransform().setPosition({doorX, doorY, doorZ});
                doorObj->getTransform().setScale(doorScale);
                doorObj->setEulerRotation({0.0f, yawDeg, 0.0f});

                m_sceneObjects->push_back(std::move(doorObj));
            }
        }
    }
}

// ── Basement Room ──────────────────────────────────────────────────────

void FilesystemBrowser::spawnBasement(const glm::vec3& center, float baseY) {
    if (!m_modelRenderer || !m_sceneObjects) return;

    // Don't spawn if basement already exists (it persists across navigations)
    for (const auto& obj : *m_sceneObjects) {
        if (obj && obj->getBuildingType() == "eden_basement") return;
    }

    float halfSize = basementSize() / 2.0f;
    float floorY = baseY - basementHeight();
    glm::vec4 wallColor = m_siloConfig.wallColor;

    auto cubeMesh = PrimitiveMeshBuilder::createCube(1.0f, wallColor);
    int panelNum = 0;

    auto spawnPanel = [&](const glm::vec3& pos, const glm::vec3& scale, const std::string& tag = "eden_basement") {
        auto obj = std::make_unique<SceneObject>(
            "FSBasement_" + std::to_string(panelNum++));
        uint32_t handle = m_modelRenderer->createModel(
            cubeMesh.vertices, cubeMesh.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(cubeMesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(cubeMesh.vertices.size()));
        obj->setLocalBounds(cubeMesh.bounds);
        obj->setMeshData(cubeMesh.vertices, cubeMesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(1.0f);
        obj->setPrimitiveColor(wallColor);
        obj->setBuildingType(tag);
        obj->setAABBCollision(true);
        obj->getTransform().setPosition(pos);
        obj->getTransform().setScale(scale);
        m_sceneObjects->push_back(std::move(obj));
    };

    // Cube mesh Y goes from 0 to size, so position.y = bottom edge.
    // Floor slab: bottom at floorY - 1, top at floorY
    // Ceiling slab: bottom at baseY, top at baseY + 1
    // Walls: bottom at floorY - 1 (flush with floor bottom), top at baseY + 1 (flush with ceiling top)
    float wallBottom = floorY - 1.0f;
    float wallHeight = (baseY + 1.0f) - wallBottom;

    // 4 walls — each split into 3 panels around a centered door opening
    // Door opening: 16 units wide, 14 units tall from floor
    const float doorWidth  = 16.0f;
    const float doorHeight = 14.0f;
    const float doorHalfW  = doorWidth / 2.0f;
    // Each side segment: (basementSize() - doorWidth) / 2 = 20
    const float segWidth   = (basementSize() - doorWidth) / 2.0f;
    const float lintelBottom = floorY - 1.0f + doorHeight; // wallBottom + doorHeight
    const float lintelHeight = wallHeight - doorHeight;

    // North wall (positive Z face, stretches along X)
    spawnPanel({center.x - doorHalfW - segWidth / 2.0f, wallBottom, center.z + halfSize},
               {segWidth, wallHeight, 1.0f}, "eden_basement_wall");
    spawnPanel({center.x + doorHalfW + segWidth / 2.0f, wallBottom, center.z + halfSize},
               {segWidth, wallHeight, 1.0f}, "eden_basement_wall");
    spawnPanel({center.x, lintelBottom, center.z + halfSize},
               {doorWidth, lintelHeight, 1.0f}, "eden_basement_wall");

    // South wall (negative Z face, stretches along X)
    spawnPanel({center.x - doorHalfW - segWidth / 2.0f, wallBottom, center.z - halfSize},
               {segWidth, wallHeight, 1.0f}, "eden_basement_wall");
    spawnPanel({center.x + doorHalfW + segWidth / 2.0f, wallBottom, center.z - halfSize},
               {segWidth, wallHeight, 1.0f}, "eden_basement_wall");
    spawnPanel({center.x, lintelBottom, center.z - halfSize},
               {doorWidth, lintelHeight, 1.0f}, "eden_basement_wall");

    // East wall (positive X face, stretches along Z)
    spawnPanel({center.x + halfSize, wallBottom, center.z - doorHalfW - segWidth / 2.0f},
               {1.0f, wallHeight, segWidth}, "eden_basement_wall");
    spawnPanel({center.x + halfSize, wallBottom, center.z + doorHalfW + segWidth / 2.0f},
               {1.0f, wallHeight, segWidth}, "eden_basement_wall");
    spawnPanel({center.x + halfSize, lintelBottom, center.z},
               {1.0f, lintelHeight, doorWidth}, "eden_basement_wall");

    // West wall (negative X face, stretches along Z)
    spawnPanel({center.x - halfSize, wallBottom, center.z - doorHalfW - segWidth / 2.0f},
               {1.0f, wallHeight, segWidth}, "eden_basement_wall");
    spawnPanel({center.x - halfSize, wallBottom, center.z + doorHalfW + segWidth / 2.0f},
               {1.0f, wallHeight, segWidth}, "eden_basement_wall");
    spawnPanel({center.x - halfSize, lintelBottom, center.z},
               {1.0f, lintelHeight, doorWidth}, "eden_basement_wall");

    // Floor — bottom at floorY - 1, top at floorY
    spawnPanel({center.x, floorY - 1.0f, center.z},
               {basementSize(), 1.0f, basementSize()});

    // Solid ceiling — no hole
    // Bottom at baseY, top at baseY + 1
    spawnPanel({center.x, baseY, center.z},
               {basementSize(), 1.0f, basementSize()});
}

// ── Spawn Objects ──────────────────────────────────────────────────────

void FilesystemBrowser::spawnObjects(const std::string& dirPath) {
    if (!m_modelRenderer || !m_sceneObjects || !m_terrain) return;

    // Handle app:// virtual paths (e.g. "app://forge")
    if (dirPath.rfind("app://", 0) == 0) {
        if (!m_basementBaseYValid) {
            m_basementBaseY = 100.0f;
            m_basementBaseYValid = true;
        }
        float baseY = m_basementBaseY;
        glm::vec3 center = m_spawnOrigin;
        center.y = baseY;
        spawnAppSpace(dirPath, center, baseY);
        return;
    }

    namespace fs = std::filesystem;
    fs::path dir(dirPath);

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "[FilesystemBrowser] Not a valid directory: " << dirPath << std::endl;
        m_spawnFailed = true;
        return;
    }

    m_cancelExtraction = std::make_shared<std::atomic<bool>>(false);

    // Collect entries
    std::vector<EntryInfo> entries;

    if (dir.has_parent_path() && dir != dir.root_path()) {
        entries.push_back({"..", fs::canonical(dir.parent_path()).string(), FileCategory::Folder});
    }

    try {
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
            std::string name = entry.path().filename().string();
            // Skip ImageBot sidecar description files
            if (name.size() > 9 && name.substr(name.size() - 9) == ".desc.txt") continue;
            std::string full = entry.path().string();
            // Skip files currently held in inventory
            if (m_excludedPaths.count(full)) continue;
            FileCategory cat = categorize(entry);
            entries.push_back({name, full, cat});
        }
    } catch (const std::exception& e) {
        std::cerr << "[FilesystemBrowser] Error scanning " << dirPath << ": " << e.what() << std::endl;
    }

    // Sort all entries by modification time (newest first) before truncating
    // so the MAX_ENTRIES cap keeps the most recent files
    std::sort(entries.begin(), entries.end(), [](const EntryInfo& a, const EntryInfo& b) {
        if (a.name == "..") return true;
        if (b.name == "..") return false;
        std::error_code ec1, ec2;
        auto ta = std::filesystem::last_write_time(a.fullPath, ec1);
        auto tb = std::filesystem::last_write_time(b.fullPath, ec2);
        if (ec1 || ec2) return a.name < b.name;
        return ta > tb;
    });
    if (entries.size() > MAX_ENTRIES) {
        entries.resize(MAX_ENTRIES);
    }

    // Split entries by type
    std::vector<EntryInfo> folders;
    std::vector<EntryInfo> images;
    std::vector<EntryInfo> videos;
    std::vector<EntryInfo> models;
    std::vector<EntryInfo> others; // text, source, exe, other

    for (auto& e : entries) {
        switch (e.category) {
            case FileCategory::Folder:  folders.push_back(std::move(e)); break;
            case FileCategory::Image:   images.push_back(std::move(e));  break;
            case FileCategory::Video:   videos.push_back(std::move(e));  break;
            case FileCategory::Model3D: models.push_back(std::move(e));  break;
            default:                    others.push_back(std::move(e));   break;
        }
    }

    // Sort each group by modification time (newest first), .. always first in folders
    auto sortByTime = [](std::vector<EntryInfo>& v) {
        std::sort(v.begin(), v.end(), [](const EntryInfo& a, const EntryInfo& b) {
            if (a.name == "..") return true;
            if (b.name == "..") return false;
            std::error_code ec1, ec2;
            auto ta = std::filesystem::last_write_time(a.fullPath, ec1);
            auto tb = std::filesystem::last_write_time(b.fullPath, ec2);
            if (ec1 || ec2) return a.name < b.name; // fallback to alpha
            return ta > tb; // newest first
        });
    };
    sortByTime(folders);
    sortByTime(images);
    sortByTime(videos);
    sortByTime(models);
    sortByTime(others);

    // Use cached baseY so silo stays aligned with persistent basement across navigations.
    // Only recompute from terrain when no cached value exists (first spawn or new level).
    if (!m_basementBaseYValid) {
        m_basementBaseY = 100.0f;  // Fixed Y — silo complex sits at 100m
        m_basementBaseYValid = true;
    }
    float baseY = m_basementBaseY;
    glm::vec3 center = m_spawnOrigin;
    center.y = baseY;

    bool hasRing = !folders.empty() || !images.empty() || !videos.empty() || !models.empty();

    // Reserve level 0 for the app launcher ring in every silo
    // App ring takes level 0, filesystem content starts at level 1
    int nextLevel = 1;

    // Spawn app launcher ring at level 0 (base of every silo)
    spawnAppRing(center, baseY);
    nextLevel = spawnGalleryRing(folders, center, baseY, nextLevel);
    nextLevel = spawnGalleryRing(images,  center, baseY, nextLevel);
    nextLevel = spawnGalleryRing(videos,  center, baseY, nextLevel);
    nextLevel = spawnGalleryRing(models,  center, baseY, nextLevel);

    // Spawn basement room beneath the silo
    if (nextLevel > 0) {
        spawnBasement(center, baseY);
    }

    // Spawn vertical columns between panel sections (extended down through basement)
    if (nextLevel > 0) {
        float totalHeight = nextLevel * GALLERY_WALL_HEIGHT + basementHeight();
        float radius = galleryRadius();
        float segmentAngle = 2.0f * M_PI / gallerySides();
        glm::vec4 colColor = m_siloConfig.columnColor;

        for (int s = 0; s < gallerySides(); ++s) {
            float angle = (s + 0.5f) * segmentAngle;
            float colX = center.x + radius * cosf(angle);
            float colZ = center.z + radius * sinf(angle);
            float yawDeg = -angle * 180.0f / M_PI + 90.0f;

            auto mesh = PrimitiveMeshBuilder::createCube(1.0f, colColor);
            uint32_t handle = m_modelRenderer->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);

            auto obj = std::make_unique<SceneObject>("FSColumn_" + std::to_string(s));
            obj->setBufferHandle(handle);
            obj->setIndexCount(mesh.indices.size());
            obj->setVertexCount(mesh.vertices.size());
            obj->setLocalBounds(mesh.bounds);
            obj->setMeshData(mesh.vertices, mesh.indices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveSize(1.0f);
            obj->setPrimitiveColor(colColor);
            obj->setBuildingType("filesystem_wall");
            obj->setAABBCollision(true);

            obj->getTransform().setPosition({colX, baseY - basementHeight(), colZ});
            obj->getTransform().setScale({0.3f, totalHeight, 0.3f});
            obj->setEulerRotation({0.0f, yawDeg, 0.0f});

            m_sceneObjects->push_back(std::move(obj));
        }
    }

    // Place remaining "other" entries in a grid in the center of the room
    if (!others.empty()) {
        float gridOffsetX = center.x - (GRID_COLUMNS * GRID_SPACING / 2.0f);
        float gridOffsetZ = center.z - 2.0f;

        if (!hasRing) {
            gridOffsetX = m_spawnOrigin.x;
            gridOffsetZ = m_spawnOrigin.z;
        }

        int col = 0, row = 0;
        for (size_t i = 0; i < others.size(); ++i) {
            float x = gridOffsetX + col * GRID_SPACING;
            float z = gridOffsetZ + row * GRID_SPACING;
            float y = m_terrain->getHeightAt(x, z);

            spawnOneObject(others[i], i, {x, y, z}, glm::vec3(1.0f));

            col++;
            if (col >= GRID_COLUMNS) { col = 0; row++; }
        }
    }

    size_t total = folders.size() + images.size() + videos.size() + models.size() + others.size();
    // std::cout << "[FilesystemBrowser] Spawned " << total
    //           << " objects for " << dirPath << std::endl;

    // Spawn forge room in assets/models/ directory
    if (dirPath.find("assets/models") != std::string::npos) {
        m_forgeRoom.init(m_sceneObjects, m_modelRenderer);
        m_forgeRoom.spawn(center, baseY);
    }

    // Always load the deployed bots registry (so deployed bots work in any directory)
    m_forgeRoom.loadRegistry();

    // Check if there are deployed bots for this territory
    auto deployedBots = m_forgeRoom.getDeployedBotsForTerritory(dirPath);
    bool spawnedDeployedBot = false;
    bool spawnedImageBot = false;
    for (auto& bot : deployedBots) {
        if (bot.job == "CleanerBot" && !spawnedDeployedBot) {
            m_cleanerBot.init(m_sceneObjects, m_modelRenderer);
            glm::vec3 botPos = center;
            botPos.y = baseY;
            m_cleanerBot.spawn(botPos, m_modelRenderer, bot.modelPath);
            spawnedDeployedBot = true;
        }
        if (bot.job == "ImageBot" && !spawnedImageBot) {
            m_imageBot.init(m_sceneObjects, m_modelRenderer);
            glm::vec3 botPos = center;
            botPos.y = baseY;
            botPos.x += 2.0f; // offset so it doesn't overlap CleanerBot
            m_imageBot.spawn(botPos, m_modelRenderer, bot.modelPath);
            spawnedImageBot = true;
        }
        if (bot.job == "CullRobot") {
            m_cullSession.init(m_sceneObjects, m_modelRenderer, center, baseY);
            m_cullSession.spawnRobots(center, baseY, m_modelRenderer, bot.modelPath);
        }
    }

    // Spawn default cleaner bot in home directory (only if no deployed bot took the slot)
    if (!spawnedDeployedBot) {
        const char* homeEnv = getenv("HOME");
        if (homeEnv && dirPath == std::string(homeEnv)) {
            m_cleanerBot.init(m_sceneObjects, m_modelRenderer);
            glm::vec3 botPos = center;
            botPos.y = baseY;
            m_cleanerBot.spawn(botPos, m_modelRenderer);
        }
    }

}

// ── Label Rendering ────────────────────────────────────────────────────

void FilesystemBrowser::renderLabel(std::vector<unsigned char>& pixels,
                                    const std::string& filename,
                                    FileCategory category,
                                    const glm::vec4& color) {
    const int texSize = LABEL_SIZE;
    pixels.resize(texSize * texSize * 4);

    unsigned char bgR = static_cast<unsigned char>(color.r * 60);
    unsigned char bgG = static_cast<unsigned char>(color.g * 60);
    unsigned char bgB = static_cast<unsigned char>(color.b * 60);

    for (int i = 0; i < texSize * texSize; ++i) {
        pixels[i * 4 + 0] = bgR;
        pixels[i * 4 + 1] = bgG;
        pixels[i * 4 + 2] = bgB;
        pixels[i * 4 + 3] = 255;
    }

    unsigned char fgR = 255, fgG = 255, fgB = 255;

    const int scale = 2;
    const int charW = 8 * scale;
    const int charH = 16 * scale;

    const char* catLabel = "";
    switch (category) {
        case FileCategory::Folder:     catLabel = "[DIR]"; break;
        case FileCategory::Image:      catLabel = "[IMG]"; break;
        case FileCategory::Video:      catLabel = "[VID]"; break;
        case FileCategory::Text:       catLabel = "[TXT]"; break;
        case FileCategory::Executable: catLabel = "[EXE]"; break;
        case FileCategory::SourceCode: catLabel = "[SRC]"; break;
        case FileCategory::Model3D:   catLabel = "[3D]"; break;
        default:                       catLabel = "[---]"; break;
    }

    auto drawChar = [&](char ch, int px0, int py0) {
        if (ch < 32 || ch > 126) return;
        const unsigned char* glyph = &kTermFont8x16[(ch - 32) * 16];
        for (int y = 0; y < 16; ++y) {
            unsigned char bits = glyph[y];
            for (int x = 0; x < 8; ++x) {
                if (bits & (0x80 >> x)) {
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            int px = px0 + x * scale + sx;
                            int py = py0 + y * scale + sy;
                            if (px >= 0 && px < texSize && py >= 0 && py < texSize) {
                                int idx = (py * texSize + px) * 4;
                                pixels[idx + 0] = fgR;
                                pixels[idx + 1] = fgG;
                                pixels[idx + 2] = fgB;
                                pixels[idx + 3] = 255;
                            }
                        }
                    }
                }
            }
        }
    };

    int catLen = static_cast<int>(strlen(catLabel));
    int catStartX = (texSize - catLen * charW) / 2;
    int lineY = 20;
    for (int i = 0; i < catLen; ++i) {
        drawChar(catLabel[i], catStartX + i * charW, lineY);
    }

    int maxCharsPerLine = texSize / charW;
    lineY += charH + 8;

    std::string display = filename;
    if (display.length() > static_cast<size_t>(maxCharsPerLine * 4)) {
        display = display.substr(0, maxCharsPerLine * 4 - 3) + "...";
    }

    int cx = 0;
    for (size_t i = 0; i < display.size(); ++i) {
        if (cx >= maxCharsPerLine) {
            cx = 0;
            lineY += charH + 2;
            if (lineY + charH > texSize - 10) break;
        }
        if (cx == 0 && i > 0) {
            drawChar(display[i], 8 + cx * charW, lineY);
        } else if (i < static_cast<size_t>(maxCharsPerLine)) {
            int firstLineLen = std::min(static_cast<int>(display.size()), maxCharsPerLine);
            int startX = (texSize - firstLineLen * charW) / 2;
            drawChar(display[i], startX + cx * charW, lineY);
        } else {
            drawChar(display[i], 8 + cx * charW, lineY);
        }
        cx++;
    }

    for (int y = 0; y < texSize / 2; ++y) {
        for (int x = 0; x < texSize; ++x) {
            int ti = (y * texSize + x) * 4;
            int bi = ((texSize - 1 - y) * texSize + x) * 4;
            for (int c = 0; c < 4; ++c)
                std::swap(pixels[ti + c], pixels[bi + c]);
        }
    }
}

// ── Image Focus Mode ────────────────────────────────────────────────────

void FilesystemBrowser::focusImage(SceneObject* panel) {
    if (!panel || !m_modelRenderer || m_imageFocus.active) return;

    // Get source file path from targetLevel ("fs://..." prefix)
    std::string target = panel->getTargetLevel();
    if (target.size() <= 5 || target.substr(0, 5) != "fs://") return;
    std::string path = target.substr(5);

    // Load full-res image
    int imgW, imgH, imgChannels;
    unsigned char* data = stbi_load(path.c_str(), &imgW, &imgH, &imgChannels, 4);
    if (!data) return;

    // Cap to FOCUS_MAX_SIZE preserving aspect ratio
    int capW = imgW, capH = imgH;
    int maxDim = std::max(imgW, imgH);
    if (maxDim > FOCUS_MAX_SIZE) {
        float scale = static_cast<float>(FOCUS_MAX_SIZE) / maxDim;
        capW = static_cast<int>(imgW * scale);
        capH = static_cast<int>(imgH * scale);
        if (capW < 1) capW = 1;
        if (capH < 1) capH = 1;
    }

    // Resample into RGBA buffer at capped size (flipped vertically for GPU)
    std::vector<unsigned char> hiRes(capW * capH * 4);
    for (int py = 0; py < capH; ++py) {
        int srcY = (capH - 1 - py) * imgH / capH;
        for (int px = 0; px < capW; ++px) {
            int srcX = px * imgW / capW;
            int srcIdx = (srcY * imgW + srcX) * 4;
            int dstIdx = (py * capW + px) * 4;
            hiRes[dstIdx + 0] = data[srcIdx + 0];
            hiRes[dstIdx + 1] = data[srcIdx + 1];
            hiRes[dstIdx + 2] = data[srcIdx + 2];
            hiRes[dstIdx + 3] = data[srcIdx + 3];
        }
    }
    stbi_image_free(data);

    // Save state for unfocus
    m_imageFocus.panel = panel;
    m_imageFocus.bufferHandle = panel->getBufferHandle();
    m_imageFocus.originalScale = panel->getTransform().getScale();

    // Upload hi-res texture
    m_modelRenderer->updateTexture(m_imageFocus.bufferHandle, hiRes.data(), capW, capH);

    // Store focused image dimensions for click-to-pixel mapping
    m_imageFocus.focusedWidth = capW;
    m_imageFocus.focusedHeight = capH;

    // Adjust panel scale to match image aspect ratio
    // Keep Y scale, adjust X = Y * aspect, keep Z
    float aspect = static_cast<float>(capW) / capH;
    glm::vec3 s = m_imageFocus.originalScale;
    panel->getTransform().setScale({s.y * aspect, s.y, s.z});

    m_imageFocus.active = true;
}

void FilesystemBrowser::unfocusImage() {
    if (!m_imageFocus.active || !m_imageFocus.panel || !m_modelRenderer) return;

    cancelSegmentation();

    // Get source path and reload as 256x256 thumbnail
    std::string target = m_imageFocus.panel->getTargetLevel();
    std::string path = target.substr(5);

    int imgW, imgH, imgChannels;
    unsigned char* data = stbi_load(path.c_str(), &imgW, &imgH, &imgChannels, 4);
    if (data) {
        std::vector<unsigned char> thumb(LABEL_SIZE * LABEL_SIZE * 4);
        for (int py = 0; py < LABEL_SIZE; ++py) {
            int srcY = (LABEL_SIZE - 1 - py) * imgH / LABEL_SIZE;
            for (int px = 0; px < LABEL_SIZE; ++px) {
                int srcX = px * imgW / LABEL_SIZE;
                int srcIdx = (srcY * imgW + srcX) * 4;
                int dstIdx = (py * LABEL_SIZE + px) * 4;
                thumb[dstIdx + 0] = data[srcIdx + 0];
                thumb[dstIdx + 1] = data[srcIdx + 1];
                thumb[dstIdx + 2] = data[srcIdx + 2];
                thumb[dstIdx + 3] = data[srcIdx + 3];
            }
        }
        stbi_image_free(data);
        m_modelRenderer->updateTexture(m_imageFocus.bufferHandle, thumb.data(), LABEL_SIZE, LABEL_SIZE);
    }

    // Restore original scale
    m_imageFocus.panel->getTransform().setScale(m_imageFocus.originalScale);

    m_imageFocus.active = false;
    m_imageFocus.panel = nullptr;
    m_imageFocus.bufferHandle = 0;
    m_imageFocus.focusedWidth = 0;
    m_imageFocus.focusedHeight = 0;
}

// ── SAM2 Segmentation ──────────────────────────────────────────────────

void FilesystemBrowser::segmentImage(int imgX, int imgY) {
    if (!m_imageFocus.active) return;

    // Cancel any in-progress segmentation first
    if (m_segmentation.processing) {
        cancelSegmentation();
    }

    // If already segmented, undo first to work from original
    if (m_segmentation.active) {
        undoSegmentation();
    }

    // Get source image path from focused panel
    if (!m_imageFocus.panel) return;
    std::string target = m_imageFocus.panel->getTargetLevel();
    if (target.size() <= 5 || target.substr(0, 5) != "fs://") return;
    std::string imagePath = target.substr(5);

    // Create temp output path
    std::string outputPath = "/tmp/eden_segment_result.png";

    m_segmentation.imagePath = imagePath;
    m_segmentation.outputPath = outputPath;
    m_segmentation.done = std::make_shared<std::atomic<bool>>(false);
    m_segmentation.cancelled = std::make_shared<std::atomic<bool>>(false);
    m_segmentation.statusMutex = std::make_shared<std::mutex>();
    m_segmentation.statusText = std::make_shared<std::string>("Starting...");
    m_segmentation.processing = true;

    auto done = m_segmentation.done;
    auto cancelled = m_segmentation.cancelled;
    auto statusMtx = m_segmentation.statusMutex;
    auto statusTxt = m_segmentation.statusText;

    m_segmentation.workerThread = std::thread([imagePath, outputPath, imgX, imgY, done, cancelled, statusMtx, statusTxt]() {
        if (cancelled->load()) { done->store(true); return; }

        std::string python = "/home/jasondube/Desktop/segment/.venv/bin/python3";
        std::string script = "/home/jasondube/Desktop/segment/segment_cli.py";

        std::string cmd = shellEscape(python) + " " + shellEscape(script)
            + " --input " + shellEscape(imagePath)
            + " --px " + std::to_string(imgX)
            + " --py " + std::to_string(imgY)
            + " --output " + shellEscape(outputPath)
            + " 2>&1";

        std::cerr << "[Segment] Running: " << cmd << std::endl;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "[Segment] popen failed!" << std::endl;
            done->store(true);
            return;
        }
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (!line.empty()) std::cerr << "[Segment] " << line << std::endl;
            if (line.rfind("STATUS ", 0) == 0) {
                std::lock_guard<std::mutex> lk(*statusMtx);
                *statusTxt = line.substr(7);
            }
        }
        int exitCode = pclose(pipe);
        std::cerr << "[Segment] Process exited with code " << exitCode << std::endl;
        done->store(true);
    });
}

void FilesystemBrowser::segmentImageBox(int x1, int y1, int x2, int y2) {
    if (!m_imageFocus.active) return;

    if (m_segmentation.processing) cancelSegmentation();
    if (m_segmentation.active) undoSegmentation();

    if (!m_imageFocus.panel) return;
    std::string target = m_imageFocus.panel->getTargetLevel();
    if (target.size() <= 5 || target.substr(0, 5) != "fs://") return;
    std::string imagePath = target.substr(5);
    std::string outputPath = "/tmp/eden_segment_result.png";

    m_segmentation.imagePath = imagePath;
    m_segmentation.outputPath = outputPath;
    m_segmentation.done = std::make_shared<std::atomic<bool>>(false);
    m_segmentation.cancelled = std::make_shared<std::atomic<bool>>(false);
    m_segmentation.statusMutex = std::make_shared<std::mutex>();
    m_segmentation.statusText = std::make_shared<std::string>("Starting...");
    m_segmentation.processing = true;

    auto done = m_segmentation.done;
    auto cancelled = m_segmentation.cancelled;
    auto statusMtx = m_segmentation.statusMutex;
    auto statusTxt = m_segmentation.statusText;

    m_segmentation.workerThread = std::thread([imagePath, outputPath, x1, y1, x2, y2, done, cancelled, statusMtx, statusTxt]() {
        if (cancelled->load()) { done->store(true); return; }

        std::string python = "/home/jasondube/Desktop/segment/.venv/bin/python3";
        std::string script = "/home/jasondube/Desktop/segment/segment_cli.py";

        std::string cmd = shellEscape(python) + " " + shellEscape(script)
            + " --input " + shellEscape(imagePath)
            + " --box " + std::to_string(x1) + " " + std::to_string(y1)
            + " " + std::to_string(x2) + " " + std::to_string(y2)
            + " --output " + shellEscape(outputPath)
            + " 2>&1";

        std::cerr << "[Segment] Running: " << cmd << std::endl;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "[Segment] popen failed!" << std::endl;
            done->store(true);
            return;
        }
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (!line.empty()) std::cerr << "[Segment] " << line << std::endl;
            if (line.rfind("STATUS ", 0) == 0) {
                std::lock_guard<std::mutex> lk(*statusMtx);
                *statusTxt = line.substr(7);
            }
        }
        int exitCode = pclose(pipe);
        std::cerr << "[Segment] Process exited with code " << exitCode << std::endl;
        done->store(true);
    });
}

void FilesystemBrowser::pollSegmentation() {
    if (!m_segmentation.processing || !m_segmentation.done) return;
    if (!m_segmentation.done->load()) return;

    // Join the worker thread
    if (m_segmentation.workerThread.joinable()) {
        m_segmentation.workerThread.join();
    }
    m_segmentation.processing = false;

    // If cancelled, just clean up
    if (m_segmentation.cancelled && m_segmentation.cancelled->load()) {
        std::remove(m_segmentation.outputPath.c_str());
        return;
    }

    // Load the output RGBA PNG
    if (!m_imageFocus.active || !m_imageFocus.panel || !m_modelRenderer) {
        std::remove(m_segmentation.outputPath.c_str());
        return;
    }

    int imgW, imgH, imgChannels;
    unsigned char* data = stbi_load(m_segmentation.outputPath.c_str(), &imgW, &imgH, &imgChannels, 4);
    if (!data) {
        std::cerr << "[Segmentation] Failed to load result: " << m_segmentation.outputPath << std::endl;
        std::remove(m_segmentation.outputPath.c_str());
        return;
    }

    // Cap to FOCUS_MAX_SIZE preserving aspect ratio (same as focusImage)
    int capW = imgW, capH = imgH;
    int maxDim = std::max(imgW, imgH);
    if (maxDim > FOCUS_MAX_SIZE) {
        float scale = static_cast<float>(FOCUS_MAX_SIZE) / maxDim;
        capW = static_cast<int>(imgW * scale);
        capH = static_cast<int>(imgH * scale);
        if (capW < 1) capW = 1;
        if (capH < 1) capH = 1;
    }

    // Resample into raw RGBA buffer (image-space, top-down for eraser)
    m_segmentation.rgbaW = capW;
    m_segmentation.rgbaH = capH;
    m_segmentation.rgbaData.resize(capW * capH * 4);
    for (int py = 0; py < capH; ++py) {
        int srcY = py * imgH / capH;
        for (int px = 0; px < capW; ++px) {
            int srcX = px * imgW / capW;
            int srcIdx = (srcY * imgW + srcX) * 4;
            int dstIdx = (py * capW + px) * 4;
            m_segmentation.rgbaData[dstIdx + 0] = data[srcIdx + 0];
            m_segmentation.rgbaData[dstIdx + 1] = data[srcIdx + 1];
            m_segmentation.rgbaData[dstIdx + 2] = data[srcIdx + 2];
            m_segmentation.rgbaData[dstIdx + 3] = data[srcIdx + 3];
        }
    }
    stbi_image_free(data);

    // Composite over checkerboard for display (flipped vertically for GPU)
    std::vector<unsigned char> pixels(capW * capH * 4);
    for (int py = 0; py < capH; ++py) {
        int srcY = capH - 1 - py; // flip for GPU
        for (int px = 0; px < capW; ++px) {
            int srcIdx = (srcY * capW + px) * 4;
            int dstIdx = (py * capW + px) * 4;
            unsigned char r = m_segmentation.rgbaData[srcIdx + 0];
            unsigned char g = m_segmentation.rgbaData[srcIdx + 1];
            unsigned char b = m_segmentation.rgbaData[srcIdx + 2];
            unsigned char a = m_segmentation.rgbaData[srcIdx + 3];
            bool light = ((px / 16) + (py / 16)) % 2 == 0;
            unsigned char bg = light ? 60 : 40;
            float af = a / 255.0f;
            pixels[dstIdx + 0] = static_cast<unsigned char>(r * af + bg * (1.0f - af));
            pixels[dstIdx + 1] = static_cast<unsigned char>(g * af + bg * (1.0f - af));
            pixels[dstIdx + 2] = static_cast<unsigned char>(b * af + bg * (1.0f - af));
            pixels[dstIdx + 3] = 255;
        }
    }

    std::cerr << "[Segment] Uploading segmented texture " << capW << "x" << capH
              << " to buffer " << m_imageFocus.bufferHandle << std::endl;
    m_modelRenderer->updateTexture(m_imageFocus.bufferHandle, pixels.data(), capW, capH);
    m_imageFocus.focusedWidth = capW;
    m_imageFocus.focusedHeight = capH;
    m_segmentation.active = true;

    // Load original image at same resolution for restore brush
    m_segmentation.originalData.resize(capW * capH * 4);
    int origW, origH, origC;
    unsigned char* origData = stbi_load(m_segmentation.imagePath.c_str(), &origW, &origH, &origC, 4);
    if (origData) {
        for (int py = 0; py < capH; ++py) {
            int srcY = py * origH / capH;
            for (int px = 0; px < capW; ++px) {
                int srcX = px * origW / capW;
                int srcIdx = (srcY * origW + srcX) * 4;
                int dstIdx = (py * capW + px) * 4;
                m_segmentation.originalData[dstIdx + 0] = origData[srcIdx + 0];
                m_segmentation.originalData[dstIdx + 1] = origData[srcIdx + 1];
                m_segmentation.originalData[dstIdx + 2] = origData[srcIdx + 2];
                m_segmentation.originalData[dstIdx + 3] = 255;
            }
        }
        stbi_image_free(origData);
    }

    // Clean up temp file
    std::remove(m_segmentation.outputPath.c_str());
    std::cerr << "[Segment] Done! Segmentation active." << std::endl;
}

void FilesystemBrowser::undoSegmentation() {
    if (!m_segmentation.active || !m_imageFocus.active || !m_imageFocus.panel || !m_modelRenderer) return;

    // Re-load original hi-res image (same logic as focusImage)
    std::string target = m_imageFocus.panel->getTargetLevel();
    if (target.size() <= 5 || target.substr(0, 5) != "fs://") return;
    std::string path = target.substr(5);

    int imgW, imgH, imgChannels;
    unsigned char* data = stbi_load(path.c_str(), &imgW, &imgH, &imgChannels, 4);
    if (!data) return;

    int capW = imgW, capH = imgH;
    int maxDim = std::max(imgW, imgH);
    if (maxDim > FOCUS_MAX_SIZE) {
        float scale = static_cast<float>(FOCUS_MAX_SIZE) / maxDim;
        capW = static_cast<int>(imgW * scale);
        capH = static_cast<int>(imgH * scale);
        if (capW < 1) capW = 1;
        if (capH < 1) capH = 1;
    }

    std::vector<unsigned char> hiRes(capW * capH * 4);
    for (int py = 0; py < capH; ++py) {
        int srcY = (capH - 1 - py) * imgH / capH;
        for (int px = 0; px < capW; ++px) {
            int srcX = px * imgW / capW;
            int srcIdx = (srcY * imgW + srcX) * 4;
            int dstIdx = (py * capW + px) * 4;
            hiRes[dstIdx + 0] = data[srcIdx + 0];
            hiRes[dstIdx + 1] = data[srcIdx + 1];
            hiRes[dstIdx + 2] = data[srcIdx + 2];
            hiRes[dstIdx + 3] = data[srcIdx + 3];
        }
    }
    stbi_image_free(data);

    m_modelRenderer->updateTexture(m_imageFocus.bufferHandle, hiRes.data(), capW, capH);
    m_imageFocus.focusedWidth = capW;
    m_imageFocus.focusedHeight = capH;
    m_segmentation.active = false;
    m_segmentation.rgbaData.clear();
    m_segmentation.originalData.clear();
    m_segmentation.rgbaW = 0;
    m_segmentation.rgbaH = 0;
}

void FilesystemBrowser::eraseSegmentAt(int imgX, int imgY, int radius) {
    if (!m_segmentation.active || m_segmentation.rgbaData.empty()) return;
    int w = m_segmentation.rgbaW;
    int h = m_segmentation.rgbaH;
    if (w <= 0 || h <= 0) return;

    // Zero alpha in a circle around (imgX, imgY)
    int r2 = radius * radius;
    int x0 = std::max(0, imgX - radius);
    int x1 = std::min(w - 1, imgX + radius);
    int y0 = std::max(0, imgY - radius);
    int y1 = std::min(h - 1, imgY + radius);
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            int dx = px - imgX, dy = py - imgY;
            if (dx * dx + dy * dy <= r2) {
                m_segmentation.rgbaData[(py * w + px) * 4 + 3] = 0;
            }
        }
    }

    // Re-composite over checkerboard and upload (flipped for GPU)
    std::vector<unsigned char> pixels(w * h * 4);
    for (int py = 0; py < h; ++py) {
        int srcY = h - 1 - py;
        for (int px = 0; px < w; ++px) {
            int srcIdx = (srcY * w + px) * 4;
            int dstIdx = (py * w + px) * 4;
            unsigned char r = m_segmentation.rgbaData[srcIdx + 0];
            unsigned char g = m_segmentation.rgbaData[srcIdx + 1];
            unsigned char b = m_segmentation.rgbaData[srcIdx + 2];
            unsigned char a = m_segmentation.rgbaData[srcIdx + 3];
            bool light = ((px / 16) + (py / 16)) % 2 == 0;
            unsigned char bg = light ? 60 : 40;
            float af = a / 255.0f;
            pixels[dstIdx + 0] = static_cast<unsigned char>(r * af + bg * (1.0f - af));
            pixels[dstIdx + 1] = static_cast<unsigned char>(g * af + bg * (1.0f - af));
            pixels[dstIdx + 2] = static_cast<unsigned char>(b * af + bg * (1.0f - af));
            pixels[dstIdx + 3] = 255;
        }
    }
    m_modelRenderer->updateTexture(m_imageFocus.bufferHandle, pixels.data(), w, h);
}

void FilesystemBrowser::restoreSegmentAt(int imgX, int imgY, int radius) {
    if (!m_segmentation.active || m_segmentation.rgbaData.empty() || m_segmentation.originalData.empty()) return;
    int w = m_segmentation.rgbaW;
    int h = m_segmentation.rgbaH;
    if (w <= 0 || h <= 0) return;

    // Copy RGB from original and set alpha=255 in a circle
    int r2 = radius * radius;
    int x0 = std::max(0, imgX - radius);
    int x1 = std::min(w - 1, imgX + radius);
    int y0 = std::max(0, imgY - radius);
    int y1 = std::min(h - 1, imgY + radius);
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            int dx = px - imgX, dy = py - imgY;
            if (dx * dx + dy * dy <= r2) {
                int idx = (py * w + px) * 4;
                m_segmentation.rgbaData[idx + 0] = m_segmentation.originalData[idx + 0];
                m_segmentation.rgbaData[idx + 1] = m_segmentation.originalData[idx + 1];
                m_segmentation.rgbaData[idx + 2] = m_segmentation.originalData[idx + 2];
                m_segmentation.rgbaData[idx + 3] = 255;
            }
        }
    }

    // Re-composite and upload
    std::vector<unsigned char> pixels(w * h * 4);
    for (int py = 0; py < h; ++py) {
        int srcY = h - 1 - py;
        for (int px = 0; px < w; ++px) {
            int srcIdx = (srcY * w + px) * 4;
            int dstIdx = (py * w + px) * 4;
            unsigned char r = m_segmentation.rgbaData[srcIdx + 0];
            unsigned char g = m_segmentation.rgbaData[srcIdx + 1];
            unsigned char b = m_segmentation.rgbaData[srcIdx + 2];
            unsigned char a = m_segmentation.rgbaData[srcIdx + 3];
            bool light = ((px / 16) + (py / 16)) % 2 == 0;
            unsigned char bg = light ? 60 : 40;
            float af = a / 255.0f;
            pixels[dstIdx + 0] = static_cast<unsigned char>(r * af + bg * (1.0f - af));
            pixels[dstIdx + 1] = static_cast<unsigned char>(g * af + bg * (1.0f - af));
            pixels[dstIdx + 2] = static_cast<unsigned char>(b * af + bg * (1.0f - af));
            pixels[dstIdx + 3] = 255;
        }
    }
    m_modelRenderer->updateTexture(m_imageFocus.bufferHandle, pixels.data(), w, h);
}

void FilesystemBrowser::cancelSegmentation() {
    if (m_segmentation.processing) {
        if (m_segmentation.cancelled) m_segmentation.cancelled->store(true);
        if (m_segmentation.workerThread.joinable()) m_segmentation.workerThread.join();
        m_segmentation.processing = false;
        std::remove(m_segmentation.outputPath.c_str());
    }
    m_segmentation.active = false;
    m_segmentation.done.reset();
    m_segmentation.cancelled.reset();
}

std::string FilesystemBrowser::saveSegmentationToForge() {
    if (!m_segmentation.active || m_segmentation.rgbaData.empty()) return "";

    // Find the forge (assets/models) directory
    std::string forgeDir;
    for (const auto& candidate : {
        "assets/models",
        "../../../examples/terrain_editor/assets/models",
        "examples/terrain_editor/assets/models"
    }) {
        std::error_code ec;
        auto p = std::filesystem::canonical(candidate, ec);
        if (!ec) { forgeDir = p.string(); break; }
    }
    if (forgeDir.empty()) {
        forgeDir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/assets/models";
        std::filesystem::create_directories(forgeDir);
    }

    // Generate filename from source image name + timestamp
    std::string baseName = "segment";
    if (!m_segmentation.imagePath.empty()) {
        auto stem = std::filesystem::path(m_segmentation.imagePath).stem().string();
        if (!stem.empty()) baseName = stem;
    }
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    std::string outPath = forgeDir + "/" + baseName + "_" + std::to_string(secs) + ".png";

    // Pre-multiply alpha: zero out RGB where alpha is 0 so the background
    // is truly removed (not just hidden behind alpha channel)
    int w = m_segmentation.rgbaW;
    int h = m_segmentation.rgbaH;
    std::vector<unsigned char> saveData(m_segmentation.rgbaData.begin(),
                                         m_segmentation.rgbaData.end());
    for (int i = 0; i < w * h; ++i) {
        if (saveData[i * 4 + 3] == 0) {
            saveData[i * 4 + 0] = 0;
            saveData[i * 4 + 1] = 0;
            saveData[i * 4 + 2] = 0;
        }
    }

    // Write RGBA data as PNG
    int ok = stbi_write_png(outPath.c_str(), w, h, 4,
                            saveData.data(), w * 4);
    if (ok) {
        std::cout << "[FilesystemBrowser] Saved segmentation to forge: " << outPath << std::endl;
        return outPath;
    } else {
        std::cerr << "[FilesystemBrowser] Failed to save segmentation to: " << outPath << std::endl;
        return "";
    }
}

// ── Emanation Render Data ───────────────────────────────────────────────

std::vector<FilesystemBrowser::EmanationBatch>
FilesystemBrowser::getEmanationRenderData() const {
    std::vector<EmanationBatch> batches;
    if (m_emanationRings.empty()) return batches;

    float maxAge = EMANATION_MAX_DIST / EMANATION_SPEED;

    for (const auto& ring : m_emanationRings) {
        if (ring.emanationIdx >= m_emanations.size()) continue;
        const auto& em = m_emanations[ring.emanationIdx];

        float t = ring.age / maxAge;  // 0..1 normalized lifetime
        float dist = ring.age * EMANATION_SPEED;

        // Alpha fades out over distance
        float alpha = (1.0f - t) * em.intensity;
        if (alpha < 0.01f) continue;

        // Scale up slightly as it travels outward (1x to 1.3x)
        float scaleMult = 1.0f + t * 0.3f;

        // Compute the 4 corners of the wireframe square
        glm::vec3 center = em.center + em.forward * dist;
        glm::vec3 r = em.right * em.halfExtent.x * scaleMult;
        glm::vec3 u = em.up * em.halfExtent.y * scaleMult;

        glm::vec3 c0 = center - r - u;
        glm::vec3 c1 = center + r - u;
        glm::vec3 c2 = center + r + u;
        glm::vec3 c3 = center - r + u;

        // 4 line segments forming the square (8 vec3 points = 4 line pairs)
        EmanationBatch batch;
        batch.lines = {c0, c1, c1, c2, c2, c3, c3, c0};
        // White-blue color tinted by intensity
        batch.color = glm::vec4(0.6f + 0.4f * em.intensity,
                                0.7f + 0.3f * em.intensity,
                                1.0f,
                                alpha);
        batches.push_back(std::move(batch));
    }

    return batches;
}

} // namespace eden
