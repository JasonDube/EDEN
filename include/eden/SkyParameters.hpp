#pragma once

#include <glm/glm.hpp>

namespace eden {

struct SkyParameters {
    // Zenith (top of sky)
    glm::vec3 zenithColor{0.02f, 0.008f, 0.04f};

    // Horizon colors - blended around the horizon for variety
    glm::vec3 horizonColor1{0.35f, 0.15f, 0.45f};  // Light purple
    glm::vec3 horizonColor2{0.45f, 0.18f, 0.40f};  // Pink-purple
    glm::vec3 horizonColor3{0.30f, 0.12f, 0.50f};  // Blue-purple
    glm::vec3 horizonColor4{0.40f, 0.20f, 0.35f};  // Dusty rose

    // Mid sky (between zenith and horizon)
    glm::vec3 midSkyColor{0.08f, 0.03f, 0.15f};

    // Below horizon
    glm::vec3 belowHorizonColor{0.06f, 0.02f, 0.10f};

    // Nebula settings
    float nebulaIntensity{0.25f};      // 0 = no nebula, 1 = full intensity
    float nebulaScale{2.5f};           // Size of nebula clouds
    glm::vec3 nebulaColor1{0.4f, 0.1f, 0.5f};   // Purple
    glm::vec3 nebulaColor2{0.6f, 0.15f, 0.4f};  // Magenta
    glm::vec3 nebulaColor3{0.2f, 0.1f, 0.4f};   // Deep blue-purple

    // Stars
    float starDensity{0.08f};          // Star probability (0-0.5 range, 0.08 = sparse)
    float starBrightness{1.0f};        // Overall star brightness multiplier
    float starSizeMin{0.5f};           // Minimum star size (0.5 = tiny dot)
    float starSizeMax{2.5f};           // Maximum star size (for brightest stars)
    float starTwinkle{0.0f};           // Twinkle amount (0 = none)
    float starColorIntensity{0.7f};    // Color saturation (0 = white, 1 = vivid colors)
    // Star color distribution (values 0-100, should sum to 100)
    float starWhitePercent{40.0f};
    float starBluePercent{15.0f};
    float starYellowPercent{25.0f};
    float starOrangePercent{15.0f};
    float starRedPercent{5.0f};

    // Horizon band width (how much of sky is "horizon colored")
    float horizonHeight{0.25f};        // Height of horizon blend zone

    // Space mode - when true, stars appear everywhere (full sphere)
    bool spaceMode{false};

    // Helper to get aligned size for uniform buffer
    static constexpr size_t alignedSize() {
        // Round up to 256 bytes for uniform buffer alignment
        return 256;
    }
};

// GPU-friendly version with proper alignment for uniform buffer
struct SkyParametersGPU {
    alignas(16) glm::vec4 zenithColor;           // w unused
    alignas(16) glm::vec4 horizonColor1;
    alignas(16) glm::vec4 horizonColor2;
    alignas(16) glm::vec4 horizonColor3;
    alignas(16) glm::vec4 horizonColor4;
    alignas(16) glm::vec4 midSkyColor;
    alignas(16) glm::vec4 belowHorizonColor;
    alignas(16) glm::vec4 nebulaColor1;
    alignas(16) glm::vec4 nebulaColor2;
    alignas(16) glm::vec4 nebulaColor3;
    alignas(16) glm::vec4 nebulaParams;          // x=intensity, y=scale, z=unused, w=unused
    alignas(16) glm::vec4 starParams;            // x=density, y=brightness, z=horizonHeight, w=twinkle
    alignas(16) glm::vec4 starSizeParams;        // x=sizeMin, y=sizeMax, z=colorIntensity, w=unused
    alignas(16) glm::vec4 starColorParams1;      // x=white, y=blue, z=yellow, w=orange
    alignas(16) glm::vec4 starColorParams2;      // x=red, y=unused, z=unused, w=unused

    static SkyParametersGPU fromCPU(const SkyParameters& p) {
        SkyParametersGPU gpu;
        gpu.zenithColor = glm::vec4(p.zenithColor, 1.0f);
        gpu.horizonColor1 = glm::vec4(p.horizonColor1, 1.0f);
        gpu.horizonColor2 = glm::vec4(p.horizonColor2, 1.0f);
        gpu.horizonColor3 = glm::vec4(p.horizonColor3, 1.0f);
        gpu.horizonColor4 = glm::vec4(p.horizonColor4, 1.0f);
        gpu.midSkyColor = glm::vec4(p.midSkyColor, 1.0f);
        gpu.belowHorizonColor = glm::vec4(p.belowHorizonColor, 1.0f);
        gpu.nebulaColor1 = glm::vec4(p.nebulaColor1, 1.0f);
        gpu.nebulaColor2 = glm::vec4(p.nebulaColor2, 1.0f);
        gpu.nebulaColor3 = glm::vec4(p.nebulaColor3, 1.0f);
        gpu.nebulaParams = glm::vec4(p.nebulaIntensity, p.nebulaScale, p.spaceMode ? 1.0f : 0.0f, 0.0f);
        gpu.starParams = glm::vec4(p.starDensity, p.starBrightness, p.horizonHeight, p.starTwinkle);
        gpu.starSizeParams = glm::vec4(p.starSizeMin, p.starSizeMax, p.starColorIntensity, 0.0f);
        // Convert 0-100 percentages to 0-1 range for shader
        gpu.starColorParams1 = glm::vec4(p.starWhitePercent / 100.0f, p.starBluePercent / 100.0f,
                                          p.starYellowPercent / 100.0f, p.starOrangePercent / 100.0f);
        gpu.starColorParams2 = glm::vec4(p.starRedPercent / 100.0f, 0.0f, 0.0f, 0.0f);
        return gpu;
    }
};

} // namespace eden
