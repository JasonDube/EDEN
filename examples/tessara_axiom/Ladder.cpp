#include "Ladder.hpp"
#include "MeshBuild.hpp"

namespace tessara {

void Ladder::buildMesh(std::vector<SceneVertex>& verts,
                         std::vector<uint32_t>& indices) const {
    if (!m_placed) return;

    const glm::vec3 r(1.0f, 0.0f, 0.0f);
    const glm::vec3 u(0.0f, 1.0f, 0.0f);
    const glm::vec3 f(0.0f, 0.0f, 1.0f);

    const glm::vec3 kPole(0.42f, 0.42f, 0.46f);
    const glm::vec3 kRung(0.86f, 0.72f, 0.24f);   // yellow, so it reads as a thing to use

    // The pole itself, standing a little into the ground so it does not float.
    appendBox(verts, indices, m_base + u * (m_height * 0.5f - 0.15f), r, u, f,
              glm::vec3(0.16f, m_height * 0.5f + 0.15f, 0.16f), kPole);

    // Two rails and the rungs between them, on the -Z face.
    const float railGap = 0.34f;
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;
        appendBox(verts, indices,
                  m_base + r * (sx * railGap) - f * 0.22f + u * (m_height * 0.5f),
                  r, u, f, glm::vec3(0.05f, m_height * 0.5f, 0.05f), kRung);
    }
    const int rungs = static_cast<int>(m_height / 0.35f);
    for (int i = 1; i <= rungs; ++i) {
        const float y = m_height * static_cast<float>(i) / (rungs + 1);
        appendBox(verts, indices, m_base - f * 0.22f + u * y, r, u, f,
                  glm::vec3(railGap, 0.035f, 0.045f), kRung);
    }

    // A marker on the ground so it is obvious where to stand.
    appendBox(verts, indices, m_base + u * 0.02f, r, u, f,
              glm::vec3(kReach * 0.7f, 0.02f, kReach * 0.7f),
              glm::vec3(0.20f, 0.36f, 0.22f));
}

} // namespace tessara
