#version 450

// One vertex format for the whole TESSARA:AXIOM scene -- the heightfield and the
// walker are drawn by the same pipeline, so there is nothing to keep in sync.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 tint;        // rgb multiplies vertex colour, a is alpha
    vec4 eye;         // xyz camera world position, w = 1 for the ghost pass
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorld;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor * pc.tint.rgb;
    fragNormal = inNormal;

    // Nothing in this scene has a model matrix: the terrain and the creature are
    // both built in world space, so the input position IS the world position.
    fragWorld = inPosition;
}
