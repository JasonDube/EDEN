#version 450

layout(location = 0) in vec3 fragDirection;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SkyParams {
    vec4 zenithColor;
    vec4 horizonColor1;
    vec4 horizonColor2;
    vec4 horizonColor3;
    vec4 horizonColor4;
    vec4 midSkyColor;
    vec4 belowHorizonColor;
    vec4 nebulaColor1;
    vec4 nebulaColor2;
    vec4 nebulaColor3;
    vec4 nebulaParams;     // x=intensity, y=scale, z=spaceMode (1.0 = stars everywhere)
    vec4 starParams;       // x=density, y=brightness, z=horizonHeight, w=twinkle
    vec4 starSizeParams;   // x=sizeMin, y=sizeMax, z=colorIntensity
    vec4 starColorParams1; // x=white, y=blue, z=yellow, w=orange
    vec4 starColorParams2; // x=red
} sky;

// Hash function for pseudo-random values
float hash(vec3 p) {
    float h = dot(p, vec3(12.9898, 78.233, 37.719));
    return fract(abs(sin(h) * 43758.5453));
}

// Smooth noise
float smoothNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);

    // Smoothstep
    f = f * f * (3.0 - 2.0 * f);

    float v000 = hash(i + vec3(0, 0, 0));
    float v100 = hash(i + vec3(1, 0, 0));
    float v010 = hash(i + vec3(0, 1, 0));
    float v110 = hash(i + vec3(1, 1, 0));
    float v001 = hash(i + vec3(0, 0, 1));
    float v101 = hash(i + vec3(1, 0, 1));
    float v011 = hash(i + vec3(0, 1, 1));
    float v111 = hash(i + vec3(1, 1, 1));

    float v00 = mix(v000, v100, f.x);
    float v10 = mix(v010, v110, f.x);
    float v01 = mix(v001, v101, f.x);
    float v11 = mix(v011, v111, f.x);

    float v0 = mix(v00, v10, f.y);
    float v1 = mix(v01, v11, f.y);

    return mix(v0, v1, f.z);
}

// Fractal brownian motion
float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * smoothNoise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// Get nebula contribution
vec3 getNebula(vec3 dir) {
    float scale = sky.nebulaParams.y;
    float intensity = sky.nebulaParams.x;

    if (intensity < 0.001) return vec3(0.0);

    float n1 = fbm(dir * scale, 5);
    float n2 = fbm(dir * scale + vec3(100.0, 50.0, 25.0), 4);
    float n3 = fbm(dir * scale * 0.5 + vec3(200.0, 0.0, 0.0), 3);

    // Nebula mask - concentrated in certain areas
    float nebulaMask = smoothNoise(dir * 1.2 + vec3(5.0, 0.0, 0.0));
    nebulaMask = pow(nebulaMask, 1.5);

    // Only in upper sky (unless in space mode where nebulas can be everywhere)
    float spaceMode = sky.nebulaParams.z;
    if (spaceMode < 0.5) {
        float heightFade = clamp(dir.y * 2.0, 0.0, 1.0);
        nebulaMask *= heightFade;
    }

    vec3 nebula = sky.nebulaColor1.rgb * (n1 * 0.5) +
                  sky.nebulaColor2.rgb * (n2 * 0.3) +
                  sky.nebulaColor3.rgb * (n3 * 0.4);

    return nebula * nebulaMask * intensity;
}

// Get star contribution with stable positions using spherical coordinates
// Returns vec4(color.rgb, alpha) where alpha is how much to replace sky
vec4 getStars(vec3 dir) {
    float density = sky.starParams.x;
    float brightness = sky.starParams.y;
    float sizeMin = sky.starSizeParams.x;
    float sizeMax = sky.starSizeParams.y;

    if (density < 0.000001) return vec4(0.0);

    // Convert to spherical coordinates - these are FIXED in world space
    float theta = atan(dir.z, dir.x);  // -PI to PI (longitude)
    float phi = asin(clamp(dir.y, -1.0, 1.0));  // -PI/2 to PI/2 (latitude)

    // Create a grid in spherical space
    // Adjust grid density based on latitude to avoid bunching at poles
    float latScale = cos(phi) + 0.1;  // Prevent division issues near poles
    float gridTheta = 300.0;  // Longitude divisions
    float gridPhi = 150.0;    // Latitude divisions

    // Quantize to grid cells
    vec2 cell = floor(vec2(theta * gridTheta / 3.14159, phi * gridPhi / 1.5708));

    // Get cell center in spherical coords
    vec2 cellCenter = (cell + 0.5) * vec2(3.14159 / gridTheta, 1.5708 / gridPhi);

    // Hash the cell to determine if it has a star
    float cellHash = hash(vec3(cell, 0.0));

    // Density check - reduce density near poles to prevent clustering
    float adjustedDensity = density;
    // Reduce density near poles (where cells bunch up)
    float poleDistance = abs(phi) / 1.5708;  // 0 at equator, 1 at poles
    adjustedDensity *= 1.0 - poleDistance * poleDistance;  // Quadratic falloff toward poles
    // Fewer stars below horizon (unless in space mode where stars are everywhere)
    float spaceMode = sky.nebulaParams.z;
    if (dir.y < -0.1 && spaceMode < 0.5) adjustedDensity *= 0.1;

    if (cellHash > adjustedDensity) return vec4(0.0);

    // Calculate angular distance from cell center
    vec2 currentPos = vec2(theta, phi);
    vec2 offset = currentPos - cellCenter;
    // Account for latitude scaling in distance
    offset.x *= latScale;
    float angularDist = length(offset) * 100.0;  // Scale to reasonable units

    // Star properties from hash
    float starIntensity = hash(vec3(cell * 1.1, 1.0));
    float starSize = mix(sizeMin, sizeMax, starIntensity);

    // Soft circular falloff
    float falloff = 1.0 - smoothstep(0.0, starSize, angularDist);
    if (falloff <= 0.0) return vec4(0.0);

    // Final brightness - vary by star
    float starBright = (0.3 + starIntensity * 0.7) * brightness * falloff;

    // Color variation based on cell using 5 color percentages
    float colorVar = hash(vec3(cell * 2.2, 2.0));
    float colorIntensity = sky.starSizeParams.z;

    // Get thresholds from cumulative percentages
    float whiteEnd = sky.starColorParams1.x;
    float blueEnd = whiteEnd + sky.starColorParams1.y;
    float yellowEnd = blueEnd + sky.starColorParams1.z;
    float orangeEnd = yellowEnd + sky.starColorParams1.w;

    // Define vivid colors
    vec3 whiteColor = vec3(1.0, 1.0, 1.0);
    vec3 blueColor = vec3(0.2, 0.4, 1.0);
    vec3 yellowColor = vec3(1.0, 1.0, 0.0);
    vec3 orangeColor = vec3(1.0, 0.4, 0.0);
    vec3 redColor = vec3(1.0, 0.1, 0.1);

    vec3 starTint;
    if (colorVar < whiteEnd) {
        starTint = whiteColor;
    } else if (colorVar < blueEnd) {
        starTint = blueColor;
    } else if (colorVar < yellowEnd) {
        starTint = yellowColor;
    } else if (colorVar < orangeEnd) {
        starTint = orangeColor;
    } else {
        starTint = redColor;
    }

    // Apply color intensity - blend between white and the tint color
    vec3 finalTint = mix(whiteColor, starTint, colorIntensity);

    // Star color at full brightness
    vec3 starColor = finalTint * brightness;

    // Alpha = how much this star should replace the sky (not add to it)
    float starAlpha = clamp(starBright * 2.0, 0.0, 1.0);

    return vec4(starColor, starAlpha);
}

void main() {
    vec3 dir = normalize(fragDirection);
    float elevation = dir.y;
    float horizonHeight = sky.starParams.z;

    // Horizon color variation based on horizontal direction
    float horizonVar = fbm(vec3(dir.x * 3.0, 0.0, dir.z * 3.0), 3);

    vec3 horizonColor;
    if (horizonVar < 0.25) {
        horizonColor = mix(sky.horizonColor1.rgb, sky.horizonColor2.rgb, horizonVar * 4.0);
    } else if (horizonVar < 0.5) {
        horizonColor = mix(sky.horizonColor2.rgb, sky.horizonColor3.rgb, (horizonVar - 0.25) * 4.0);
    } else if (horizonVar < 0.75) {
        horizonColor = mix(sky.horizonColor3.rgb, sky.horizonColor4.rgb, (horizonVar - 0.5) * 4.0);
    } else {
        horizonColor = mix(sky.horizonColor4.rgb, sky.horizonColor1.rgb, (horizonVar - 0.75) * 4.0);
    }

    // Base sky gradient
    vec3 skyColor;
    if (elevation > horizonHeight) {
        // Upper sky: mid to zenith
        float t = (elevation - horizonHeight) / (1.0 - horizonHeight);
        t = sqrt(t);  // Ease transition
        skyColor = mix(sky.midSkyColor.rgb, sky.zenithColor.rgb, t);
    } else if (elevation > -0.05) {
        // Around horizon
        float t = (elevation + 0.05) / (horizonHeight + 0.05);
        skyColor = mix(horizonColor, sky.midSkyColor.rgb, t);
    } else {
        // Below horizon
        float t = min(1.0, (-elevation - 0.05) / 0.4);
        skyColor = mix(horizonColor, sky.belowHorizonColor.rgb, t);
    }

    // Add nebula
    skyColor += getNebula(dir);

    // Blend stars ON TOP of sky (replace, not add)
    vec4 starData = getStars(dir);
    skyColor = mix(skyColor, starData.rgb, starData.a);

    // Subtle noise for texture
    float noise = (hash(dir * 50.0) - 0.5) * 0.015;
    skyColor += vec3(noise, noise * 0.7, noise);

    outColor = vec4(clamp(skyColor, 0.0, 1.0), 1.0);
}
