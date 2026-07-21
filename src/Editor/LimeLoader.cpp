#include "LimeLoader.hpp"
#include "SceneObject.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    glm::ivec4 boneIndices = glm::ivec4(0);   // v3.0 skinning (optional)
    glm::vec4 boneWeights = glm::vec4(0.0f);
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

    // RIG_RUNTIME capture (bind-pose verts/heads + world-space animation keys)
    std::vector<glm::vec3> rigBpVerts;
    std::vector<glm::vec3> rigBpBonePos;
    std::vector<float> rigTimes;
    std::vector<std::vector<glm::vec3>> rigKPos;
    std::vector<std::vector<glm::quat>> rigKRot;

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
                // v3.0 skinning: optional "| bi0 bi1 bi2 bi3 | bw0 bw1 bw2 bw3"
                char pipe5, pipe6;
                glm::ivec4 bi; glm::vec4 bw;
                if (iss >> pipe5 >> bi.x >> bi.y >> bi.z >> bi.w
                        >> pipe6 >> bw.x >> bw.y >> bw.z >> bw.w) {
                    v.boneIndices = bi;
                    v.boneWeights = bw;
                }
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
        else if (type == "bone") {
            // v3.0 skeleton: bone i: parent "name" | 16 IBM floats | 16 local floats
            uint32_t idx; int parent; char colon;
            iss >> idx >> colon >> parent;
            size_t q1 = line.find('"');
            size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string bname = line.substr(q1 + 1, q2 - q1 - 1);
            std::istringstream ms(line.substr(q2 + 1));
            char pipe;
            Bone bone;
            bone.name = bname;
            bone.parentIndex = parent;
            float* ibm = &bone.inverseBindMatrix[0][0];
            ms >> pipe;
            for (int j = 0; j < 16; ++j) ms >> ibm[j];
            float* lt = &bone.localTransform[0][0];
            ms >> pipe;
            for (int j = 0; j < 16; ++j) ms >> lt[j];
            if (idx >= result.mesh.skeleton.bones.size())
                result.mesh.skeleton.bones.resize(idx + 1);
            result.mesh.skeleton.bones[idx] = bone;
            result.mesh.skeleton.boneNameToIndex[bname] = static_cast<int>(idx);
            result.mesh.hasSkeleton = true;
        }
        else if (type == "RIG_RUNTIME_BEGIN") {
            // Editor rig-runtime blob: bind-pose bone heads + keyframed animation
            // (world-space heads + world rotation deltas per bone per key).
            rigBpBonePos.clear(); rigTimes.clear(); rigKPos.clear(); rigKRot.clear();
            std::string rl;
            auto readVec3s = [&](size_t n, std::vector<glm::vec3>* into) {
                for (size_t i = 0; i < n && std::getline(file, rl); ++i) {
                    if (into) {
                        std::istringstream vs(rl);
                        glm::vec3 p; vs >> p.x >> p.y >> p.z;
                        into->push_back(p);
                    }
                }
            };
            auto readQuats = [&](size_t n, std::vector<glm::quat>* into) {
                for (size_t i = 0; i < n && std::getline(file, rl); ++i) {
                    if (into) {
                        std::istringstream qs(rl);
                        glm::quat q; qs >> q.w >> q.x >> q.y >> q.z;
                        into->push_back(glm::normalize(q));
                    }
                }
            };
            while (std::getline(file, rl)) {
                if (rl == "RIG_RUNTIME_END") break;
                std::istringstream rs(rl);
                std::string tok; rs >> tok;
                size_t n = 0;
                if (tok == "bonepos")        { rs >> n; readVec3s(n, nullptr); }
                else if (tok == "bpverts")   { rs >> n; readVec3s(n, &rigBpVerts); }
                else if (tok == "bpbonepos") { rs >> n; readVec3s(n, &rigBpBonePos); }
                else if (tok == "bpbonerot") { rs >> n; readQuats(n, nullptr); }
                else if (tok == "key")       { float t = 0; rs >> t; rigTimes.push_back(t); }
                else if (tok == "kbonepos")  { rs >> n; rigKPos.emplace_back(); readVec3s(n, &rigKPos.back()); }
                else if (tok == "kbonerot")  { rs >> n; rigKRot.emplace_back(); readQuats(n, &rigKRot.back()); }
                // version/bindpose/anim/tp/tr/ts/ik* lines: no payload to skip
            }
        }
        // We don't need half-edge data for rendering, skip "he" lines
    }

    file.close();

    // Rebuild the skeleton translation-only from the bind-pose bone heads
    // (exactly what LIME's Set Bind Pose produced), then convert the world-space
    // keys into per-bone LOCAL TRS channels for the engine AnimationPlayer.
    // FK of these locals reproduces world_b = translate(P)*mat(Q), so skinning
    // world*IBM = translate(P)*mat(Q)*translate(-H) == LIME's reskin exactly.
    if (result.mesh.hasSkeleton && !rigBpBonePos.empty()) {
        auto& bones = result.mesh.skeleton.bones;
        size_t nb = std::min(bones.size(), rigBpBonePos.size());
        for (size_t b = 0; b < nb; ++b) {
            int p = bones[b].parentIndex;
            glm::vec3 parentPos = (p >= 0 && p < static_cast<int>(nb)) ? rigBpBonePos[p] : glm::vec3(0.0f);
            bones[b].localTransform = glm::translate(glm::mat4(1.0f), rigBpBonePos[b] - parentPos);
            bones[b].inverseBindMatrix = glm::translate(glm::mat4(1.0f), -rigBpBonePos[b]);
        }
        result.mesh.skeleton.rootTransform = glm::mat4(1.0f);
    }
    if (result.mesh.hasSkeleton && !rigTimes.empty() && rigKPos.size() == rigTimes.size()) {
        const size_t nb = result.mesh.skeleton.bones.size();
        AnimationClip clip;
        clip.name = "lime_anim";
        clip.duration = rigTimes.back();
        clip.channels.resize(nb);
        for (size_t b = 0; b < nb; ++b) clip.channels[b].boneIndex = static_cast<int>(b);
        for (size_t k = 0; k < rigTimes.size(); ++k) {
            const auto& P = rigKPos[k];
            const bool haveRot = (k < rigKRot.size() && rigKRot[k].size() == P.size());
            for (size_t b = 0; b < nb && b < P.size(); ++b) {
                int p = result.mesh.skeleton.bones[b].parentIndex;
                glm::quat Qb = haveRot ? rigKRot[k][b] : glm::quat(1, 0, 0, 0);
                glm::vec3 T; glm::quat R;
                if (p < 0 || p >= static_cast<int>(P.size())) {
                    T = P[b]; R = Qb;
                } else {
                    glm::quat Qp = haveRot ? rigKRot[k][p] : glm::quat(1, 0, 0, 0);
                    glm::quat QpInv = glm::inverse(Qp);
                    T = QpInv * (P[b] - P[p]);
                    R = glm::normalize(QpInv * Qb);
                }
                auto& ch = clip.channels[b];
                ch.positionTimes.push_back(rigTimes[k]);
                ch.positions.push_back(T);
                ch.rotationTimes.push_back(rigTimes[k]);
                ch.rotations.push_back(R);
            }
        }
        result.mesh.animClip = std::move(clip);
        result.mesh.hasAnimation = true;
        std::cout << "[LimeLoader] rig animation: " << rigTimes.size() << " keys x "
                  << nb << " bones (" << result.mesh.animClip.duration << "s)" << std::endl;
    }

    // Convert to triangulated mesh for GPU
    // First, create ModelVertex array from lime vertices
    result.mesh.vertices.reserve(limeVertices.size());
    result.mesh.boneIndices.reserve(limeVertices.size());
    result.mesh.boneWeights.reserve(limeVertices.size());
    for (const auto& lv : limeVertices) {
        ModelVertex mv;
        mv.position = lv.position;
        mv.normal = lv.normal;
        mv.texCoord = lv.uv;
        mv.color = lv.color;
        result.mesh.vertices.push_back(mv);
        result.mesh.boneIndices.push_back(lv.boneIndices);
        result.mesh.boneWeights.push_back(lv.boneWeights);
    }

    // If the file was saved mid-pose, the mesh verts are DEFORMED. Skinning
    // needs the BIND-pose verts (v' = Q*(v-H)+P assumes v at bind), which the
    // rig blob preserves — restore them when available.
    if (result.mesh.hasAnimation && rigBpVerts.size() == result.mesh.vertices.size()) {
        for (size_t i = 0; i < rigBpVerts.size(); ++i)
            result.mesh.vertices[i].position = rigBpVerts[i];
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
