#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <array>

namespace eden {

class Noise {
public:
    // Single octave Perlin noise, returns value in range [-1, 1]
    static float perlin(float x, float y) {
        // Grid cell coordinates
        int x0 = static_cast<int>(std::floor(x));
        int y0 = static_cast<int>(std::floor(y));
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        // Interpolation weights
        float sx = x - static_cast<float>(x0);
        float sy = y - static_cast<float>(y0);

        // Smooth interpolation
        float u = fade(sx);
        float v = fade(sy);

        // Gradient dot products
        float n00 = dotGridGradient(x0, y0, x, y);
        float n10 = dotGridGradient(x1, y0, x, y);
        float n01 = dotGridGradient(x0, y1, x, y);
        float n11 = dotGridGradient(x1, y1, x, y);

        // Interpolate
        float ix0 = lerp(n00, n10, u);
        float ix1 = lerp(n01, n11, u);

        return lerp(ix0, ix1, v);
    }

    // Fractal Brownian Motion - multiple octaves for natural terrain
    static float fbm(float x, float y, int octaves = 6, float persistence = 0.5f, float lacunarity = 2.0f) {
        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            total += perlin(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return total / maxValue;  // Normalize to [-1, 1]
    }

    // Convenience: returns value in [0, 1] range
    static float fbmNormalized(float x, float y, int octaves = 6, float persistence = 0.5f) {
        return (fbm(x, y, octaves, persistence) + 1.0f) * 0.5f;
    }

private:
    static float fade(float t) {
        // 6t^5 - 15t^4 + 10t^3 (smoother than linear)
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    static float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }

    static float dotGridGradient(int ix, int iy, float x, float y) {
        // Get pseudorandom gradient
        glm::vec2 gradient = randomGradient(ix, iy);

        // Distance vector
        float dx = x - static_cast<float>(ix);
        float dy = y - static_cast<float>(iy);

        return dx * gradient.x + dy * gradient.y;
    }

    static glm::vec2 randomGradient(int ix, int iy) {
        // Hash function for deterministic random gradient
        const unsigned int w = 8 * sizeof(unsigned int);
        const unsigned int s = w / 2;
        unsigned int a = static_cast<unsigned int>(ix);
        unsigned int b = static_cast<unsigned int>(iy);

        a *= 3284157443u;
        b ^= a << s | a >> (w - s);
        b *= 1911520717u;
        a ^= b << s | b >> (w - s);
        a *= 2048419325u;

        float random = static_cast<float>(a) * (3.14159265f / static_cast<float>(~(~0u >> 1)));

        return {std::cos(random), std::sin(random)};
    }
};

} // namespace eden
