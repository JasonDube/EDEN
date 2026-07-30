#pragma once

// A developer-authored .lime, in the world.
//
// The smallest possible test of the whole prefab idea: a file the game has never
// been compiled against decides what a thing IS, where it mounts, and where a
// body stands to use it. If this works, "buy a helm from a catalogue" is
// bookkeeping on top of it. If it does not, nothing else in that plan matters.
//
// Deliberately small and deliberately loose. It holds a mesh, the ports by name
// and the metadata by key, and it knows how to put itself somewhere. It does not
// know about vessels, credits, catalogues, or flying, and it should not until
// there is a reason.

#include "SceneVertex.hpp"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace tessara {

class Prefab {
public:
    // Read a .lime. Returns false and says why on the console -- a prefab that
    // silently fails to appear is the failure mode this whole idea is most
    // exposed to, so it is never silent.
    bool load(const std::string& path);

    bool loaded() const { return m_loaded; }
    const std::string& path() const { return m_path; }

    // What the FILE says it is. No default: asking for a key that is not there
    // is a question about the file, and answering it with a guess would hide
    // exactly the authoring mistake we want to catch.
    std::string meta(const std::string& key, const std::string& fallback = {}) const {
        auto it = m_metadata.find(key);
        return it == m_metadata.end() ? fallback : it->second;
    }
    bool hasMeta(const std::string& key) const { return m_metadata.count(key) > 0; }
    const std::unordered_map<std::string, std::string>& metadata() const { return m_metadata; }

    struct Port {
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
    };
    bool hasPort(const std::string& name) const { return m_ports.count(name) > 0; }

    // A port in WORLD space, given where the prefab is standing. Ports are
    // authored in the model's own frame, which is the point of them -- move the
    // console in LIME and the pilot's spot moves with it, with no code involved.
    bool worldPort(const std::string& name, glm::vec3& outPosition,
                   glm::vec3& outForward) const;

    // Stand it somewhere, facing a yaw in degrees.
    void placeAt(const glm::vec3& position, float yawDegrees);
    bool placed() const { return m_placed; }
    glm::vec3 position() const { return m_position; }

    // Its triangles, transformed, appended to whatever is being built this frame.
    void appendMesh(std::vector<SceneVertex>& verts,
                    std::vector<uint32_t>& indices) const;

private:
    std::string m_path;
    bool m_loaded = false;
    bool m_placed = false;

    std::vector<SceneVertex> m_verts;      // in the model's own frame
    std::vector<uint32_t>    m_indices;
    std::unordered_map<std::string, Port>        m_ports;
    std::unordered_map<std::string, std::string> m_metadata;

    glm::vec3 m_position{0.0f};
    float     m_yaw = 0.0f;
};

} // namespace tessara
