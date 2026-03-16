#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec4 inTexSplat0;   // Splatmap weights for textures 0-3
layout(location = 5) in vec4 inTexSplat1;   // Splatmap weights for textures 4-7
layout(location = 6) in float inSelection;
layout(location = 7) in float inPaintAlpha;
layout(location = 8) in vec3 inTexHSB;
layout(location = 9) in float inHoleMask;
layout(location = 10) in vec4 inTexSplat2;  // Splatmap weights for textures 8-11
layout(location = 11) in vec4 inTexSplat3;  // Splatmap weights for textures 12-15

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 fogColor;
    float fogStart;
    float fogEnd;
    float pad0;
    float pad1;
    vec4 cameraPos;
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragTexSplat0;
layout(location = 4) out vec4 fragTexSplat1;
layout(location = 5) out float fragDistance;
layout(location = 6) out float fragSelection;
layout(location = 7) out float fragPaintAlpha;
layout(location = 8) out vec3 fragTexHSB;
layout(location = 9) out float fragHoleMask;
layout(location = 10) out vec4 fragTexSplat2;
layout(location = 11) out vec4 fragTexSplat3;
layout(location = 12) out vec3 fragWorldPos;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragNormal = inNormal;
    fragUV = inUV;
    fragTexSplat0 = inTexSplat0;
    fragTexSplat1 = inTexSplat1;
    fragTexSplat2 = inTexSplat2;
    fragTexSplat3 = inTexSplat3;
    fragDistance = gl_Position.w;  // Distance from camera
    fragSelection = inSelection;
    fragPaintAlpha = inPaintAlpha;
    fragTexHSB = inTexHSB;
    fragHoleMask = inHoleMask;
    fragWorldPos = inPosition;
}
