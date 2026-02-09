#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec4 inTexWeights;
layout(location = 5) in uvec4 inTexIndices;
layout(location = 6) in float inSelection;
layout(location = 7) in float inPaintAlpha;
layout(location = 8) in vec3 inTexHSB;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 fogColor;
    float fogStart;
    float fogEnd;
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragTexWeights;
layout(location = 4) out flat uvec4 fragTexIndices;
layout(location = 5) out float fragDistance;
layout(location = 6) out float fragSelection;
layout(location = 7) out float fragPaintAlpha;
layout(location = 8) out vec3 fragTexHSB;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragNormal = inNormal;
    fragUV = inUV;
    fragTexWeights = inTexWeights;
    fragTexIndices = inTexIndices;
    fragDistance = gl_Position.w;  // Distance from camera
    fragSelection = inSelection;
    fragPaintAlpha = inPaintAlpha;
    fragTexHSB = inTexHSB;
}
