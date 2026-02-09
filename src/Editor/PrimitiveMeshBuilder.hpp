#pragma once

#include "Renderer/ModelRenderer.hpp"
#include "Editor/SceneObject.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace eden {

/**
 * Utility class for generating primitive mesh geometry.
 * All methods return vertex/index data that can be used with ModelRenderer.
 */
class PrimitiveMeshBuilder {
public:
    struct MeshData {
        std::vector<ModelVertex> vertices;
        std::vector<uint32_t> indices;
        AABB bounds;
    };

    /**
     * Generate a cylinder mesh.
     * @param radius Cylinder radius
     * @param height Cylinder height
     * @param segments Number of segments around the circumference
     * @param color Base color for the mesh
     * @return MeshData with vertices, indices, and bounds
     */
    static MeshData createCylinder(float radius, float height, int segments = 32,
                                   const glm::vec4& color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));

    /**
     * Generate a cube mesh.
     * @param size Side length of the cube
     * @param color Base color for the mesh
     * @param interior If true, normals point inward (for viewing from inside)
     * @return MeshData with vertices, indices, and bounds
     */
    static MeshData createCube(float size,
                               const glm::vec4& color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f),
                               bool interior = false);

    /**
     * Generate a spawn point marker mesh (a colored cube-like shape).
     * @param size Size of the marker
     * @return MeshData with vertices, indices, and bounds
     */
    static MeshData createSpawnMarker(float size = 2.0f);
};

} // namespace eden
