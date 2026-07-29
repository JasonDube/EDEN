#pragma once

#include <glm/glm.hpp>

namespace tessara {

// Everything in the scene -- terrain and walker alike -- is this. Deliberately
// nothing like eden::Vertex3D, which carries 32 splatmap weights this example
// has no use for.
struct SceneVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
};

// Matches the push_constant block in shaders/tessara.{vert,frag}.
struct ScenePush {
    glm::mat4 mvp;
    glm::vec4 tint;   // rgb multiplies vertex colour, a is alpha
    glm::vec4 eye;    // xyz camera world position, w = 1 to switch the ghost on
};

} // namespace tessara
