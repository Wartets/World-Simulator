#include "ws/core/random.hpp"

#include <cmath>
#include <algorithm>

namespace ws::random {

// FNV-1a hash for combining seeds
static constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ULL;
static constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;

std::uint64_t DeterministicRNG::hash(
    std::uint32_t x,
    std::uint32_t y,
    std::uint64_t step,
    std::uint64_t globalSeed) noexcept {
    std::uint64_t h = FNV_OFFSET;

    // Hash x coordinate
    h ^= static_cast<std::uint64_t>(x);
    h *= FNV_PRIME;

    // Hash y coordinate
    h ^= static_cast<std::uint64_t>(y);
    h *= FNV_PRIME;

    // Hash step
    h ^= step;
    h *= FNV_PRIME;

    // Hash global seed
    h ^= globalSeed;
    h *= FNV_PRIME;

    return h;
}

DeterministicRNG::DeterministicRNG(std::uint64_t globalSeed) noexcept
    : state_(0), globalSeed_(globalSeed), hasSpare_(false), spare_(0.0f) {}

void DeterministicRNG::seedCell(std::uint32_t x, std::uint32_t y, std::uint64_t step) noexcept {
    state_ = hash(x, y, step, globalSeed_);
    hasSpare_ = false;
}

std::uint32_t DeterministicRNG::next() noexcept {
    // PCG-XSH-RR variant
    const std::uint64_t state = state_;
    state_ = state * 6364136223846793005ULL + 1442695040888963407ULL;

    const std::uint32_t xorShifted = static_cast<std::uint32_t>(((state >> 18u) ^ state) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(state >> 59u);
    return (xorShifted >> rot) | (xorShifted << (32u - rot));
}

float DeterministicRNG::uniform() noexcept {
    const std::uint32_t u = next();
    // Map to [0, 1) by dividing by 2^32
    return static_cast<float>(u) / 4294967296.0f;
}

std::uint32_t DeterministicRNG::uniformInt(std::uint32_t minVal, std::uint32_t maxVal) noexcept {
    if (minVal == maxVal) {
        return minVal;
    }
    if (minVal > maxVal) {
        std::swap(minVal, maxVal);
    }
    const std::uint32_t range = maxVal - minVal + 1u;
    const std::uint32_t u = next();
    return minVal + (u % range);
}

float DeterministicRNG::gaussian(float mean, float stddev) noexcept {
    // Box-Muller transform: generates two Gaussian random numbers
    // We cache one and return it next time

    if (hasSpare_) {
        hasSpare_ = false;
        return spare_ * stddev + mean;
    }

    float u0, u1, rsq;
    do {
        u0 = 2.0f * uniform() - 1.0f;
        u1 = 2.0f * uniform() - 1.0f;
        rsq = u0 * u0 + u1 * u1;
    } while (rsq >= 1.0f || rsq == 0.0f);

    const float fac = std::sqrt(-2.0f * std::log(rsq) / rsq);
    spare_ = u1 * fac;
    hasSpare_ = true;

    return u0 * fac * stddev + mean;
}

DeterministicRNG CellRNGRegistry::getRNG(
    std::uint32_t x,
    std::uint32_t y,
    std::uint64_t step,
    std::uint64_t globalSeed) noexcept {
    DeterministicRNG rng(globalSeed);
    rng.seedCell(x, y, step);
    return rng;
}

namespace noise {

// Simple hash-based noise
static std::uint32_t hashNoise(std::uint32_t x, std::uint32_t y, std::uint64_t seed) noexcept {
    std::uint64_t h = seed;

    h ^= static_cast<std::uint64_t>(x);
    h *= 1099511628211ULL;

    h ^= static_cast<std::uint64_t>(y);
    h *= 1099511628211ULL;

    return static_cast<std::uint32_t>(h ^ (h >> 32u));
}

static float smoothstep(float t) noexcept {
    // Smoothstep function for interpolation: 3t^2 - 2t^3
    return t * t * (3.0f - 2.0f * t);
}

static float lerp(float a, float b, float t) noexcept {
    return a * (1.0f - t) + b * t;
}

float valueNoise2D(float x, float y, std::uint64_t seed) noexcept {
    const auto xi = static_cast<std::uint32_t>(std::floor(x));
    const auto yi = static_cast<std::uint32_t>(std::floor(y));
    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);

    // Sample four corners
    const float v00 = static_cast<float>(hashNoise(xi, yi, seed)) / 4294967296.0f;
    const float v10 = static_cast<float>(hashNoise(xi + 1, yi, seed)) / 4294967296.0f;
    const float v01 = static_cast<float>(hashNoise(xi, yi + 1, seed)) / 4294967296.0f;
    const float v11 = static_cast<float>(hashNoise(xi + 1, yi + 1, seed)) / 4294967296.0f;

    // Smoothly interpolate
    const float u = smoothstep(xf);
    const float v = smoothstep(yf);

    const float x0 = lerp(v00, v10, u);
    const float x1 = lerp(v01, v11, u);
    const float result = lerp(x0, x1, v);

    return result;
}

float perlin2D(float x, float y, std::uint64_t seed) noexcept {
    // Simplified Perlin-like noise using value noise
    // Octave the value noise for more natural appearance
    float result = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < 4; ++i) {
        result += amplitude * (2.0f * valueNoise2D(x * frequency, y * frequency, seed + i) - 1.0f);
        maxValue += amplitude;
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }

    return result / maxValue;
}

} // namespace noise

} // namespace ws::random
