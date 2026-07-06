#version 450

// Grass (and other alpha-cutout foliage) fragment shader. Identical descriptor
// layout to model.frag so it reuses the instanced pipeline layout (set 0 = texture,
// set 1 = light UBO), but it ALPHA-TESTS: fully-transparent texels are discarded so a
// flat cross-quad reads as wispy blades instead of a solid rectangle. No alpha
// blending — cutout keeps depth sorting trivial and the grass writes depth normally.

layout(set = 0, binding = 0) uniform sampler2D texSampler;

struct PointLight {
    vec4 position;    // xyz = position, w = radius
    vec4 color;       // xyz = color, w = intensity
    vec4 direction;   // xyz = direction (0,0,0 = point light), w = cone angle cosine
};

layout(set = 1, binding = 0) uniform LightUBO {
    PointLight lights[16];
    int numLights;
    float sunY;
    float ambientLevel;
} lightData;

layout(location = 0) flat in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec4 fragColorAdjust;  // x=hue, y=saturation, z=brightness, w=alpha

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);

    // ALPHA TEST: cut away the transparent gaps between blades. This is the whole
    // reason grass needs its own pipeline — a flat quad becomes a tuft of blades.
    if (texColor.a < 0.5) discard;

    vec3 baseColor = texColor.rgb * fragColor.rgb;

    // Per-instance brightness jitter (packed into colorAdjust.z) so a field of grass
    // isn't a flat uniform green. Hue/sat left neutral for now.
    float bright = fragColorAdjust.z > 0.0 ? fragColorAdjust.z : 1.0;
    baseColor *= bright;

    // Simple two-sided lighting: grass is a thin sheet, so light it by the sun's
    // vertical term without caring which face we're seeing (abs on the up component).
    vec3 sunDir = normalize(vec3(0.5, lightData.sunY, 0.3));
    float ambient = lightData.ambientLevel;
    float sunDiffuse = clamp(0.4 + 0.6 * max(sunDir.y, 0.0), 0.0, 1.0);
    float lighting = clamp(ambient + sunDiffuse * lightData.ambientLevel, 0.0, 1.2);

    outColor = vec4(baseColor * lighting, 1.0);   // cutout: always fully opaque where kept
}
