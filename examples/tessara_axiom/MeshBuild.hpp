#pragma once

// Shared geometry helpers. Both creatures are built out of boxes and struts, so
// these live here rather than in either one of them.

#include "SceneVertex.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tessara {

// A box given its own frame. Each face gets its own normal, so the form reads
// under the three-point shading instead of smearing.
inline void appendBox(std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices,
                      const glm::vec3& centre,
                      const glm::vec3& right, const glm::vec3& up, const glm::vec3& fwd,
                      const glm::vec3& half, const glm::vec3& color)
{
    const glm::vec3 r = right * half.x;
    const glm::vec3 u = up    * half.y;
    const glm::vec3 f = fwd   * half.z;

    const glm::vec3 c[8] = {
        centre - r - u - f, centre + r - u - f, centre + r + u - f, centre - r + u - f,
        centre - r - u + f, centre + r - u + f, centre + r + u + f, centre - r + u + f,
    };

    struct Face { int a, b, c, d; glm::vec3 n; };
    const Face faces[6] = {
        {4, 5, 6, 7,  fwd},   {1, 0, 3, 2, -fwd},
        {5, 1, 2, 6,  right}, {0, 4, 7, 3, -right},
        {3, 2, 6, 7,  up},    {0, 1, 5, 4, -up},
    };

    for (const Face& face : faces) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({c[face.a], face.n, color});
        verts.push_back({c[face.b], face.n, color});
        verts.push_back({c[face.c], face.n, color});
        verts.push_back({c[face.d], face.n, color});
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    }
}

// A strut between two points, with an arbitrary but stable cross-section frame.
inline void appendStrut(std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices,
                        const glm::vec3& from, const glm::vec3& to,
                        float thickness, const glm::vec3& color)
{
    glm::vec3 axis = to - from;
    float length = glm::length(axis);
    if (length < 1e-5f) return;
    axis /= length;

    glm::vec3 hint = std::abs(axis.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 right = glm::normalize(glm::cross(hint, axis));
    glm::vec3 up    = glm::cross(axis, right);

    appendBox(verts, indices, (from + to) * 0.5f, right, up, axis,
              glm::vec3(thickness, thickness, length * 0.5f), color);
}

// A tapered strut: thick at `from`, thin at `to`. Limbs read much better when
// they narrow toward the joint.
inline void appendTaperedStrut(std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices,
                               const glm::vec3& from, const glm::vec3& to,
                               float thickFrom, float thickTo, const glm::vec3& color)
{
    glm::vec3 axis = to - from;
    float length = glm::length(axis);
    if (length < 1e-5f) return;
    axis /= length;

    glm::vec3 hint = std::abs(axis.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 r = glm::normalize(glm::cross(hint, axis));
    glm::vec3 u = glm::cross(axis, r);

    const glm::vec3 a[4] = {
        from - r * thickFrom - u * thickFrom, from + r * thickFrom - u * thickFrom,
        from + r * thickFrom + u * thickFrom, from - r * thickFrom + u * thickFrom,
    };
    const glm::vec3 b[4] = {
        to - r * thickTo - u * thickTo, to + r * thickTo - u * thickTo,
        to + r * thickTo + u * thickTo, to - r * thickTo + u * thickTo,
    };

    auto quad = [&](const glm::vec3& p0, const glm::vec3& p1,
                    const glm::vec3& p2, const glm::vec3& p3) {
        glm::vec3 n = glm::cross(p1 - p0, p3 - p0);
        if (glm::dot(n, n) < 1e-10f) return;
        n = glm::normalize(n);
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({p0, n, color});
        verts.push_back({p1, n, color});
        verts.push_back({p2, n, color});
        verts.push_back({p3, n, color});
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    };

    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        quad(a[i], a[j], b[j], b[i]);
    }
    quad(a[3], a[2], a[1], a[0]);
    quad(b[0], b[1], b[2], b[3]);
}

} // namespace tessara
