#include "ShipInterior.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

using namespace eden;

namespace eve {

void ShipInterior::build(ModelRenderer& renderer) {
    if (m_built) return;
    
    m_cubes.clear();
    
    float cubeSize = 2.0f;
    
    // Ship dimensions in cubes
    int width = static_cast<int>(m_dimensions.x / cubeSize);   // 10 cubes wide
    int height = static_cast<int>(m_dimensions.y / cubeSize);  // 4 cubes tall  
    int depth = static_cast<int>(m_dimensions.z / cubeSize);   // 15 cubes deep
    
    // Colors for each wall (RGBA)
    glm::vec4 floorColor = {0.2f, 0.2f, 0.3f, 1.0f};      // Dark blue-gray floor
    glm::vec4 ceilingColor = {0.15f, 0.15f, 0.2f, 1.0f};  // Darker ceiling
    glm::vec4 leftColor = {0.4f, 0.15f, 0.15f, 1.0f};     // Dark red left wall
    glm::vec4 rightColor = {0.15f, 0.4f, 0.15f, 1.0f};    // Dark green right wall
    glm::vec4 backColor = {0.25f, 0.25f, 0.3f, 1.0f};     // Gray back wall
    glm::vec4 frontColor = {0.15f, 0.25f, 0.4f, 1.0f};    // Blue-ish front wall
    
    // Window position (center of front wall)
    int windowX = width / 2;
    int windowY = height / 2;
    
    auto addCube = [&](const glm::vec3& pos, const glm::vec4& color) {
        // interior=true: normals point inward, winding reversed for viewing from inside ship
        PrimitiveMeshBuilder::MeshData mesh = PrimitiveMeshBuilder::createCube(cubeSize, color, true);
        
        uint32_t handle = renderer.createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
        
        CubeInstance cube;
        cube.bufferHandle = handle;
        cube.indexCount = static_cast<uint32_t>(mesh.indices.size());
        cube.transform = glm::translate(glm::mat4(1.0f), pos);
        
        m_cubes.push_back(cube);
    };
    
    std::cout << "Building ship interior: " << width << "x" << height << "x" << depth << " cubes..." << std::endl;
    
    // Build floor (y = 0)
    for (int x = 0; x < width; x++) {
        for (int z = 0; z < depth; z++) {
            glm::vec3 pos = {
                (x - width/2.0f + 0.5f) * cubeSize,
                0.0f,
                (z - depth/2.0f + 0.5f) * cubeSize
            };
            // Checkerboard pattern
            glm::vec4 color = ((x + z) % 2 == 0) ? floorColor : floorColor * 0.7f;
            color.a = 1.0f;
            addCube(pos, color);
        }
    }
    
    // Build ceiling (y = height)
    for (int x = 0; x < width; x++) {
        for (int z = 0; z < depth; z++) {
            glm::vec3 pos = {
                (x - width/2.0f + 0.5f) * cubeSize,
                height * cubeSize,
                (z - depth/2.0f + 0.5f) * cubeSize
            };
            addCube(pos, ceilingColor);
        }
    }
    
    // Build left wall (x = -width/2)
    for (int y = 1; y < height; y++) {
        for (int z = 0; z < depth; z++) {
            glm::vec3 pos = {
                (-width/2.0f + 0.5f) * cubeSize,
                y * cubeSize,
                (z - depth/2.0f + 0.5f) * cubeSize
            };
            float gradient = 0.7f + 0.3f * (float(y) / height);
            glm::vec4 color = leftColor * gradient;
            color.a = 1.0f;
            addCube(pos, color);
        }
    }
    
    // Build right wall (x = width/2)
    for (int y = 1; y < height; y++) {
        for (int z = 0; z < depth; z++) {
            glm::vec3 pos = {
                (width/2.0f - 0.5f) * cubeSize,
                y * cubeSize,
                (z - depth/2.0f + 0.5f) * cubeSize
            };
            float gradient = 0.7f + 0.3f * (float(y) / height);
            glm::vec4 color = rightColor * gradient;
            color.a = 1.0f;
            addCube(pos, color);
        }
    }
    
    // Build back wall (z = -depth/2)
    for (int x = 1; x < width - 1; x++) {
        for (int y = 1; y < height; y++) {
            glm::vec3 pos = {
                (x - width/2.0f + 0.5f) * cubeSize,
                y * cubeSize,
                (-depth/2.0f + 0.5f) * cubeSize
            };
            addCube(pos, backColor);
        }
    }
    
    // Build front wall (z = depth/2) with window hole
    for (int x = 1; x < width - 1; x++) {
        for (int y = 1; y < height; y++) {
            // Skip cubes around center to create window (3x2 cube hole)
            bool isWindow = (x >= windowX - 1 && x <= windowX + 1) && 
                           (y >= windowY - 1 && y <= windowY);
            
            if (isWindow) continue; // Leave hole for window/viewport
            
            glm::vec3 pos = {
                (x - width/2.0f + 0.5f) * cubeSize,
                y * cubeSize,
                (depth/2.0f - 0.5f) * cubeSize
            };
            addCube(pos, frontColor);
        }
    }
    
    std::cout << "Ship interior built: " << m_cubes.size() << " cubes" << std::endl;
    m_built = true;
}

void ShipInterior::render(ModelRenderer& renderer, VkCommandBuffer cmd, 
                          const glm::mat4& view, const glm::mat4& proj) {
    if (!m_built) return;
    
    glm::mat4 viewProj = proj * view;
    
    for (const auto& cube : m_cubes) {
        // twoSided = true since we're inside the ship looking at interior walls
        renderer.render(cmd, viewProj, cube.bufferHandle, cube.transform, 0.0f, 1.0f, 1.0f, true);
    }
}

} // namespace eve
