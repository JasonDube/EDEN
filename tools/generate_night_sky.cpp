// Generate a seamless purple night sky cubemap with moon and nebula
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <algorithm>

constexpr int FACE_SIZE = 512;
constexpr int CANVAS_WIDTH = FACE_SIZE * 4;
constexpr int CANVAS_HEIGHT = FACE_SIZE * 3;

struct Color {
    float r, g, b;

    Color operator*(float s) const { return {r*s, g*s, b*s}; }
    Color operator+(Color c) const { return {r+c.r, g+c.g, b+c.b}; }
};

Color lerp(Color c1, Color c2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        c1.r + (c2.r - c1.r) * t,
        c1.g + (c2.g - c1.g) * t,
        c1.b + (c2.b - c1.b) * t
    };
}

float randf() {
    return static_cast<float>(rand()) / RAND_MAX;
}

// Simple noise function
float hash(float x, float y, float z) {
    float h = x * 12.9898f + y * 78.233f + z * 37.719f;
    return std::fmod(std::abs(std::sin(h) * 43758.5453f), 1.0f);
}

// Smooth noise
float smoothNoise(float x, float y, float z) {
    float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;

    // Smoothstep
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    fz = fz * fz * (3.0f - 2.0f * fz);

    float v000 = hash(ix, iy, iz);
    float v100 = hash(ix+1, iy, iz);
    float v010 = hash(ix, iy+1, iz);
    float v110 = hash(ix+1, iy+1, iz);
    float v001 = hash(ix, iy, iz+1);
    float v101 = hash(ix+1, iy, iz+1);
    float v011 = hash(ix, iy+1, iz+1);
    float v111 = hash(ix+1, iy+1, iz+1);

    float v00 = v000 * (1-fx) + v100 * fx;
    float v10 = v010 * (1-fx) + v110 * fx;
    float v01 = v001 * (1-fx) + v101 * fx;
    float v11 = v011 * (1-fx) + v111 * fx;

    float v0 = v00 * (1-fy) + v10 * fy;
    float v1 = v01 * (1-fy) + v11 * fy;

    return v0 * (1-fz) + v1 * fz;
}

// Fractal brownian motion noise
float fbm(float x, float y, float z, int octaves = 4) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * smoothNoise(x * frequency, y * frequency, z * frequency);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value;
}

// Convert pixel coordinates to 3D direction for each cube face
void pixelToDirection(int face, int x, int y, float& dx, float& dy, float& dz) {
    float u = (2.0f * (x + 0.5f) / FACE_SIZE) - 1.0f;
    float v = (2.0f * (y + 0.5f) / FACE_SIZE) - 1.0f;

    switch (face) {
        case 0: dx = 1.0f; dy = -v; dz = -u; break;  // +X
        case 1: dx = -1.0f; dy = -v; dz = u; break;  // -X
        case 2: dx = u; dy = 1.0f; dz = v; break;    // +Y
        case 3: dx = u; dy = -1.0f; dz = -v; break;  // -Y
        case 4: dx = u; dy = -v; dz = 1.0f; break;   // +Z
        case 5: dx = -u; dy = -v; dz = -1.0f; break; // -Z
    }

    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    dx /= len; dy /= len; dz /= len;
}

// Moon direction (upper right area of sky)
const float moonDirX = 0.5f, moonDirY = 0.7f, moonDirZ = 0.5f;
const float moonSize = 0.08f;
const float moonGlowSize = 0.35f;

float getMoonIntensity(float dx, float dy, float dz) {
    // Normalize moon direction
    float len = std::sqrt(moonDirX*moonDirX + moonDirY*moonDirY + moonDirZ*moonDirZ);
    float mx = moonDirX/len, my = moonDirY/len, mz = moonDirZ/len;

    // Dot product = cosine of angle
    float dot = dx*mx + dy*my + dz*mz;
    float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));

    // Moon disc
    if (angle < moonSize) {
        float t = angle / moonSize;
        // Slight darkening at edge (limb darkening)
        return 0.85f + 0.15f * (1.0f - t * t);
    }

    // Moon glow
    if (angle < moonGlowSize) {
        float t = (angle - moonSize) / (moonGlowSize - moonSize);
        return 0.4f * std::pow(1.0f - t, 2.0f);
    }

    return 0.0f;
}

// Nebula effect
Color getNebula(float dx, float dy, float dz) {
    // Scale for nebula clouds
    float scale = 2.5f;

    // Get noise value
    float n1 = fbm(dx * scale, dy * scale, dz * scale, 5);
    float n2 = fbm(dx * scale + 100.0f, dy * scale + 50.0f, dz * scale + 25.0f, 4);
    float n3 = fbm(dx * scale * 0.5f + 200.0f, dy * scale * 0.5f, dz * scale * 0.5f, 3);

    // Create wispy nebula effect - concentrated in certain areas
    float nebulaMask = smoothNoise(dx * 1.2f + 5.0f, dy * 1.2f, dz * 1.2f);
    nebulaMask = std::pow(nebulaMask, 1.5f);

    // Only show nebula in upper half of sky, fading at horizon
    float heightFade = std::clamp(dy * 2.0f, 0.0f, 1.0f);
    nebulaMask *= heightFade;

    // Nebula colors - purples, magentas, deep blues
    Color nebulaColor1 = {0.4f, 0.1f, 0.5f};   // Purple
    Color nebulaColor2 = {0.6f, 0.15f, 0.4f};  // Magenta
    Color nebulaColor3 = {0.2f, 0.1f, 0.4f};   // Deep blue-purple

    Color nebula = nebulaColor1 * (n1 * 0.5f) +
                   nebulaColor2 * (n2 * 0.3f) +
                   nebulaColor3 * (n3 * 0.4f);

    // Apply mask and reduce intensity
    nebula = nebula * (nebulaMask * 0.25f);

    return nebula;
}

// Get sky color based on direction
Color getSkyColor(float dx, float dy, float dz) {
    // Base sky colors
    Color zenith = {0.02f, 0.008f, 0.04f};       // Very dark purple at top
    Color midSky = {0.08f, 0.03f, 0.15f};        // Mid purple

    // Horizon colors - more variety of purples/magentas
    float horizonVariation = fbm(dx * 3.0f, 0.0f, dz * 3.0f, 3);

    Color horizon1 = {0.35f, 0.15f, 0.45f};     // Light purple
    Color horizon2 = {0.45f, 0.18f, 0.40f};     // Pink-purple
    Color horizon3 = {0.30f, 0.12f, 0.50f};     // Blue-purple
    Color horizon4 = {0.40f, 0.20f, 0.35f};     // Dusty rose

    // Blend horizon colors based on position
    Color horizon;
    if (horizonVariation < 0.25f) {
        horizon = lerp(horizon1, horizon2, horizonVariation * 4.0f);
    } else if (horizonVariation < 0.5f) {
        horizon = lerp(horizon2, horizon3, (horizonVariation - 0.25f) * 4.0f);
    } else if (horizonVariation < 0.75f) {
        horizon = lerp(horizon3, horizon4, (horizonVariation - 0.5f) * 4.0f);
    } else {
        horizon = lerp(horizon4, horizon1, (horizonVariation - 0.75f) * 4.0f);
    }

    Color belowHorizon = {0.06f, 0.02f, 0.10f};

    float elevation = dy;

    Color skyColor;
    if (elevation > 0.2f) {
        // Upper sky
        float t = (elevation - 0.2f) / 0.8f;
        t = std::sqrt(t);  // Ease the transition
        skyColor = lerp(midSky, zenith, t);
    } else if (elevation > -0.05f) {
        // Around horizon - wider band
        float t = (elevation + 0.05f) / 0.25f;
        skyColor = lerp(horizon, midSky, t);
    } else {
        // Below horizon
        float t = std::min(1.0f, (-elevation - 0.05f) / 0.4f);
        skyColor = lerp(horizon, belowHorizon, t);
    }

    // Add nebula
    Color nebula = getNebula(dx, dy, dz);
    skyColor = skyColor + nebula;

    // Add moon
    float moonIntensity = getMoonIntensity(dx, dy, dz);
    if (moonIntensity > 0.0f) {
        Color moonColor = {0.95f, 0.92f, 0.85f};  // Slightly warm white
        Color moonGlow = {0.4f, 0.35f, 0.5f};     // Purple-ish glow

        if (moonIntensity > 0.5f) {
            // Moon disc
            skyColor = lerp(skyColor, moonColor, moonIntensity);
        } else {
            // Moon glow
            skyColor = skyColor + moonGlow * moonIntensity;
        }
    }

    // Subtle noise
    float noise = (hash(dx * 50.0f, dy * 50.0f, dz * 50.0f) - 0.5f) * 0.015f;
    skyColor.r = std::clamp(skyColor.r + noise, 0.0f, 1.0f);
    skyColor.g = std::clamp(skyColor.g + noise * 0.7f, 0.0f, 1.0f);
    skyColor.b = std::clamp(skyColor.b + noise, 0.0f, 1.0f);

    return skyColor;
}

void setPixel(std::vector<uint8_t>& canvas, int x, int y, Color c) {
    int idx = (y * CANVAS_WIDTH + x) * 3;
    canvas[idx + 0] = static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f));
    canvas[idx + 1] = static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f));
    canvas[idx + 2] = static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f));
}

Color getPixel(std::vector<uint8_t>& canvas, int x, int y) {
    int idx = (y * CANVAS_WIDTH + x) * 3;
    return {
        canvas[idx + 0] / 255.0f,
        canvas[idx + 1] / 255.0f,
        canvas[idx + 2] / 255.0f
    };
}

// Add sparse stars
void addStars(std::vector<uint8_t>& canvas, int faceIdx, int startX, int startY) {
    for (int y = 0; y < FACE_SIZE; y++) {
        for (int x = 0; x < FACE_SIZE; x++) {
            float dx, dy, dz;
            pixelToDirection(faceIdx, x, y, dx, dy, dz);

            // Hash for star placement
            float h = hash(dx * 100.0f, dy * 100.0f, dz * 100.0f);

            // Much lower star density
            float starChance = 0.0008f;
            if (dy > 0) starChance += dy * 0.001f;
            if (dy < -0.2f) starChance = 0.0001f;

            // Skip stars near moon
            float moonDist = getMoonIntensity(dx, dy, dz);
            if (moonDist > 0.1f) continue;

            if (h < starChance) {
                float brightness = 0.4f + h * 50.0f;
                brightness = std::clamp(brightness, 0.4f, 1.0f);

                // Color variation - some bluish, some white, some warm
                Color starColor;
                float colorVar = hash(dx * 200.0f, dy * 200.0f, dz * 200.0f);
                if (colorVar < 0.3f) {
                    starColor = {brightness * 0.9f, brightness * 0.9f, brightness};  // Bluish
                } else if (colorVar < 0.7f) {
                    starColor = {brightness, brightness, brightness};  // White
                } else {
                    starColor = {brightness, brightness * 0.95f, brightness * 0.85f};  // Warm
                }

                int px = startX + x;
                int py = startY + y;
                setPixel(canvas, px, py, starColor);

                // Very bright stars get tiny glow
                if (brightness > 0.9f && x > 0 && x < FACE_SIZE-1 && y > 0 && y < FACE_SIZE-1) {
                    Color dim = starColor * 0.25f;
                    Color existing;

                    existing = getPixel(canvas, px-1, py);
                    setPixel(canvas, px-1, py, {std::max(existing.r, dim.r), std::max(existing.g, dim.g), std::max(existing.b, dim.b)});

                    existing = getPixel(canvas, px+1, py);
                    setPixel(canvas, px+1, py, {std::max(existing.r, dim.r), std::max(existing.g, dim.g), std::max(existing.b, dim.b)});

                    existing = getPixel(canvas, px, py-1);
                    setPixel(canvas, px, py-1, {std::max(existing.r, dim.r), std::max(existing.g, dim.g), std::max(existing.b, dim.b)});

                    existing = getPixel(canvas, px, py+1);
                    setPixel(canvas, px, py+1, {std::max(existing.r, dim.r), std::max(existing.g, dim.g), std::max(existing.b, dim.b)});
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    srand(42);

    std::vector<uint8_t> canvas(CANVAS_WIDTH * CANVAS_HEIGHT * 3, 0);

    struct FaceInfo {
        const char* name;
        int col, row;
        int faceIdx;
    };

    FaceInfo faces[] = {
        {"+Y (top)",    1, 0, 2},
        {"-X (left)",   0, 1, 1},
        {"+Z (front)",  1, 1, 4},
        {"+X (right)",  2, 1, 0},
        {"-Z (back)",   3, 1, 5},
        {"-Y (bottom)", 1, 2, 3},
    };

    std::cout << "Generating night sky with moon and nebula...\n";

    for (const auto& face : faces) {
        std::cout << "  Rendering face " << face.name << "...\n";

        int startX = face.col * FACE_SIZE;
        int startY = face.row * FACE_SIZE;

        for (int y = 0; y < FACE_SIZE; y++) {
            for (int x = 0; x < FACE_SIZE; x++) {
                float dx, dy, dz;
                pixelToDirection(face.faceIdx, x, y, dx, dy, dz);

                Color skyColor = getSkyColor(dx, dy, dz);
                setPixel(canvas, startX + x, startY + y, skyColor);
            }
        }
    }

    std::cout << "  Adding stars...\n";
    for (const auto& face : faces) {
        int startX = face.col * FACE_SIZE;
        int startY = face.row * FACE_SIZE;
        addStars(canvas, face.faceIdx, startX, startY);
    }

    const char* outputPath = "sky_box/night_sky_purple.png";
    if (argc > 1) {
        outputPath = argv[1];
    }

    if (stbi_write_png(outputPath, CANVAS_WIDTH, CANVAS_HEIGHT, 3, canvas.data(), CANVAS_WIDTH * 3)) {
        std::cout << "Saved to: " << outputPath << "\n";
    } else {
        std::cerr << "Failed to write image!\n";
        return 1;
    }

    return 0;
}
