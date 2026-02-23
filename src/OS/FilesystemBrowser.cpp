#include "OS/FilesystemBrowser.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Terminal/EdenTerminalFont.inc"

#include <stb_image.h>
#include <algorithm>
#include <iostream>
#include <fstream>
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
    m_cancelExtraction.reset();
}

// ── Init / Navigate / Clear ────────────────────────────────────────────

FilesystemBrowser::~FilesystemBrowser() {
    cancelAllExtractions();
}

void FilesystemBrowser::init(ModelRenderer* modelRenderer,
                             std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                             Terrain* terrain) {
    m_modelRenderer = modelRenderer;
    m_sceneObjects = sceneObjects;
    m_terrain = terrain;
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
        std::cout << "[FilesystemBrowser] Navigated to: " << m_currentPath << std::endl;
    }
    m_active = true;
}

void FilesystemBrowser::updateAnimations(float deltaTime) {
    if (!m_modelRenderer) return;

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
                std::cout << "[FilesystemBrowser] Video loaded: " << anim.frames.size() << " frames" << std::endl;
            }
            anim.loaded = true;
            anim.pendingFrames.reset();
            anim.ready.reset();
        }

        if (anim.frames.size() <= 1) continue;

        anim.timer += deltaTime;
        if (anim.timer >= VIDEO_FRAME_INTERVAL) {
            anim.timer -= VIDEO_FRAME_INTERVAL;
            anim.currentFrame = (anim.currentFrame + 1) % static_cast<int>(anim.frames.size());

            m_modelRenderer->updateTexture(
                anim.bufferHandle,
                anim.frames[anim.currentFrame].data(),
                LABEL_SIZE, LABEL_SIZE);
        }
    }
}

void FilesystemBrowser::clearFilesystemObjects() {
    if (!m_sceneObjects || !m_modelRenderer) return;

    cancelAllExtractions();
    m_videoAnimations.clear();

    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        const auto& bt = (*it) ? (*it)->getBuildingType() : "";
        if (*it && (bt == "filesystem" || bt == "filesystem_wall")) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) {
                m_modelRenderer->destroyModel(handle);
            }
            it = m_sceneObjects->erase(it);
        } else {
            ++it;
        }
    }
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

            m_extractionThreads.emplace_back(
                extractionWorker,
                entry.fullPath, cachePath,
                LABEL_SIZE, MAX_VIDEO_FRAMES,
                anim.pendingFrames, anim.ready, m_cancelExtraction);
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
    float radius = GALLERY_RADIUS;
    float segmentAngle = 2.0f * M_PI / GALLERY_SIDES;
    float segmentWidth = 2.0f * radius * sinf(segmentAngle / 2.0f);

    // Determine scale based on type (all items in this call are same type)
    FileCategory cat = items[0].category;
    bool isDoorType = (cat == FileCategory::Folder);

    for (int placed = 0; placed < totalItems; ) {
        int itemsThisLevel = std::min(GALLERY_SIDES, totalItems - placed);
        float levelY = baseY + level * GALLERY_WALL_HEIGHT;

        for (int s = 0; s < GALLERY_SIDES; ++s) {
            float angle = s * segmentAngle;
            float wallX = center.x + radius * cosf(angle);
            float wallZ = center.z + radius * sinf(angle);
            float yawDeg = -angle * 180.0f / M_PI + 90.0f;

            // Spawn dark wall segment
            glm::vec4 wallColor(0.15f, 0.15f, 0.18f, 1.0f);
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
            // Tag wall with its ring category for paste-type matching
            switch (cat) {
                case FileCategory::Folder:     wallObj->setDescription("wall_folder"); break;
                case FileCategory::Image:      wallObj->setDescription("wall_image"); break;
                case FileCategory::Video:      wallObj->setDescription("wall_video"); break;
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

                float inset = 0.6f;
                float itemX = center.x + (radius - inset) * cosf(angle);
                float itemZ = center.z + (radius - inset) * sinf(angle);
                float itemY = levelY + 0.5f;

                glm::vec3 scale;
                if (isDoorType) {
                    // Doors: tall slab shape, fitting within wall
                    scale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 0.15f};
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

    // Position: inset 0.6 toward center from wall, raised 0.5 from wall base
    float inset = 0.6f;
    float itemX = wallPos.x - inset * cosf(angle);
    float itemZ = wallPos.z - inset * sinf(angle);
    float itemY = wallPos.y + 0.5f;

    // Scale: match gallery ring layout
    bool isDoorType = (cat == FileCategory::Folder);
    float segmentWidth = wallScale.x; // wall scale X == segmentWidth
    glm::vec3 scale;
    if (isDoorType) {
        scale = {segmentWidth * 0.5f / 2.0f, (GALLERY_WALL_HEIGHT - 1.0f) / 2.0f, 0.15f};
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

// ── Spawn Objects ──────────────────────────────────────────────────────

void FilesystemBrowser::spawnObjects(const std::string& dirPath) {
    if (!m_modelRenderer || !m_sceneObjects || !m_terrain) return;

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
            if (entries.size() >= MAX_ENTRIES) break;
            std::string name = entry.path().filename().string();
            FileCategory cat = categorize(entry);
            std::string full = entry.path().string();
            entries.push_back({name, full, cat});
        }
    } catch (const std::exception& e) {
        std::cerr << "[FilesystemBrowser] Error scanning " << dirPath << ": " << e.what() << std::endl;
    }

    // Split entries by type
    std::vector<EntryInfo> folders;
    std::vector<EntryInfo> images;
    std::vector<EntryInfo> videos;
    std::vector<EntryInfo> others; // text, source, exe, other

    for (auto& e : entries) {
        switch (e.category) {
            case FileCategory::Folder:  folders.push_back(std::move(e)); break;
            case FileCategory::Image:   images.push_back(std::move(e));  break;
            case FileCategory::Video:   videos.push_back(std::move(e));  break;
            default:                    others.push_back(std::move(e));   break;
        }
    }

    // Sort each group alphabetically (.. always first in folders)
    auto sortAlpha = [](std::vector<EntryInfo>& v) {
        std::sort(v.begin(), v.end(), [](const EntryInfo& a, const EntryInfo& b) {
            if (a.name == "..") return true;
            if (b.name == "..") return false;
            return a.name < b.name;
        });
    };
    sortAlpha(folders);
    sortAlpha(images);
    sortAlpha(videos);
    std::sort(others.begin(), others.end(), [](const EntryInfo& a, const EntryInfo& b) {
        return a.name < b.name;
    });

    float baseY = m_terrain->getHeightAt(m_spawnOrigin.x, m_spawnOrigin.z);
    glm::vec3 center = m_spawnOrigin;
    center.y = baseY;

    bool hasRing = !folders.empty() || !images.empty() || !videos.empty();

    // Stack types on the gallery ring: folders (bottom), images, videos
    int nextLevel = 0;
    nextLevel = spawnGalleryRing(folders, center, baseY, nextLevel);
    nextLevel = spawnGalleryRing(images,  center, baseY, nextLevel);
    nextLevel = spawnGalleryRing(videos,  center, baseY, nextLevel);

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

    size_t total = folders.size() + images.size() + videos.size() + others.size();
    std::cout << "[FilesystemBrowser] Spawned " << total
              << " objects for " << dirPath << std::endl;
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

} // namespace eden
