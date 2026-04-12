#include "ws/app/noise_generator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ws::app {
namespace {

// 2D gradient vectors for Perlin/Simplex noise.
constexpr std::array<int, 8> kGrad2X = {1, -1, 1, -1, 1, -1, 0, 0};
constexpr std::array<int, 8> kGrad2Y = {1, 1, -1, -1, 0, 0, 1, -1};

// 64-bit hash mixing using FNV-1a variant.
inline std::uint64_t mix64(std::uint64_t v) {
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return v;
}

// Hashes 2D coordinates with seed using splitmix64-like mixing.
inline std::uint64_t hash2(std::int64_t x, std::int64_t y, std::uint64_t seed) {
    const auto ux = static_cast<std::uint64_t>(x) * 0x9e3779b185ebca87ULL;
    const auto uy = static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL;
    return mix64(seed ^ ux ^ (uy << 1));
}

// Converts 64-bit integer to float in [0, 1] range.
inline float toUnitFloat(std::uint64_t v) {
    constexpr double kScale = 1.0 / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return static_cast<float>(static_cast<double>(v) * kScale);
}

// Perlin fade function: 6t^5 - 15t^4 + 10t^3.
// Smooths interpolation between grid points.
inline float fade(const float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Linear interpolation between a and b by t.
inline float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

// Computes dot product of gradient vector with offset.
float gradDot(const std::uint64_t h, const float x, const float y) {
    const std::size_t idx = static_cast<std::size_t>(h & 7ULL);
    return static_cast<float>(kGrad2X[idx]) * x + static_cast<float>(kGrad2Y[idx]) * y;
}

// Samples value noise at (x, y) with given seed.
// Uses bilinear interpolation of hash values at grid corners.
float sampleValueNoise(const float x, const float y, const std::uint64_t seed) {
    const auto x0 = static_cast<std::int64_t>(std::floor(x));
    const auto y0 = static_cast<std::int64_t>(std::floor(y));
    const auto x1 = x0 + 1;
    const auto y1 = y0 + 1;

    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float u = fade(tx);
    const float v = fade(ty);

    const float n00 = (toUnitFloat(hash2(x0, y0, seed)) * 2.0f) - 1.0f;
    const float n10 = (toUnitFloat(hash2(x1, y0, seed)) * 2.0f) - 1.0f;
    const float n01 = (toUnitFloat(hash2(x0, y1, seed)) * 2.0f) - 1.0f;
    const float n11 = (toUnitFloat(hash2(x1, y1, seed)) * 2.0f) - 1.0f;

    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
}

// Samples classic Perlin noise at (x, y).
// Uses gradient interpolation rather than value interpolation.
float samplePerlin(const float x, const float y, const std::uint64_t seed) {
    const auto x0 = static_cast<std::int64_t>(std::floor(x));
    const auto y0 = static_cast<std::int64_t>(std::floor(y));
    const auto x1 = x0 + 1;
    const auto y1 = y0 + 1;

    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float u = fade(tx);
    const float v = fade(ty);

    const float g00 = gradDot(hash2(x0, y0, seed), tx, ty);
    const float g10 = gradDot(hash2(x1, y0, seed), tx - 1.0f, ty);
    const float g01 = gradDot(hash2(x0, y1, seed), tx, ty - 1.0f);
    const float g11 = gradDot(hash2(x1, y1, seed), tx - 1.0f, ty - 1.0f);

    return lerp(lerp(g00, g10, u), lerp(g01, g11, u), v) * 0.70710678f;
}

// Samples simplified Simplex noise approximation.
// Uses skewed grid transformation and radial falloff.
float sampleSimplexApprox(const float x, const float y, const std::uint64_t seed) {
    constexpr float kF2 = 0.366025403784f;
    constexpr float kG2 = 0.211324865405f;

    const float s = (x + y) * kF2;
    const float xs = x + s;
    const float ys = y + s;

    const auto i = static_cast<std::int64_t>(std::floor(xs));
    const auto j = static_cast<std::int64_t>(std::floor(ys));

    const float t = static_cast<float>(i + j) * kG2;
    const float X0 = static_cast<float>(i) - t;
    const float Y0 = static_cast<float>(j) - t;
    const float x0 = x - X0;
    const float y0 = y - Y0;

    const int i1 = x0 > y0 ? 1 : 0;
    const int j1 = x0 > y0 ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + kG2;
    const float y1 = y0 - static_cast<float>(j1) + kG2;
    const float x2 = x0 - 1.0f + 2.0f * kG2;
    const float y2 = y0 - 1.0f + 2.0f * kG2;

    const auto n = [&](const float xr, const float yr, const std::int64_t ii, const std::int64_t jj) {
        float t2 = 0.5f - xr * xr - yr * yr;
        if (t2 <= 0.0f) {
            return 0.0f;
        }
        t2 *= t2;
        const float g = gradDot(hash2(ii, jj, seed), xr, yr);
        return t2 * t2 * g;
    };

    const float n0 = n(x0, y0, i, j);
    const float n1 = n(x1, y1, i + i1, j + j1);
    const float n2 = n(x2, y2, i + 1, j + 1);
    return 45.23065f * (n0 + n1 + n2);
}

// Samples Worley (cellular/Voronoi) noise.
// Returns distance to nearest feature point, normalized to [-1, 1].
float sampleWorley(const float x, const float y, const std::uint64_t seed) {
    const auto baseX = static_cast<std::int64_t>(std::floor(x));
    const auto baseY = static_cast<std::int64_t>(std::floor(y));

    float bestDistance = std::numeric_limits<float>::infinity();
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const auto cx = baseX + ox;
            const auto cy = baseY + oy;
            const std::uint64_t h = hash2(cx, cy, seed);
            const float jitterX = toUnitFloat(h);
            const float jitterY = toUnitFloat(mix64(h ^ 0x517cc1b727220a95ULL));

            const float px = static_cast<float>(cx) + jitterX;
            const float py = static_cast<float>(cy) + jitterY;
            const float dx = px - x;
            const float dy = py - y;
            const float d2 = dx * dx + dy * dy;
            bestDistance = std::min(bestDistance, d2);
        }
    }

    const float d = std::sqrt(std::max(0.0f, bestDistance));
    const float normalized = std::clamp(d / 1.41421356f, 0.0f, 1.0f);
    return (1.0f - normalized) * 2.0f - 1.0f;
}

// Samples wavelet noise at (x, y).
// Uses randomized basis functions at multiple scales for smooth variation.
// Adapted from Perlin's wavelet noise approach.
float sampleWavelet(const float x, const float y, const std::uint64_t seed) {
    constexpr int kTileSize = 256;
    
    float result = 0.0f;
    
    // Sample from multiple scales (coarse to fine).
    // Each scale contributes scaled basis functions.
    for (int scale = 0; scale < 3; ++scale) {
        const float scaleFactor = std::pow(2.0f, static_cast<float>(scale));
        const float sx = x * scaleFactor;
        const float sy = y * scaleFactor;
        
        const auto ix0 = static_cast<std::int64_t>(std::floor(sx));
        const auto iy0 = static_cast<std::int64_t>(std::floor(sy));
        const auto ix1 = ix0 + 1;
        const auto iy1 = iy0 + 1;
        
        const float fx = sx - static_cast<float>(ix0);
        const float fy = sy - static_cast<float>(iy0);
        
        // Use fade function for smooth interpolation.
        const float u = fade(fx);
        const float v = fade(fy);
        
        // Sample four corners with per-scale seed variation.
        const std::uint64_t scaleSeed = mix64(seed ^ static_cast<std::uint64_t>(scale) * 0x85ebca6b);
        
        const float n00 = (toUnitFloat(hash2(ix0, iy0, scaleSeed)) * 2.0f) - 1.0f;
        const float n10 = (toUnitFloat(hash2(ix1, iy0, scaleSeed)) * 2.0f) - 1.0f;
        const float n01 = (toUnitFloat(hash2(ix0, iy1, scaleSeed)) * 2.0f) - 1.0f;
        const float n11 = (toUnitFloat(hash2(ix1, iy1, scaleSeed)) * 2.0f) - 1.0f;
        
        // Bilinear interpolation.
        const float nx0 = lerp(n00, n10, u);
        const float nx1 = lerp(n01, n11, u);
        const float nxy = lerp(nx0, nx1, v);
        
        // Accumulate with decreasing amplitude per scale.
        const float amplitude = 1.0f / (1.0f + static_cast<float>(scale));
        result += nxy * amplitude;
    }
    
    // Normalize to [-1, 1] range.
    return std::clamp(result / 1.833f, -1.0f, 1.0f);
}

// Dispatches to the appropriate noise sampling function based on type.
float sampleOne(const NoiseType type, const float x, const float y, const std::uint64_t seed) {
    switch (type) {
        case NoiseType::Simplex:
            return sampleSimplexApprox(x, y, seed);
        case NoiseType::Worley:
            return sampleWorley(x, y, seed);
        case NoiseType::Wavelet:
            return sampleWavelet(x, y, seed);
        case NoiseType::Perlin:
        default:
            return samplePerlin(x, y, seed);
    }
}

} // namespace

// Samples 2D noise with fractal Brownian motion (multiple octaves).
// Combines multiple frequency layers with decreasing amplitude.
float NoiseGenerator::sample2D(const float x, const float y, const std::uint64_t seed, const NoiseConfig& config) {
    const int octaveCount = std::clamp(config.octaves, 1, 16);
    float amplitude = std::max(0.0f, config.amplitude);
    float frequency = std::max(0.0001f, config.frequency);
    const float persistence = std::clamp(config.persistence, 0.0f, 1.5f);
    const float lacunarity = std::max(1.0f, config.lacunarity);

    float total = 0.0f;
    float weight = 0.0f;

    for (int octave = 0; octave < octaveCount; ++octave) {
        const std::uint64_t octaveSeed = mix64(seed ^ static_cast<std::uint64_t>(octave) * 0x9e3779b185ebca87ULL);
        const float sample = sampleOne(config.type, x * frequency, y * frequency, octaveSeed);
        total += sample * amplitude;
        weight += amplitude;

        frequency *= lacunarity;
        amplitude *= persistence;
    }

    if (weight <= 1e-6f) {
        return 0.0f;
    }
    return std::clamp(total / weight, -1.0f, 1.0f);
}

// Generates a 2D noise field of specified dimensions.
// Returns flat vector representing the grid (row-major order).
std::vector<float> NoiseGenerator::generate2D(
    const std::size_t width,
    const std::size_t height,
    const std::uint64_t seed,
    const NoiseConfig& config) {

    std::vector<float> output(width * height, 0.0f);
    if (width == 0 || height == 0) {
        return output;
    }

    const float invW = 1.0f / static_cast<float>(width);
    const float invH = 1.0f / static_cast<float>(height);

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const float nx = static_cast<float>(x) * invW;
            const float ny = static_cast<float>(y) * invH;
            output[y * width + x] = sample2D(nx, ny, seed, config);
        }
    }

    return output;
}

} // namespace ws::app
