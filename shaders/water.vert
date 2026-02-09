#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    vec4 cameraPos;
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveFrequency;
} pc;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out float fragDistance;

void main() {
    vec3 worldPos = (pc.model * vec4(inPosition, 1.0)).xyz;

    // Animate waves using multiple sine waves for more natural look
    float wave1 = sin(worldPos.x * pc.waveFrequency * 0.7 + pc.time * 1.2) * pc.waveAmplitude;
    float wave2 = sin(worldPos.z * pc.waveFrequency * 0.5 + pc.time * 0.9) * pc.waveAmplitude * 0.8;
    float wave3 = sin((worldPos.x + worldPos.z) * pc.waveFrequency * 0.3 + pc.time * 1.5) * pc.waveAmplitude * 0.5;

    worldPos.y += wave1 + wave2 + wave3;

    // Calculate animated normal from wave derivatives
    float dx = pc.waveFrequency * 0.7 * cos(worldPos.x * pc.waveFrequency * 0.7 + pc.time * 1.2) * pc.waveAmplitude;
    dx += pc.waveFrequency * 0.3 * cos((worldPos.x + worldPos.z) * pc.waveFrequency * 0.3 + pc.time * 1.5) * pc.waveAmplitude * 0.5;

    float dz = pc.waveFrequency * 0.5 * cos(worldPos.z * pc.waveFrequency * 0.5 + pc.time * 0.9) * pc.waveAmplitude * 0.8;
    dz += pc.waveFrequency * 0.3 * cos((worldPos.x + worldPos.z) * pc.waveFrequency * 0.3 + pc.time * 1.5) * pc.waveAmplitude * 0.5;

    fragNormal = normalize(vec3(-dx, 1.0, -dz));

    gl_Position = pc.mvp * vec4(worldPos, 1.0);
    fragWorldPos = worldPos;
    fragUV = inUV;
    fragDistance = length(worldPos - pc.cameraPos.xyz);
}
