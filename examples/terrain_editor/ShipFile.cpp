#include "ShipFile.hpp"

#include "Editor/PrimitiveMeshBuilder.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

using eden::SceneObject;
using nlohmann::json;

namespace shipfile {
namespace {

// ---- base64 (texture blobs travel inside the JSON) -------------------------
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64encode(const unsigned char* data, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = data[i] << 16;
        if (i + 1 < n) v |= data[i + 1] << 8;
        if (i + 2 < n) v |= data[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? kB64[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < n ? kB64[v & 63] : '=');
    }
    return out;
}

std::vector<unsigned char> b64decode(const std::string& s) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<unsigned char> out;
    out.reserve(s.size() / 4 * 3);
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        const int d = val(c);
        if (d < 0) continue;
        v = (v << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((v >> bits) & 0xFF));
        }
    }
    return out;
}

uint64_t blobHash(const std::vector<unsigned char>& d) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : d) { h ^= c; h *= 1099511628211ull; }
    return h;
}

SceneObject* findByName(Host& host, const std::string& n) {
    for (auto& o : *host.sceneObjects)
        if (o && o->getName() == n) return o.get();
    return nullptr;
}

json vec3j(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
glm::vec3 jvec3(const json& j) { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }

}  // namespace

std::string save(Host& host, const glm::vec3& nearPos, const std::string& path) {
    const std::vector<std::string> names = host.surveyShip(nearPos);
    if (names.empty()) return "no ship near enough to save";

    std::vector<SceneObject*> members;
    for (const auto& n : names)
        if (SceneObject* o = findByName(host, n)) members.push_back(o);
    if (members.empty()) return "the survey came back empty";

    // Origin: hull footprint centre at the keel (lowest bound).
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (SceneObject* o : members) {
        const eden::AABB wb = o->getWorldBounds();
        mn = glm::min(mn, wb.min);
        mx = glm::max(mx, wb.max);
    }
    const glm::vec3 origin((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);

    json doc;
    doc["format"] = "eden-ship";
    doc["version"] = 1;
    doc["name"] = std::filesystem::path(path).stem().string();
    doc["crew"] = json::array();   // reserved: robots and NPCs, when they board

    // Texture blobs, deduplicated: a hull painted in four textures stores
    // four, however many hundred plates wear them.
    std::vector<std::pair<uint64_t, json>> texTable;
    auto textureRef = [&](SceneObject* o) -> int {
        if (!o->hasTextureData()) return -1;
        const auto& px = o->getTextureData();
        const uint64_t h = blobHash(px);
        for (size_t i = 0; i < texTable.size(); ++i)
            if (texTable[i].first == h) return static_cast<int>(i);
        json t;
        t["w"] = o->getTextureWidth();
        t["h"] = o->getTextureHeight();
        t["data"] = b64encode(px.data(), px.size());
        texTable.push_back({h, std::move(t)});
        return static_cast<int>(texTable.size()) - 1;
    };

    json objs = json::array();
    for (SceneObject* o : members) {
        json j;
        // Strip a shipN_ spawn prefix so re-imported ships re-prefix cleanly.
        std::string nm = o->getName();
        if (nm.rfind("ship", 0) == 0) {
            size_t i = 4;
            while (i < nm.size() && std::isdigit(static_cast<unsigned char>(nm[i]))) ++i;
            if (i > 4 && i < nm.size() && nm[i] == '_') nm = nm.substr(i + 1);
        }
        j["name"] = nm;
        j["bt"] = o->getBuildingType();
        j["pos"] = vec3j(o->getTransform().getPosition() - origin);
        const glm::quat q = o->getTransform().getRotation();
        j["rot"] = json::array({q.w, q.x, q.y, q.z});
        j["scale"] = vec3j(o->getTransform().getScale());
        const glm::vec4 c = o->getPrimitiveColor();
        j["color"] = json::array({c.r, c.g, c.b, c.a});
        j["model"] = o->getModelPath();
        j["aabb"] = o->hasAABBCollision();
        j["transparent"] = o->isTransparent();
        if (!o->getModelMetadata().empty()) {
            json md;
            for (const auto& [k, v] : o->getModelMetadata()) md[k] = v;
            j["meta"] = md;
        }
        if (o->hasPorts()) {
            json ps = json::array();
            for (const auto& p : o->getPorts())
                ps.push_back({{"n", p.name}, {"p", vec3j(p.position)},
                              {"f", vec3j(p.forward)}, {"u", vec3j(p.up)}});
            j["ports"] = ps;
        }
        const int tref = textureRef(o);
        if (tref >= 0) {
            j["tex"] = tref;
            // Painted primitives carry bespoke vertices (per-face UV tiling
            // baked at apply time) -- store them so the paint survives.
            if (o->getModelPath().empty() && o->hasMeshData()) {
                json vs = json::array();
                for (const auto& v : o->getVertices()) {
                    vs.push_back({v.position.x, v.position.y, v.position.z,
                                  v.normal.x, v.normal.y, v.normal.z,
                                  v.texCoord.x, v.texCoord.y,
                                  v.color.r, v.color.g, v.color.b, v.color.a});
                }
                j["verts"] = std::move(vs);
            }
        }
        objs.push_back(std::move(j));
    }
    doc["objects"] = std::move(objs);

    json texArr = json::array();
    for (auto& [h, t] : texTable) texArr.push_back(std::move(t));
    doc["textures"] = std::move(texArr);

    // Wires among members, by member index -- names change at spawn, indices
    // do not.
    json wiresJ = json::array();
    int wireCount = 0;
    if (host.wiresAmong) {
        auto idxOf = [&](SceneObject* o) -> int {
            for (size_t i = 0; i < members.size(); ++i)
                if (members[i] == o) return static_cast<int>(i);
            return -1;
        };
        for (const auto& w : host.wiresAmong(members)) {
            const int ia = idxOf(w.a), ib = idxOf(w.b);
            if (ia < 0 || ib < 0) continue;
            wiresJ.push_back({{"a", ia}, {"ap", w.ap}, {"b", ib}, {"bp", w.bp}});
            ++wireCount;
        }
    }
    doc["wires"] = std::move(wiresJ);

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return "cannot write " + path;
    f << doc.dump(1);

    char msg[160];
    std::snprintf(msg, sizeof msg, "saved %zu pieces, %d wires, %zu textures -> %s",
                  members.size(), wireCount, texTable.size(),
                  std::filesystem::path(path).filename().string().c_str());
    return msg;
}

std::string spawn(Host& host, const glm::vec3& dropPos, const std::string& path) {
    std::ifstream f(path);
    if (!f) return "cannot read " + path;
    json doc;
    try { f >> doc; } catch (...) { return "not a ship file: " + path; }
    if (doc.value("format", "") != std::string("eden-ship"))
        return "not a ship file: " + path;

    const std::string prefix = host.nextPrefix();
    const auto& texArr = doc["textures"];

    std::vector<SceneObject*> placed;
    for (const auto& j : doc["objects"]) {
        auto obj = std::make_unique<SceneObject>(prefix + j.value("name", "piece"));
        const std::string modelPath = j.value("model", "");
        bool meshed = false;

        std::vector<unsigned char> texPixels;
        int texW = 0, texH = 0;
        if (j.contains("tex")) {
            const auto& t = texArr[j["tex"].get<int>()];
            texPixels = b64decode(t["data"].get<std::string>());
            texW = t["w"].get<int>();
            texH = t["h"].get<int>();
        }

        if (!modelPath.empty()) {
            Host::LoadedModel lm;
            if (host.loadModelFile && host.loadModelFile(modelPath, lm) && !lm.verts.empty()) {
                const uint32_t handle = host.createModel(lm.verts, lm.indices);
                obj->setBufferHandle(handle);
                obj->setIndexCount(static_cast<uint32_t>(lm.indices.size()));
                obj->setVertexCount(static_cast<uint32_t>(lm.verts.size()));
                obj->setMeshData(lm.verts, lm.indices);
                eden::AABB lb;
                lb.min = glm::vec3(1e30f); lb.max = glm::vec3(-1e30f);
                for (const auto& v : lm.verts) {
                    lb.min = glm::min(lb.min, v.position);
                    lb.max = glm::max(lb.max, v.position);
                }
                obj->setLocalBounds(lb);
                if (!lm.texture.empty())
                    host.uploadTexture(handle, lm.texture.data(), lm.texW, lm.texH);
                else if (!texPixels.empty())
                    host.uploadTexture(handle, texPixels.data(), texW, texH);
                if (!lm.ports.empty()) obj->setPorts(lm.ports);
                obj->setModelPath(modelPath);
                meshed = true;
            }
        }
        if (!meshed) {
            // Primitive piece. Painted ones carry bespoke vertices; plain
            // ones regenerate from the unit cube and their colour.
            const auto& cj = j["color"];
            const glm::vec4 col(cj[0].get<float>(), cj[1].get<float>(),
                                cj[2].get<float>(), cj[3].get<float>());
            // Always build the canonical cube first: its INDEX order is the
            // authority, and painted pieces' bespoke vertices (baked UV
            // tiling) overwrite the vertex list only when the layout agrees.
            auto mesh = eden::PrimitiveMeshBuilder::createCube(1.0f, col);
            std::vector<eden::ModelVertex> verts = std::move(mesh.vertices);
            if (j.contains("verts") && j["verts"].size() == verts.size()) {
                size_t vi = 0;
                for (const auto& vj : j["verts"]) {
                    eden::ModelVertex v{};
                    v.position = {vj[0].get<float>(), vj[1].get<float>(), vj[2].get<float>()};
                    v.normal   = {vj[3].get<float>(), vj[4].get<float>(), vj[5].get<float>()};
                    v.texCoord = {vj[6].get<float>(), vj[7].get<float>()};
                    v.color    = {vj[8].get<float>(), vj[9].get<float>(),
                                  vj[10].get<float>(), vj[11].get<float>()};
                    verts[vi++] = v;
                }
            }
            const auto& indices = mesh.indices;
            const uint32_t handle = host.createModel(verts, indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(verts.size()));
            obj->setMeshData(verts, indices);
            eden::AABB lb;
            lb.min = glm::vec3(1e30f); lb.max = glm::vec3(-1e30f);
            for (const auto& v : verts) {
                lb.min = glm::min(lb.min, v.position);
                lb.max = glm::max(lb.max, v.position);
            }
            obj->setLocalBounds(lb);
            obj->setPrimitiveType(eden::PrimitiveType::Cube);
            obj->setPrimitiveSize(1.0f);
            obj->setPrimitiveColor(col);
            if (!texPixels.empty()) {
                obj->setTextureData(texPixels, texW, texH);
                host.uploadTexture(handle, texPixels.data(), texW, texH);
            }
        }

        obj->setBuildingType(j.value("bt", ""));
        obj->setAABBCollision(j.value("aabb", true));
        if (j.value("transparent", false)) obj->setTransparent(true);
        if (j.contains("meta")) {
            std::unordered_map<std::string, std::string> md;
            for (auto& [k, v] : j["meta"].items()) md[k] = v.get<std::string>();
            obj->setModelMetadata(md);
        }
        if (j.contains("ports") && !obj->hasPorts()) {
            std::vector<SceneObject::StoredPort> ps;
            for (const auto& pj : j["ports"])
                ps.push_back({pj["n"].get<std::string>(), jvec3(pj["p"]),
                              jvec3(pj["f"]), jvec3(pj["u"])});
            obj->setPorts(ps);
        }

        obj->getTransform().setPosition(jvec3(j["pos"]) + dropPos);
        const auto& rj = j["rot"];
        const glm::quat q(rj[0].get<float>(), rj[1].get<float>(),
                          rj[2].get<float>(), rj[3].get<float>());
        obj->getTransform().setRotation(q);
        obj->setEulerRotation(glm::degrees(glm::eulerAngles(q)));
        obj->getTransform().setScale(jvec3(j["scale"]));

        placed.push_back(obj.get());
        host.sceneObjects->push_back(std::move(obj));
    }

    int wires = 0;
    if (host.addWire && doc.contains("wires")) {
        for (const auto& wj : doc["wires"]) {
            const int ia = wj["a"].get<int>(), ib = wj["b"].get<int>();
            if (ia < 0 || ib < 0 || ia >= (int)placed.size() || ib >= (int)placed.size()) continue;
            host.addWire(placed[ia], wj["ap"].get<std::string>(),
                         placed[ib], wj["bp"].get<std::string>());
            ++wires;
        }
    }

    if (host.objectsChanged) host.objectsChanged();
    char msg[160];
    std::snprintf(msg, sizeof msg,
                  "%s launched: %zu pieces, %d wires -- F5 out and in gives her collision",
                  doc.value("name", "ship").c_str(), placed.size(), wires);
    return msg;
}

std::vector<std::string> list(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() == ".ship") out.push_back(e.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace shipfile
