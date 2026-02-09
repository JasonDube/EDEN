#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in float fragDistance;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    vec4 cameraPos;
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveFrequency;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Water colors
    vec3 shallowColor = vec3(0.1, 0.4, 0.5);
    vec3 deepColor = vec3(0.0, 0.1, 0.2);
    vec3 specularColor = vec3(1.0, 1.0, 1.0);

    // View direction
    vec3 viewDir = normalize(pc.cameraPos.xyz - fragWorldPos);

    // Simple directional light (sun)
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));

    // Fresnel effect - more reflective at grazing angles
    float fresnel = pow(1.0 - max(dot(viewDir, fragNormal), 0.0), 3.0);
    fresnel = mix(0.2, 1.0, fresnel);

    // Specular highlight
    vec3 halfDir = normalize(lightDir + viewDir);
    float specular = pow(max(dot(fragNormal, halfDir), 0.0), 128.0);

    // Diffuse lighting
    float diffuse = max(dot(fragNormal, lightDir), 0.0) * 0.3 + 0.7;

    // Mix shallow/deep based on fresnel (simplified - no depth buffer)
    vec3 waterColor = mix(shallowColor, deepColor, fresnel * 0.5);

    // Apply lighting
    vec3 finalColor = waterColor * diffuse + specularColor * specular * 0.8;

    // Add subtle variation based on position
    float variation = sin(fragWorldPos.x * 0.1 + pc.time * 0.5) * sin(fragWorldPos.z * 0.1 + pc.time * 0.3) * 0.05;
    finalColor += variation;

    // Distance fog
    float fogStart = 200.0;
    float fogEnd = 2000.0;
    float fogFactor = clamp((fragDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    vec3 fogColor = vec3(0.7, 0.85, 1.0);
    finalColor = mix(finalColor, fogColor, fogFactor * 0.5);

    // Alpha for transparency - less transparent at distance, more at grazing angles
    float alpha = mix(0.7, 0.9, fresnel);
    alpha = mix(alpha, 0.95, fogFactor);

    outColor = vec4(finalColor, alpha);
}
