#include "LimeLoader.hpp"
#include "SceneObject.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>

namespace eden {

std::string LimeLoader::resolvePath(const std::string& path) {
    // 1) Try as-is (absolute or relative to CWD)
    if (std::filesystem::exists(path)) return path;

    // 2) Try under assets/models/ with just the filename
    std::string filename = std::filesystem::path(path).filename().string();
    std::string modelsDir = "assets/models";
    if (std::filesystem::exists(modelsDir)) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(modelsDir)) {
            if (entry.is_regular_file() && entry.path().filename().string() == filename) {
                return entry.path().string();
            }
        }
    }

    // 3) Return original path (will fail on load, but error message shows what's missing)
    return path;
}

std::string LimeLoader::toRelativePath(const std::string& absolutePath) {
    // If already relative, return as-is
    if (!std::filesystem::path(absolutePath).is_absolute()) return absolutePath;

    std::string filename = std::filesystem::path(absolutePath).filename().string();

    // Search assets/models/ for this filename and return relative path
    std::string modelsDir = "assets/models";
    if (std::filesystem::exists(modelsDir)) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(modelsDir)) {
            if (entry.is_regular_file() && entry.path().filename().string() == filename) {
                return entry.path().string();
            }
        }
    }

    // Fallback: return just the filename
    return filename;
}

// Base64 decoding
static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<unsigned char> base64_decode(const std::string& encoded) {
    std::vector<unsigned char> ret;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)base64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            ret.push_back((unsigned char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return ret;
}

// Internal structures for parsing
struct LimeVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color = glm::vec4(1.0f);
    uint32_t halfEdgeIndex;
    bool selected;
};

struct LimeFace {
    uint32_t halfEdgeIndex;
    uint32_t vertexCount;
    bool selected;
    std::vector<uint32_t> vertexIndices;
};

LimeLoader::LoadResult LimeLoader::load(const std::string& filepath) {
    LoadResult result;

    // Resolve path — handles absolute, relative, and asset paths
    std::string resolvedPath = resolvePath(filepath);
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + filepath + " (resolved: " + resolvedPath + ")";
        return result;
    }

    result.mesh.name = std::filesystem::path(filepath).stem().string();

    std::vector<LimeVertex> limeVertices;
    std::vector<LimeFace> limeFaces;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "transform_pos:") {
            iss >> result.mesh.position.x >> result.mesh.position.y >> result.mesh.position.z;
        }
        else if (type == "transform_rot:") {
            // Stored as quaternion (w x y z), convert to euler degrees
            float w, x, y, z;
            iss >> w >> x >> y >> z;
            glm::quat q(w, x, y, z);
            result.mesh.rotation = glm::degrees(glm::eulerAngles(q));
        }
        else if (type == "transform_scale:") {
            iss >> result.mesh.scale.x >> result.mesh.scale.y >> result.mesh.scale.z;
        }
        else if (type == "tex_size:") {
            iss >> result.mesh.textureWidth >> result.mesh.textureHeight;
        }
        else if (type == "tex_data:") {
            std::string encoded;
            iss >> encoded;
            result.mesh.textureData = base64_decode(encoded);
            result.mesh.hasTexture = !result.mesh.textureData.empty() &&
                                      result.mesh.textureWidth > 0 &&
                                      result.mesh.textureHeight > 0;
        }
        else if (type == "v") {
            // Parse vertex: v idx: pos | nrm | uv | [col |] halfEdgeIdx selected
            uint32_t idx;
            char colon, pipe1, pipe2, pipe3, pipe4;
            LimeVertex v;
            int selected;

            iss >> idx >> colon
                >> v.position.x >> v.position.y >> v.position.z >> pipe1
                >> v.normal.x >> v.normal.y >> v.normal.z >> pipe2
                >> v.uv.x >> v.uv.y >> pipe3;

            // Try to read color (v2.0 format)
            float r, g, b, a;
            if (iss >> r >> g >> b >> a >> pipe4) {
                v.color = glm::vec4(r, g, b, a);
                iss >> v.halfEdgeIndex >> selected;
            } else {
                // Fallback for v1.0 format (no color)
                v.color = glm::vec4(1.0f);
                iss.clear();
                iss.seekg(0);
                std::string dummy;
                iss >> dummy >> idx >> colon
                    >> v.position.x >> v.position.y >> v.position.z >> pipe1
                    >> v.normal.x >> v.normal.y >> v.normal.z >> pipe2
                    >> v.uv.x >> v.uv.y >> pipe3
                    >> v.halfEdgeIndex >> selected;
            }
            v.selected = (selected != 0);

            if (idx >= limeVertices.size()) {
                limeVertices.resize(idx + 1);
            }
            limeVertices[idx] = v;
        }
        else if (type == "f") {
            // Parse face: f idx: halfEdgeIdx vertexCount selected | vertex_indices...
            uint32_t idx, heIdx, vertCount;
            int selected;
            char colon, pipe;

            iss >> idx >> colon >> heIdx >> vertCount >> selected >> pipe;

            LimeFace f;
            f.halfEdgeIndex = heIdx;
            f.vertexCount = vertCount;
            f.selected = (selected != 0);

            // Read vertex indices
            uint32_t vi;
            while (iss >> vi) {
                f.vertexIndices.push_back(vi);
            }

            if (idx >= limeFaces.size()) {
                limeFaces.resize(idx + 1);
            }
            limeFaces[idx] = f;
        }
        else if (type == "cp") {
            // Parse control point: cp idx: vertexIndex "name"
            uint32_t idx, vertIdx;
            char colon;
            iss >> idx >> colon >> vertIdx;
            // Read quoted name
            std::string cpName;
            std::getline(iss, cpName);
            // Strip leading whitespace and quotes
            size_t start = cpName.find('"');
            size_t end = cpName.rfind('"');
            if (start != std::string::npos && end != std::string::npos && end > start) {
                cpName = cpName.substr(start + 1, end - start - 1);
            } else {
                // No quotes — trim whitespace
                while (!cpName.empty() && cpName[0] == ' ') cpName.erase(0, 1);
            }
            result.mesh.controlPoints.push_back({vertIdx, cpName});
        }
        else if (type == "port") {
            // Parse: port idx: "name" px py pz | fx fy fz | ux uy uz
            uint32_t idx;
            char colon;
            iss >> idx >> colon;
            std::string rest;
            std::getline(iss, rest);
            Port port;
            size_t q1 = rest.find('"');
            size_t q2 = rest.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                port.name = rest.substr(q1 + 1, q2 - q1 - 1);
            }
            std::string nums = (q2 != std::string::npos) ? rest.substr(q2 + 1) : rest;
            for (char& c : nums) { if (c == '|') c = ' '; }
            std::istringstream niss(nums);
            niss >> port.position.x >> port.position.y >> port.position.z
                 >> port.forward.x >> port.forward.y >> port.forward.z
                 >> port.up.x >> port.up.y >> port.up.z;
            result.mesh.ports.push_back(port);
        }
        else if (type == "meta") {
            // Parse metadata: meta key: value
            std::string rest;
            std::getline(iss, rest);
            // Strip leading whitespace
            size_t start = rest.find_first_not_of(' ');
            if (start != std::string::npos) {
                rest = rest.substr(start);
            }
            // Split on first ": "
            size_t colonPos = rest.find(": ");
            if (colonPos != std::string::npos) {
                std::string key = rest.substr(0, colonPos);
                std::string value = rest.substr(colonPos + 2);
                result.mesh.metadata[key] = value;
            }
        }
        // We don't need half-edge data for rendering, skip "he" lines
    }

    file.close();

    // Convert to triangulated mesh for GPU
    // First, create ModelVertex array from lime vertices
    result.mesh.vertices.reserve(limeVertices.size());
    for (const auto& lv : limeVertices) {
        ModelVertex mv;
        mv.position = lv.position;
        mv.normal = lv.normal;
        mv.texCoord = lv.uv;
        mv.color = lv.color;
        result.mesh.vertices.push_back(mv);
    }

    // Triangulate faces (fan triangulation for quads and n-gons)
    for (const auto& face : limeFaces) {
        if (face.vertexIndices.size() < 3) continue;

        // Fan triangulation: first vertex is the hub
        for (size_t i = 1; i + 1 < face.vertexIndices.size(); ++i) {
            result.mesh.indices.push_back(face.vertexIndices[0]);
            result.mesh.indices.push_back(face.vertexIndices[i]);
            result.mesh.indices.push_back(face.vertexIndices[i + 1]);
        }
    }

    result.success = true;

    return result;
}

std::unique_ptr<SceneObject> LimeLoader::createSceneObject(
    const LoadedMesh& mesh,
    ModelRenderer& renderer
) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return nullptr;
    }

    auto obj = std::make_unique<SceneObject>(mesh.name);

    // Create GPU resources
    uint32_t handle = renderer.createModel(
        mesh.vertices,
        mesh.indices,
        mesh.hasTexture ? mesh.textureData.data() : nullptr,
        mesh.textureWidth,
        mesh.textureHeight
    );

    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setMeshData(mesh.vertices, mesh.indices);

    // Compute local bounds from vertex positions
    AABB bounds;
    bounds.min = glm::vec3(INFINITY);
    bounds.max = glm::vec3(-INFINITY);
    for (const auto& v : mesh.vertices) {
        bounds.min = glm::min(bounds.min, v.position);
        bounds.max = glm::max(bounds.max, v.position);
    }
    obj->setLocalBounds(bounds);

    if (mesh.hasTexture) {
        obj->setTextureData(mesh.textureData, mesh.textureWidth, mesh.textureHeight);
    }

    // Apply saved transform (scale is critical for preserving model dimensions)
    obj->getTransform().setScale(mesh.scale);
    obj->setEulerRotation(mesh.rotation);

    // Transfer control points
    if (!mesh.controlPoints.empty()) {
        std::vector<SceneObject::StoredControlPoint> storedCPs;
        for (const auto& cp : mesh.controlPoints) {
            storedCPs.push_back({cp.vertexIndex, cp.name});
        }
        obj->setControlPoints(storedCPs);
    }

    // Transfer connection ports
    if (!mesh.ports.empty()) {
        std::vector<SceneObject::StoredPort> storedPorts;
        for (const auto& p : mesh.ports) {
            storedPorts.push_back({p.name, p.position, p.forward, p.up});
        }
        obj->setPorts(storedPorts);
    }

    // Transfer metadata
    if (!mesh.metadata.empty()) {
        obj->setModelMetadata(mesh.metadata);

        // Apply collision type from metadata
        auto collIt = mesh.metadata.find("collision");
        if (collIt != mesh.metadata.end()) {
            if (collIt->second == "box")
                obj->setBulletCollisionType(BulletCollisionType::BOX);
            else if (collIt->second == "convex_hull")
                obj->setBulletCollisionType(BulletCollisionType::CONVEX_HULL);
            else if (collIt->second == "mesh")
                obj->setBulletCollisionType(BulletCollisionType::MESH);
        }
    }

    return obj;
}

LimeLoader::SceneResult LimeLoader::loadScene(const std::string& filepath) {
    SceneResult result;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        result.error = "Failed to open scene file: " + filepath;
        return result;
    }

    // Split the .limes file into per-object line blocks
    std::string line;
    std::vector<std::string> currentBlock;
    std::string currentName;
    bool inObject = false;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            if (inObject) currentBlock.push_back(line);
            continue;
        }

        if (line.rfind("OBJECT_BEGIN", 0) == 0) {
            inObject = true;
            currentBlock.clear();
            // Extract name from OBJECT_BEGIN "name"
            size_t q1 = line.find('"');
            size_t q2 = line.rfind('"');
            if (q1 != std::string::npos && q2 > q1)
                currentName = line.substr(q1 + 1, q2 - q1 - 1);
            else
                currentName = "unnamed";
            continue;
        }

        if (line == "OBJECT_END") {
            if (inObject && !currentBlock.empty()) {
                // Parse this block using the same logic as load()
                LoadedMesh mesh;
                mesh.name = currentName;

                std::vector<LimeVertex> limeVertices;
                std::vector<LimeFace> limeFaces;

                for (const auto& bline : currentBlock) {
                    if (bline.empty() || bline[0] == '#') continue;
                    std::istringstream iss(bline);
                    std::string type;
                    iss >> type;

                    if (type == "transform_pos:") {
                        iss >> mesh.position.x >> mesh.position.y >> mesh.position.z;
                    }
                    else if (type == "transform_rot:") {
                        float w, x, y, z;
                        iss >> w >> x >> y >> z;
                        glm::quat q(w, x, y, z);
                        mesh.rotation = glm::degrees(glm::eulerAngles(q));
                    }
                    else if (type == "transform_scale:") {
                        iss >> mesh.scale.x >> mesh.scale.y >> mesh.scale.z;
                    }
                    else if (type == "tex_size:") {
                        iss >> mesh.textureWidth >> mesh.textureHeight;
                    }
                    else if (type == "tex_data:") {
                        std::string encoded;
                        iss >> encoded;
                        mesh.textureData = base64_decode(encoded);
                        mesh.hasTexture = !mesh.textureData.empty() &&
                                           mesh.textureWidth > 0 &&
                                           mesh.textureHeight > 0;
                    }
                    else if (type == "v") {
                        uint32_t idx;
                        char colon, pipe1, pipe2, pipe3, pipe4;
                        LimeVertex v;
                        int selected;

                        iss >> idx >> colon
                            >> v.position.x >> v.position.y >> v.position.z >> pipe1
                            >> v.normal.x >> v.normal.y >> v.normal.z >> pipe2
                            >> v.uv.x >> v.uv.y >> pipe3;

                        float r, g, b, a;
                        if (iss >> r >> g >> b >> a >> pipe4) {
                            v.color = glm::vec4(r, g, b, a);
                            iss >> v.halfEdgeIndex >> selected;
                        } else {
                            v.color = glm::vec4(1.0f);
                            iss.clear();
                            iss.seekg(0);
                            std::string dummy;
                            iss >> dummy >> idx >> colon
                                >> v.position.x >> v.position.y >> v.position.z >> pipe1
                                >> v.normal.x >> v.normal.y >> v.normal.z >> pipe2
                                >> v.uv.x >> v.uv.y >> pipe3
                                >> v.halfEdgeIndex >> selected;
                        }
                        v.selected = (selected != 0);
                        if (idx >= limeVertices.size()) limeVertices.resize(idx + 1);
                        limeVertices[idx] = v;
                    }
                    else if (type == "f") {
                        uint32_t idx, heIdx, vertCount;
                        int selected;
                        char colon, pipe;
                        iss >> idx >> colon >> heIdx >> vertCount >> selected >> pipe;

                        LimeFace f;
                        f.halfEdgeIndex = heIdx;
                        f.vertexCount = vertCount;
                        f.selected = (selected != 0);
                        uint32_t vi;
                        while (iss >> vi) f.vertexIndices.push_back(vi);
                        if (idx >= limeFaces.size()) limeFaces.resize(idx + 1);
                        limeFaces[idx] = f;
                    }
                    else if (type == "cp") {
                        uint32_t idx, vertIdx;
                        char colon;
                        iss >> idx >> colon >> vertIdx;
                        std::string cpName;
                        std::getline(iss, cpName);
                        size_t start = cpName.find('"');
                        size_t end = cpName.rfind('"');
                        if (start != std::string::npos && end != std::string::npos && end > start)
                            cpName = cpName.substr(start + 1, end - start - 1);
                        else
                            while (!cpName.empty() && cpName[0] == ' ') cpName.erase(0, 1);
                        mesh.controlPoints.push_back({vertIdx, cpName});
                    }
                    else if (type == "port") {
                        uint32_t idx;
                        char colon;
                        iss >> idx >> colon;
                        std::string rest;
                        std::getline(iss, rest);
                        Port port;
                        size_t q1 = rest.find('"');
                        size_t q2 = rest.find('"', q1 + 1);
                        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                            port.name = rest.substr(q1 + 1, q2 - q1 - 1);
                        }
                        std::string nums = (q2 != std::string::npos) ? rest.substr(q2 + 1) : rest;
                        for (char& c : nums) { if (c == '|') c = ' '; }
                        std::istringstream niss(nums);
                        niss >> port.position.x >> port.position.y >> port.position.z
                             >> port.forward.x >> port.forward.y >> port.forward.z
                             >> port.up.x >> port.up.y >> port.up.z;
                        mesh.ports.push_back(port);
                    }
                    else if (type == "meta") {
                        std::string rest;
                        std::getline(iss, rest);
                        size_t start = rest.find_first_not_of(' ');
                        if (start != std::string::npos) rest = rest.substr(start);
                        size_t colonPos = rest.find(": ");
                        if (colonPos != std::string::npos)
                            mesh.metadata[rest.substr(0, colonPos)] = rest.substr(colonPos + 2);
                    }
                }

                // Convert vertices
                mesh.vertices.reserve(limeVertices.size());
                for (const auto& lv : limeVertices) {
                    ModelVertex mv;
                    mv.position = lv.position;
                    mv.normal = lv.normal;
                    mv.texCoord = lv.uv;
                    mv.color = lv.color;
                    mesh.vertices.push_back(mv);
                }

                // Triangulate faces
                for (const auto& face : limeFaces) {
                    if (face.vertexIndices.size() < 3) continue;
                    for (size_t i = 1; i + 1 < face.vertexIndices.size(); ++i) {
                        mesh.indices.push_back(face.vertexIndices[0]);
                        mesh.indices.push_back(face.vertexIndices[i]);
                        mesh.indices.push_back(face.vertexIndices[i + 1]);
                    }
                }

                std::cout << "  Object: " << currentName << " ("
                          << limeVertices.size() << " verts, "
                          << limeFaces.size() << " faces";
                if (!mesh.controlPoints.empty())
                    std::cout << ", " << mesh.controlPoints.size() << " CPs";
                std::cout << ")" << std::endl;

                result.objects.push_back(std::move(mesh));
            }
            inObject = false;
            continue;
        }

        if (inObject) currentBlock.push_back(line);
    }

    file.close();

    result.success = !result.objects.empty();
    if (result.success)
        std::cout << "Loaded LIMES scene: " << filepath << " (" << result.objects.size() << " objects)" << std::endl;
    else
        result.error = "No objects found in scene file";

    return result;
}

} // namespace eden
