#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragTexSplat0;   // Weights for textures 0-3
layout(location = 4) in vec4 fragTexSplat1;   // Weights for textures 4-7
layout(location = 5) in float fragDistance;
layout(location = 6) in float fragSelection;
layout(location = 7) in float fragPaintAlpha;
layout(location = 8) in vec3 fragTexHSB;  // Per-vertex HSB (hue, saturation, brightness)
layout(location = 9) in float fragHoleMask;
layout(location = 10) in vec4 fragTexSplat2;  // Weights for textures 8-11
layout(location = 11) in vec4 fragTexSplat3;  // Weights for textures 12-15
layout(location = 12) in vec3 fragWorldPos;

// Texture arrays
layout(set = 0, binding = 0) uniform sampler2DArray terrainTextures;
layout(set = 0, binding = 1) uniform sampler2DArray terrainNormals;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 fogColor;
    float fogStart;
    float fogEnd;
    float pad0;
    float pad1;
    vec4 cameraPos;
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
    // Discard terrain fragments marked as holes
    if (fragHoleMask > 0.5) discard;

    // Sample all 16 texture layers and blend by splatmap weights
    vec3 blendedTex = vec3(0.0);
    blendedTex += texture(terrainTextures, vec3(fragUV, 0.0)).rgb * fragTexSplat0.x;
    blendedTex += texture(terrainTextures, vec3(fragUV, 1.0)).rgb * fragTexSplat0.y;
    blendedTex += texture(terrainTextures, vec3(fragUV, 2.0)).rgb * fragTexSplat0.z;
    blendedTex += texture(terrainTextures, vec3(fragUV, 3.0)).rgb * fragTexSplat0.w;
    blendedTex += texture(terrainTextures, vec3(fragUV, 4.0)).rgb * fragTexSplat1.x;
    blendedTex += texture(terrainTextures, vec3(fragUV, 5.0)).rgb * fragTexSplat1.y;
    blendedTex += texture(terrainTextures, vec3(fragUV, 6.0)).rgb * fragTexSplat1.z;
    blendedTex += texture(terrainTextures, vec3(fragUV, 7.0)).rgb * fragTexSplat1.w;
    blendedTex += texture(terrainTextures, vec3(fragUV, 8.0)).rgb * fragTexSplat2.x;
    blendedTex += texture(terrainTextures, vec3(fragUV, 9.0)).rgb * fragTexSplat2.y;
    blendedTex += texture(terrainTextures, vec3(fragUV, 10.0)).rgb * fragTexSplat2.z;
    blendedTex += texture(terrainTextures, vec3(fragUV, 11.0)).rgb * fragTexSplat2.w;
    blendedTex += texture(terrainTextures, vec3(fragUV, 12.0)).rgb * fragTexSplat3.x;
    blendedTex += texture(terrainTextures, vec3(fragUV, 13.0)).rgb * fragTexSplat3.y;
    blendedTex += texture(terrainTextures, vec3(fragUV, 14.0)).rgb * fragTexSplat3.z;
    blendedTex += texture(terrainTextures, vec3(fragUV, 15.0)).rgb * fragTexSplat3.w;

    // Sample all 16 normal map layers and blend by same splatmap weights
    vec3 blendedNormal = vec3(0.0);
    blendedNormal += texture(terrainNormals, vec3(fragUV, 0.0)).rgb * fragTexSplat0.x;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 1.0)).rgb * fragTexSplat0.y;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 2.0)).rgb * fragTexSplat0.z;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 3.0)).rgb * fragTexSplat0.w;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 4.0)).rgb * fragTexSplat1.x;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 5.0)).rgb * fragTexSplat1.y;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 6.0)).rgb * fragTexSplat1.z;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 7.0)).rgb * fragTexSplat1.w;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 8.0)).rgb * fragTexSplat2.x;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 9.0)).rgb * fragTexSplat2.y;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 10.0)).rgb * fragTexSplat2.z;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 11.0)).rgb * fragTexSplat2.w;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 12.0)).rgb * fragTexSplat3.x;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 13.0)).rgb * fragTexSplat3.y;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 14.0)).rgb * fragTexSplat3.z;
    blendedNormal += texture(terrainNormals, vec3(fragUV, 15.0)).rgb * fragTexSplat3.w;

    // Decode tangent-space normal from [0,1] -> [-1,1]
    vec3 tangentNormal = blendedNormal * 2.0 - 1.0;

    // Build TBN matrix from screen-space derivatives (no extra vertex attributes needed)
    vec3 N = normalize(fragNormal);
    vec3 dp1 = dFdx(fragWorldPos);
    vec3 dp2 = dFdy(fragWorldPos);
    vec2 duv1 = dFdx(fragUV);
    vec2 duv2 = dFdy(fragUV);

    vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = normalize(dp2 * duv1.x - dp1 * duv2.x);
    mat3 TBN = mat3(T, B, N);

    // Transform tangent-space normal to world space
    vec3 worldNormal = normalize(TBN * tangentNormal);

    // Directional lighting using the normal-mapped normal
    vec3 lightDir = normalize(vec3(0.5, pc.pad0, 0.3)); // pad0 = sunY
    float ambient = pc.pad1; // pad1 = ambientLevel
    float diffuse = max(dot(worldNormal, lightDir), 0.0) * 0.7;

    // Specular (Blinn-Phong) — makes normal map bumps catch the light
    vec3 viewDir = normalize(pc.cameraPos.xyz - fragWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specAngle = max(dot(worldNormal, halfDir), 0.0);
    float specular = pow(specAngle, 32.0) * 0.3;

    float lighting = ambient + diffuse + specular;

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
