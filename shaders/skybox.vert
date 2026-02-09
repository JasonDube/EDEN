#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;  // View-projection without translation
} pc;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    fragTexCoord = inPosition;
    vec4 pos = pc.viewProj * vec4(inPosition, 1.0);
    gl_Position = pos.xyww;  // Set z = w so depth is always max (rendered behind everything)
}
