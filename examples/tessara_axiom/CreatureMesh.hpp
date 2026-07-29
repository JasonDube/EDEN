#pragma once

#include "Ground.hpp"
#include "SceneVertex.hpp"
#include "Walker.hpp"

#include <cstdint>
#include <vector>

namespace tessara {

// Builds the walker's geometry fresh each frame from its animated state. It is
// a couple of hundred vertices, so there is nothing to gain from skinning it and
// a lot to lose in flexibility while the shape is still being decided.
void buildCreatureMesh(const Ground& hf,
                       const Walker& walker,
                       std::vector<SceneVertex>& outVertices,
                       std::vector<uint32_t>& outIndices);

} // namespace tessara
