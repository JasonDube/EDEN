#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // Soft circle: distance from center of quad
    vec2 center = fragUV - vec2(0.5);
    float dist = length(center) * 2.0; // 0 at center, 1 at edge
    float circle = 1.0 - smoothstep(0.6, 1.0, dist);

    // Smoke color: dark gray with slight blue tint
    vec3 smokeColor = vec3(0.3, 0.3, 0.35);

    float alpha = circle * fragAlpha;
    if (alpha < 0.01) discard;

    outColor = vec4(smokeColor, alpha);
}
