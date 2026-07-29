#version 450

// Three-point shading, for the same reason LIME needed it: a single overhead
// light leaves the underside of a form flat and unreadable, and this scene is
// all form -- a ridge, a pit, and a small machine standing on them.

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorld;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 tint;
    vec4 eye;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Two-sided: the walker's panels and the terrain are both drawn unculled, so
    // a normal facing away from the eye is shaded as though it faced toward it
    // rather than going black.
    vec3 n = normalize(fragNormal);

    const vec3 keyDir  = normalize(vec3(-0.45, 0.80, 0.40));
    const vec3 fillDir = normalize(vec3( 0.70, 0.25, -0.35));
    const vec3 rimDir  = normalize(vec3( 0.10, -0.30, -0.95));

    float key  = max(abs(dot(n, keyDir)),  0.0);
    float fill = max(abs(dot(n, fillDir)), 0.0);
    float rim  = pow(max(abs(dot(n, rimDir)), 0.0), 3.0);

    vec3 keyCol  = vec3(1.00, 0.97, 0.90);
    vec3 fillCol = vec3(0.42, 0.52, 0.68);
    vec3 rimCol  = vec3(0.55, 0.85, 1.00);

    vec3 lit = fragColor * (0.18 + 0.78 * key * keyCol + 0.30 * fill * fillCol)
             + 0.22 * rim * rimCol * fragColor;

    float alpha = pc.tint.a;

    // ---- ghost ------------------------------------------------------------
    // A Fresnel term: surfaces turned away from the eye are nearly transparent,
    // surfaces seen edge-on are bright and opaque. That is what makes a
    // translucent solid read as a shell with a lit rim rather than as a
    // washed-out version of itself -- the silhouette carries the shape, which is
    // exactly the job the diagram's outlines were doing.
    if (pc.eye.w > 0.5) {
        vec3 view = normalize(pc.eye.xyz - fragWorld);
        float fresnel = pow(1.0 - clamp(abs(dot(n, view)), 0.0, 1.0), 2.5);

        vec3 glow = mix(vec3(0.72, 0.88, 0.29), vec3(0.62, 0.95, 1.00), 0.45);
        lit += glow * fresnel * 1.35;

        // Opaque at the rim, glassy face-on.
        alpha = mix(pc.tint.a, 1.0, fresnel);
    }

    outColor = vec4(lit, alpha);
}
