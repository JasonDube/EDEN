#version 450

layout(set = 0, binding = 0) uniform sampler2D texSampler;

struct PointLight {
    vec4 position;    // xyz = position, w = radius
    vec4 color;       // xyz = color, w = intensity
    vec4 direction;   // xyz = direction (0,0,0 = point light), w = cone angle cosine
};

layout(set = 1, binding = 0) uniform LightUBO {
    PointLight lights[16];
    int numLights;
} lightData;

layout(location = 0) flat in vec3 fragNormal;  // flat shading - no interpolation
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec4 fragColorAdjust;  // x=hue, y=saturation, z=brightness, w=alpha

layout(location = 0) out vec4 outColor;

// RGB to HSV conversion
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB conversion
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    // Sample texture
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Combine texture with vertex color
    vec3 baseColor = texColor.rgb * fragColor.rgb;

    // Check for hit flash (saturation > 2.5 signals hit flash mode)
    bool hitFlash = fragColorAdjust.y > 2.5;

    vec3 adjustedColor;
    if (hitFlash) {
        // Override to bright red for hit flash
        adjustedColor = vec3(1.0, 0.1, 0.1);
    } else {
        // Apply HSV adjustments
        vec3 hsv = rgb2hsv(baseColor);

        // Hue shift (fragColorAdjust.x is in degrees, convert to 0-1 range)
        hsv.x = fract(hsv.x + fragColorAdjust.x / 360.0);

        // Saturation multiplier
        hsv.y = clamp(hsv.y * fragColorAdjust.y, 0.0, 1.0);

        // Brightness/Value multiplier
        hsv.z = clamp(hsv.z * fragColorAdjust.z, 0.0, 1.0);

        // Convert back to RGB
        adjustedColor = hsv2rgb(hsv);
    }

    // Directional lighting (sun) — indoor blocks get minimal ambient
    vec3 normal = normalize(fragNormal);
    vec3 sunDir = normalize(vec3(0.5, 1.0, 0.3));
    float sunDiffuse = max(dot(normal, sunDir), 0.0);
    bool isIndoor = fragColorAdjust.w < -0.5;
    float ambient = isIndoor ? 0.05 : 0.4;
    float sunContrib = isIndoor ? 0.0 : 0.4;
    float lighting = ambient + sunDiffuse * sunContrib;

    vec3 finalColor = adjustedColor * lighting;

    // Point lights and spotlights
    for (int i = 0; i < lightData.numLights; i++) {
        vec3 lightPos = lightData.lights[i].position.xyz;
        float radius = lightData.lights[i].position.w;
        vec3 lightColor = lightData.lights[i].color.xyz;
        float intensity = lightData.lights[i].color.w;
        vec3 spotDir = lightData.lights[i].direction.xyz;
        float coneCos = lightData.lights[i].direction.w;

        vec3 toLight = lightPos - fragWorldPos;
        float dist = length(toLight);
        if (dist > radius) continue;

        vec3 lightDir = toLight / dist;

        // Spotlight cone check: if direction is non-zero, it's a spotlight
        float spotFactor = 1.0;
        if (coneCos > 0.01) {
            // How much does the fragment direction align with the spot direction?
            float theta = dot(-lightDir, normalize(spotDir));
            if (theta < coneCos) continue;  // Outside the cone
            // Soft edge — fade near cone boundary
            float outerCos = coneCos * 0.9;  // Slightly wider for soft edge
            spotFactor = clamp((theta - outerCos) / (coneCos - outerCos + 0.001), 0.0, 1.0);
            spotFactor = spotFactor * spotFactor;  // Smooth falloff at edges
        }

        float diffuse = max(dot(normal, lightDir), 0.0);
        float attenuation = 1.0 - (dist / radius);
        attenuation = attenuation * attenuation;

        finalColor += adjustedColor * lightColor * diffuse * attenuation * intensity * spotFactor;
    }

    // Apply alpha multiplier (w>0 means x-ray, w<0 means indoor, w=0 means normal)
    float alphaMult = fragColorAdjust.w > 0.0 ? fragColorAdjust.w : 1.0;
    outColor = vec4(finalColor, texColor.a * fragColor.a * alphaMult);
}
