#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragTexWeights;
layout(location = 4) in flat uvec4 fragTexIndices;
layout(location = 5) in float fragDistance;
layout(location = 6) in float fragSelection;
layout(location = 7) in float fragPaintAlpha;
layout(location = 8) in vec3 fragTexHSB;  // Per-vertex HSB (hue, saturation, brightness)

// Texture array (all terrain textures in a single 2D array)
layout(set = 0, binding = 0) uniform sampler2DArray terrainTextures;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 fogColor;
    float fogStart;
    float fogEnd;
} pc;

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

// Apply HSV adjustments to a color
vec3 adjustColor(vec3 color, float hueShift, float satMult, float brightMult) {
    vec3 hsv = rgb2hsv(color);
    hsv.x = fract(hsv.x + hueShift / 360.0);
    hsv.y = clamp(hsv.y * satMult, 0.0, 1.0);
    hsv.z = clamp(hsv.z * brightMult, 0.0, 1.0);
    return hsv2rgb(hsv);
}

void main() {
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float ambient = 0.3;
    float diffuse = max(dot(normalize(fragNormal), lightDir), 0.0) * 0.7;
    float lighting = ambient + diffuse;

    // Sample textures from array using indices
    vec3 tex0 = texture(terrainTextures, vec3(fragUV, float(fragTexIndices.x))).rgb;
    vec3 tex1 = texture(terrainTextures, vec3(fragUV, float(fragTexIndices.y))).rgb;
    vec3 tex2 = texture(terrainTextures, vec3(fragUV, float(fragTexIndices.z))).rgb;
    vec3 tex3 = texture(terrainTextures, vec3(fragUV, float(fragTexIndices.w))).rgb;

    // Blend textures based on weights
    vec3 blendedTex = tex0 * fragTexWeights.x +
                      tex1 * fragTexWeights.y +
                      tex2 * fragTexWeights.z +
                      tex3 * fragTexWeights.w;

    // Apply per-vertex HSB color adjustment to the blended texture
    blendedTex = adjustColor(blendedTex, fragTexHSB.x, fragTexHSB.y, fragTexHSB.z);

    // Blend between texture and painted color based on paintAlpha
    vec3 baseColor = mix(blendedTex, fragColor, fragPaintAlpha);

    // Apply lighting
    vec3 finalColor = baseColor * lighting;

    // Selection overlay - orange/yellow tint for selected vertices
    if (fragSelection > 0.0) {
        vec3 selectionColor = vec3(1.0, 0.5, 0.0);  // Orange
        finalColor = mix(finalColor, selectionColor, fragSelection * 0.5);
    }

    // Distance fog - blend to fog color at far distances
    float fogFactor = clamp((fragDistance - pc.fogStart) / (pc.fogEnd - pc.fogStart), 0.0, 1.0);
    finalColor = mix(finalColor, pc.fogColor.rgb, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
