#include "Prefab.hpp"

#include "Editor/LimeLoader.hpp"

#include <cmath>
#include <cstdio>

namespace tessara {
namespace {
// The prefab's own frame -> the world. Yaw only: a console sits on a floor.
glm::vec3 turn(const glm::vec3& v, float sinYaw, float cosYaw) {
    return glm::vec3(v.x * cosYaw + v.z * sinYaw, v.y, -v.x * sinYaw + v.z * cosYaw);
}
} // namespace

bool Prefab::load(const std::string& path) {
    m_path = path;
    auto result = eden::LimeLoader::load(path);
    if (!result.success) {
        std::printf("[prefab] REFUSED %s: %s\n", path.c_str(), result.error.c_str());
        std::fflush(stdout);
        return false;
    }

    const auto& mesh = result.mesh;
    m_verts.clear();
    m_verts.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        SceneVertex sv;
        sv.pos = v.position;
        sv.normal = v.normal;
        sv.color = glm::vec3(v.color);
        m_verts.push_back(sv);
    }
    m_indices = mesh.indices;

    for (const auto& p : mesh.ports) m_ports[p.name] = {p.position, p.forward, p.up};
    m_metadata = mesh.metadata;

    m_loaded = !m_verts.empty() && !m_indices.empty();

    // Said out loud, every time. A catalogue is a folder of files, and the way a
    // folder of files fails is quietly.
    std::printf("[prefab] %s: role '%s', %zu triangles, %zu ports, %zu meta keys\n",
                path.c_str(), meta("role", "(none)").c_str(),
                m_indices.size() / 3, m_ports.size(), m_metadata.size());
    for (const auto& kv : m_ports)
        std::printf("[prefab]   port %s at (%.2f %.2f %.2f)\n", kv.first.c_str(),
                    kv.second.position.x, kv.second.position.y, kv.second.position.z);
    std::fflush(stdout);
    return m_loaded;
}

void Prefab::placeAt(const glm::vec3& position, float yawDegrees) {
    m_position = position;
    m_yaw = yawDegrees;
    m_placed = true;
}

bool Prefab::worldPort(const std::string& name, glm::vec3& outPosition,
                       glm::vec3& outForward) const {
    auto it = m_ports.find(name);
    if (it == m_ports.end() || !m_placed) return false;
    const float r = m_yaw * 0.01745329f;
    const float s = std::sin(r), c = std::cos(r);
    outPosition = m_position + turn(it->second.position, s, c);
    outForward = turn(it->second.forward, s, c);
    return true;
}

void Prefab::appendMesh(std::vector<SceneVertex>& verts,
                        std::vector<uint32_t>& indices) const {
    if (!m_loaded || !m_placed) return;
    const uint32_t base = static_cast<uint32_t>(verts.size());
    const float r = m_yaw * 0.01745329f;
    const float s = std::sin(r), c = std::cos(r);
    for (const SceneVertex& v : m_verts) {
        SceneVertex out = v;
        out.pos = m_position + turn(v.pos, s, c);
        out.normal = turn(v.normal, s, c);
        verts.push_back(out);
    }
    for (uint32_t i : m_indices) indices.push_back(base + i);
}

} // namespace tessara
