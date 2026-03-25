#version 450

// Per-vertex data (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

// Per-instance data (binding 1)
layout(location = 4) in mat4 instanceModel;    // locations 4,5,6,7
layout(location = 8) in vec4 instanceColorAdj;  // location 8

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) flat out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec4 fragColorAdjust;

void main() {
    gl_Position = pc.viewProj * instanceModel * vec4(inPosition, 1.0);

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(instanceModel)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragWorldPos = vec3(instanceModel * vec4(inPosition, 1.0));
    fragColorAdjust = instanceColorAdj;
}
