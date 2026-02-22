#include "PrimitiveMeshBuilder.hpp"
#include <cmath>

namespace eden {

PrimitiveMeshBuilder::MeshData PrimitiveMeshBuilder::createCylinder(
    float radius, float height, int segments, const glm::vec4& color) {

    MeshData result;
    auto& vertices = result.vertices;
    auto& indices = result.indices;

    // Generate side vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);

        ModelVertex top, bottom;

        top.position = glm::vec3(x, height, z);
        top.normal = glm::normalize(glm::vec3(x, 0, z));
        top.texCoord = glm::vec2((float)i / segments, 1.0f);
        top.color = color;

        bottom.position = glm::vec3(x, 0, z);
        bottom.normal = glm::normalize(glm::vec3(x, 0, z));
        bottom.texCoord = glm::vec2((float)i / segments, 0.0f);
        bottom.color = color;

        vertices.push_back(top);
        vertices.push_back(bottom);
    }

    // Generate side faces
    for (int i = 0; i < segments; i++) {
        uint32_t topLeft = i * 2;
        uint32_t bottomLeft = i * 2 + 1;
        uint32_t topRight = (i + 1) * 2;
        uint32_t bottomRight = (i + 1) * 2 + 1;

        indices.push_back(topLeft);
        indices.push_back(bottomLeft);
        indices.push_back(bottomRight);

        indices.push_back(topLeft);
        indices.push_back(bottomRight);
        indices.push_back(topRight);
    }

    // Top cap center vertex
    uint32_t topCenter = static_cast<uint32_t>(vertices.size());
    ModelVertex tc;
    tc.position = glm::vec3(0, height, 0);
    tc.normal = glm::vec3(0, 1, 0);
    tc.texCoord = glm::vec2(0.5f, 0.5f);
    tc.color = glm::vec4(color.r * 1.1f, color.g * 1.1f, color.b * 1.1f, color.a);
    vertices.push_back(tc);

    // Bottom cap center vertex
    uint32_t bottomCenter = static_cast<uint32_t>(vertices.size());
    ModelVertex bc;
    bc.position = glm::vec3(0, 0, 0);
    bc.normal = glm::vec3(0, -1, 0);
    bc.texCoord = glm::vec2(0.5f, 0.5f);
    bc.color = glm::vec4(color.r * 0.85f, color.g * 0.85f, color.b * 0.85f, color.a);
    vertices.push_back(bc);

    // Top cap faces
    for (int i = 0; i < segments; i++) {
        uint32_t topLeft = i * 2;
        uint32_t topRight = (i + 1) * 2;
        indices.push_back(topCenter);
        indices.push_back(topRight);
        indices.push_back(topLeft);
    }

    // Bottom cap faces
    for (int i = 0; i < segments; i++) {
        uint32_t bottomLeft = i * 2 + 1;
        uint32_t bottomRight = (i + 1) * 2 + 1;
        indices.push_back(bottomCenter);
        indices.push_back(bottomLeft);
        indices.push_back(bottomRight);
    }

    // Set bounds
    result.bounds.min = glm::vec3(-radius, 0, -radius);
    result.bounds.max = glm::vec3(radius, height, radius);

    return result;
}

PrimitiveMeshBuilder::MeshData PrimitiveMeshBuilder::createCube(float size, const glm::vec4& color, bool interior) {
    MeshData result;
    auto& vertices = result.vertices;
    auto& indices = result.indices;

    float h = size / 2.0f;

    glm::vec3 corners[8] = {
        {-h, 0, -h}, { h, 0, -h}, { h, size, -h}, {-h, size, -h},
        {-h, 0,  h}, { h, 0,  h}, { h, size,  h}, {-h, size,  h}
    };

    auto addQuad = [&](int c0, int c1, int c2, int c3, glm::vec3 normal) {
        uint32_t base = static_cast<uint32_t>(vertices.size());

        // For interior mode, flip normals to point inward AND reorder vertices
        // so they're CCW when viewed from inside
        glm::vec3 n = interior ? -normal : normal;

        // For interior: swap vertex order so CCW from inside
        // Original order c0,c1,c2,c3 is CCW from outside
        // Reversed order c1,c0,c3,c2 is CCW from inside
        int i0 = interior ? c1 : c0;
        int i1 = interior ? c0 : c1;
        int i2 = interior ? c3 : c2;
        int i3 = interior ? c2 : c3;

        ModelVertex v0, v1, v2, v3;
        v0.position = corners[i0]; v0.normal = n; v0.color = color; v0.texCoord = {0, 0};
        v1.position = corners[i1]; v1.normal = n; v1.color = color; v1.texCoord = {1, 0};
        v2.position = corners[i2]; v2.normal = n; v2.color = color; v2.texCoord = {1, 1};
        v3.position = corners[i3]; v3.normal = n; v3.color = color; v3.texCoord = {0, 1};

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        // Always use standard winding - vertex positions handle the flip
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    // Front face (+Z)
    addQuad(4, 5, 6, 7, {0, 0, 1});
    // Back face (-Z)
    addQuad(1, 0, 3, 2, {0, 0, -1});
    // Left face (-X)
    addQuad(0, 4, 7, 3, {-1, 0, 0});
    // Right face (+X)
    addQuad(5, 1, 2, 6, {1, 0, 0});
    // Top face (+Y)
    addQuad(7, 6, 2, 3, {0, 1, 0});
    // Bottom face (-Y)
    addQuad(0, 1, 5, 4, {0, -1, 0});

    // Set bounds
    result.bounds.min = glm::vec3(-h, 0, -h);
    result.bounds.max = glm::vec3(h, size, h);

    return result;
}

PrimitiveMeshBuilder::MeshData PrimitiveMeshBuilder::createSpawnMarker(float size) {
    MeshData result;
    auto& vertices = result.vertices;
    auto& indices = result.indices;

    float h = size;  // Height equals size

    auto addVertex = [&](float x, float y, float z, float nx, float ny, float nz,
                         float u, float v, float r, float g, float b) {
        ModelVertex vert;
        vert.position = glm::vec3(x, y, z);
        vert.normal = glm::vec3(nx, ny, nz);
        vert.texCoord = glm::vec2(u, v);
        vert.color = glm::vec4(r, g, b, 1.0f);
        vertices.push_back(vert);
    };

    auto addFace = [&](uint32_t a, uint32_t b, uint32_t c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    };

    // Front face (-Z)
    addVertex(-size, 0, -size, 0, 0, -1, 0, 0, 0.2f, 0.8f, 0.2f);
    addVertex( size, 0, -size, 0, 0, -1, 1, 0, 0.2f, 0.8f, 0.2f);
    addVertex( size, h, -size, 0, 0, -1, 1, 1, 0.2f, 0.8f, 0.2f);
    addVertex(-size, h, -size, 0, 0, -1, 0, 1, 0.2f, 0.8f, 0.2f);
    addFace(0, 1, 2); addFace(0, 2, 3);

    // Back face (+Z)
    addVertex( size, 0,  size, 0, 0, 1, 0, 0, 0.2f, 0.8f, 0.2f);
    addVertex(-size, 0,  size, 0, 0, 1, 1, 0, 0.2f, 0.8f, 0.2f);
    addVertex(-size, h,  size, 0, 0, 1, 1, 1, 0.2f, 0.8f, 0.2f);
    addVertex( size, h,  size, 0, 0, 1, 0, 1, 0.2f, 0.8f, 0.2f);
    addFace(4, 5, 6); addFace(4, 6, 7);

    // Left face (-X)
    addVertex(-size, 0,  size, -1, 0, 0, 0, 0, 0.2f, 0.8f, 0.2f);
    addVertex(-size, 0, -size, -1, 0, 0, 1, 0, 0.2f, 0.8f, 0.2f);
    addVertex(-size, h, -size, -1, 0, 0, 1, 1, 0.2f, 0.8f, 0.2f);
    addVertex(-size, h,  size, -1, 0, 0, 0, 1, 0.2f, 0.8f, 0.2f);
    addFace(8, 9, 10); addFace(8, 10, 11);

    // Right face (+X)
    addVertex( size, 0, -size, 1, 0, 0, 0, 0, 0.2f, 0.8f, 0.2f);
    addVertex( size, 0,  size, 1, 0, 0, 1, 0, 0.2f, 0.8f, 0.2f);
    addVertex( size, h,  size, 1, 0, 0, 1, 1, 0.2f, 0.8f, 0.2f);
    addVertex( size, h, -size, 1, 0, 0, 0, 1, 0.2f, 0.8f, 0.2f);
    addFace(12, 13, 14); addFace(12, 14, 15);

    // Top face (+Y) - brighter green
    addVertex(-size, h, -size, 0, 1, 0, 0, 0, 0.3f, 1.0f, 0.3f);
    addVertex( size, h, -size, 0, 1, 0, 1, 0, 0.3f, 1.0f, 0.3f);
    addVertex( size, h,  size, 0, 1, 0, 1, 1, 0.3f, 1.0f, 0.3f);
    addVertex(-size, h,  size, 0, 1, 0, 0, 1, 0.3f, 1.0f, 0.3f);
    addFace(16, 17, 18); addFace(16, 18, 19);

    // Bottom face (-Y) - darker green
    addVertex(-size, 0,  size, 0, -1, 0, 0, 0, 0.15f, 0.6f, 0.15f);
    addVertex( size, 0,  size, 0, -1, 0, 1, 0, 0.15f, 0.6f, 0.15f);
    addVertex( size, 0, -size, 0, -1, 0, 1, 1, 0.15f, 0.6f, 0.15f);
    addVertex(-size, 0, -size, 0, -1, 0, 0, 1, 0.15f, 0.6f, 0.15f);
    addFace(20, 21, 22); addFace(20, 22, 23);

    // Set bounds
    result.bounds.min = glm::vec3(-size, 0, -size);
    result.bounds.max = glm::vec3(size, h, size);

    return result;
}

PrimitiveMeshBuilder::MeshData PrimitiveMeshBuilder::createFoundation(
    glm::vec2 corner1, glm::vec2 corner2, float floorY, float height, const glm::vec4& color) {

    MeshData result;
    auto& vertices = result.vertices;
    auto& indices = result.indices;

    float x1 = std::min(corner1.x, corner2.x);
    float x2 = std::max(corner1.x, corner2.x);
    float z1 = std::min(corner1.y, corner2.y);
    float z2 = std::max(corner1.y, corner2.y);
    float yBot = floorY;
    float yTop = floorY + height;

    float widthX = x2 - x1;
    float widthZ = z2 - z1;

    auto addQuad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                       glm::vec3 normal, float uScale, float vScale) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        ModelVertex v0, v1, v2, v3;
        v0.position = p0; v0.normal = normal; v0.color = color; v0.texCoord = {0, 0};
        v1.position = p1; v1.normal = normal; v1.color = color; v1.texCoord = {uScale, 0};
        v2.position = p2; v2.normal = normal; v2.color = color; v2.texCoord = {uScale, vScale};
        v3.position = p3; v3.normal = normal; v3.color = color; v3.texCoord = {0, vScale};
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    // Top (+Y)
    addQuad({x1, yTop, z2}, {x2, yTop, z2}, {x2, yTop, z1}, {x1, yTop, z1},
            {0, 1, 0}, widthX, widthZ);
    // Bottom (-Y)
    addQuad({x1, yBot, z1}, {x2, yBot, z1}, {x2, yBot, z2}, {x1, yBot, z2},
            {0, -1, 0}, widthX, widthZ);
    // North (-Z)
    addQuad({x2, yBot, z1}, {x1, yBot, z1}, {x1, yTop, z1}, {x2, yTop, z1},
            {0, 0, -1}, widthX, height);
    // South (+Z)
    addQuad({x1, yBot, z2}, {x2, yBot, z2}, {x2, yTop, z2}, {x1, yTop, z2},
            {0, 0, 1}, widthX, height);
    // West (-X)
    addQuad({x1, yBot, z1}, {x1, yBot, z2}, {x1, yTop, z2}, {x1, yTop, z1},
            {-1, 0, 0}, widthZ, height);
    // East (+X)
    addQuad({x2, yBot, z2}, {x2, yBot, z1}, {x2, yTop, z1}, {x2, yTop, z2},
            {1, 0, 0}, widthZ, height);

    result.bounds.min = glm::vec3(x1, yBot, z1);
    result.bounds.max = glm::vec3(x2, yTop, z2);

    return result;
}

} // namespace eden
