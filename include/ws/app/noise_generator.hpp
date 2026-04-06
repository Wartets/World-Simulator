#pragma once

// Standard library
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ws::app {

// =============================================================================
// Noise Type
// =============================================================================

// Available noise generation algorithms.
enum class NoiseType : std::uint8_t {
    Perlin = 0,    // Perlin noise algorithm.
    Simplex = 1,   // Simplex noise algorithm.
    Worley = 2,    // Worley (cellular) noise algorithm.
};

// =============================================================================
// Noise Config
// =============================================================================

// Configuration parameters for noise generation.
struct NoiseConfig {
    NoiseType type = NoiseType::Perlin;   // Type of noise to generate.
    float frequency = 2.0f;               // Base frequency of the noise.
    float amplitude = 1.0f;               // Base amplitude of the noise.
    int octaves = 4;                      // Number of octaves for fractal noise.
    float persistence = 0.5f;             // Amplitude decay per octave.
    float lacunarity = 2.0f;              // Frequency multiplier per octave.
};

// =============================================================================
// Noise Generator
// =============================================================================

// Generates procedural noise for initial conditions.
class NoiseGenerator {
public:
    // Samples a single noise value at 2D coordinates.
    static float sample2D(float x, float y, std::uint64_t seed, const NoiseConfig& config);

    // Generates a 2D grid of noise values.
    static std::vector<float> generate2D(
        std::size_t width,
        std::size_t height,
        std::uint64_t seed,
        const NoiseConfig& config);
};

} // namespace ws::app
