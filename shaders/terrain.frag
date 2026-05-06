#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragTexSplat0;   // Weights for textures 0-3
layout(location = 4) in vec4 fragTexSplat1;   // Weights for textures 4-7
layout(location = 5) in vec4 fragTexSplat2;   // Weights for textures 8-11
layout(location = 6) in vec4 fragTexSplat3;   // Weights for textures 12-15
layout(location = 7) in vec4 fragTexSplat4;   // Weights for textures 16-19
layout(location = 8) in vec4 fragTexSplat5;   // Weights for textures 20-23
layout(location = 9) in vec4 fragTexSplat6;   // Weights for textures 24-27
layout(location = 10) in vec4 fragTexSplat7;  // Weights for textures 28-31
layout(location = 11) in float fragDistance;
layout(location = 12) in float fragSelection;
layout(location = 13) in float fragPaintAlpha;
layout(location = 14) in vec3 fragTexHSB;
layout(location = 15) in float fragHoleMask;
layout(location = 16) in vec3 fragWorldPos;

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

    // Sample only texture layers whose splatmap weight is nonzero. Skipping zero-weight
    // layers turns 64 unconditional samples (32 albedo + 32 normal) into typically 2-6
    // total on terrain that uses 1-3 dominant textures per area. Each pair shares one
    // branch so albedo and normal are sampled together.
    vec3 blendedTex = vec3(0.0);
    vec3 blendedNormal = vec3(0.0);

    #define SAMPLE_LAYER(layer, weight) \
        if (weight > 0.001) { \
            blendedTex    += texture(terrainTextures, vec3(fragUV, float(layer))).rgb * weight; \
            blendedNormal += texture(terrainNormals,  vec3(fragUV, float(layer))).rgb * weight; \
        }

    // Group early-out: if all 4 weights in a vec4 are ~0, skip the whole group.
    if (dot(fragTexSplat0, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(0,  fragTexSplat0.x)
        SAMPLE_LAYER(1,  fragTexSplat0.y)
        SAMPLE_LAYER(2,  fragTexSplat0.z)
        SAMPLE_LAYER(3,  fragTexSplat0.w)
    }
    if (dot(fragTexSplat1, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(4,  fragTexSplat1.x)
        SAMPLE_LAYER(5,  fragTexSplat1.y)
        SAMPLE_LAYER(6,  fragTexSplat1.z)
        SAMPLE_LAYER(7,  fragTexSplat1.w)
    }
    if (dot(fragTexSplat2, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(8,  fragTexSplat2.x)
        SAMPLE_LAYER(9,  fragTexSplat2.y)
        SAMPLE_LAYER(10, fragTexSplat2.z)
        SAMPLE_LAYER(11, fragTexSplat2.w)
    }
    if (dot(fragTexSplat3, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(12, fragTexSplat3.x)
        SAMPLE_LAYER(13, fragTexSplat3.y)
        SAMPLE_LAYER(14, fragTexSplat3.z)
        SAMPLE_LAYER(15, fragTexSplat3.w)
    }
    if (dot(fragTexSplat4, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(16, fragTexSplat4.x)
        SAMPLE_LAYER(17, fragTexSplat4.y)
        SAMPLE_LAYER(18, fragTexSplat4.z)
        SAMPLE_LAYER(19, fragTexSplat4.w)
    }
    if (dot(fragTexSplat5, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(20, fragTexSplat5.x)
        SAMPLE_LAYER(21, fragTexSplat5.y)
        SAMPLE_LAYER(22, fragTexSplat5.z)
        SAMPLE_LAYER(23, fragTexSplat5.w)
    }
    if (dot(fragTexSplat6, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(24, fragTexSplat6.x)
        SAMPLE_LAYER(25, fragTexSplat6.y)
        SAMPLE_LAYER(26, fragTexSplat6.z)
        SAMPLE_LAYER(27, fragTexSplat6.w)
    }
    if (dot(fragTexSplat7, vec4(1.0)) > 0.001) {
        SAMPLE_LAYER(28, fragTexSplat7.x)
        SAMPLE_LAYER(29, fragTexSplat7.y)
        SAMPLE_LAYER(30, fragTexSplat7.z)
        SAMPLE_LAYER(31, fragTexSplat7.w)
    }

    #undef SAMPLE_LAYER

    // If no weights anywhere, blendedNormal stays (0,0,0). Decoded below as (-1,-1,-1)
    // would be a degenerate normal — fall back to flat (0,0,1) tangent-space normal so
    // lighting matches an unblended surface instead of going dark.
    if (dot(blendedNormal, vec3(1.0)) < 0.001) {
        blendedNormal = vec3(0.5, 0.5, 1.0);  // encodes tangent-space (0,0,1)
    }

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
