#pragma once

#include "Renderer/ModelRenderer.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace eve {

/**
 * @brief Simple ship interior made of colored cubes
 */
class ShipInterior {
public:
    ShipInterior() = default;
    ~ShipInterior() = default;
    
    /**
     * @brief Build the ship interior geometry
     * @param renderer ModelRenderer to create meshes
     */
    void build(eden::ModelRenderer& renderer);
    
    /**
     * @brief Render the ship interior
     * @param renderer ModelRenderer
     * @param cmd Command buffer
     * @param view View matrix
     * @param proj Projection matrix
     */
    void render(eden::ModelRenderer& renderer, VkCommandBuffer cmd, 
                const glm::mat4& view, const glm::mat4& proj);
    
    /**
     * @brief Get ship dimensions
     */
    glm::vec3 getDimensions() const { return m_dimensions; }
    
    /**
     * @brief Check if built
     */
    bool isBuilt() const { return m_built; }
    
private:
    struct CubeInstance {
        uint32_t bufferHandle;
        uint32_t indexCount;
        glm::mat4 transform;
    };
    
    std::vector<CubeInstance> m_cubes;
    glm::vec3 m_dimensions = {20.0f, 8.0f, 30.0f}; // Width, Height, Depth
    bool m_built = false;
};

} // namespace eve
