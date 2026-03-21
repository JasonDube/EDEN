#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in float inAlpha;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragAlpha;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    fragUV = inUV;
    fragAlpha = inAlpha;
}
