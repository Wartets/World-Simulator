#include "ws/app/noise_generator.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// Helper for assertions in tests.
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + message); \
        } \
    } while (0)

#define TEST_EQUAL(a, b, message) \
    TEST_ASSERT((a) == (b), message)

#define TEST_NOT_EQUAL(a, b, message) \
    TEST_ASSERT((a) != (b), message)

#define TEST_GREATER(a, b, message) \
    TEST_ASSERT((a) > (b), message)

#define TEST_LESS(a, b, message) \
    TEST_ASSERT((a) < (b), message)

#define TEST_GREATER_EQUAL(a, b, message) \
    TEST_ASSERT((a) >= (b), message)

#define TEST_LESS_EQUAL(a, b, message) \
    TEST_ASSERT((a) <= (b), message)

using namespace ws::app;

// Test that Wavelet noise can be sampled and produces values in [-1, 1].
void testWaveletNoiseGeneration() {
    const NoiseConfig config{
        .type = NoiseType::Wavelet,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 4,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    const float sample1 = NoiseGenerator::sample2D(0.5f, 0.5f, 12345ULL, config);
    const float sample2 = NoiseGenerator::sample2D(0.6f, 0.5f, 12345ULL, config);

    TEST_GREATER_EQUAL(sample1, -1.0f, "Sample1 out of range (low)");
    TEST_LESS_EQUAL(sample1, 1.0f, "Sample1 out of range (high)");
    TEST_GREATER_EQUAL(sample2, -1.0f, "Sample2 out of range (low)");
    TEST_LESS_EQUAL(sample2, 1.0f, "Sample2 out of range (high)");
    TEST_NOT_EQUAL(sample1, sample2, "Different coordinates should produce different values");
}

// Test that identical seed and coordinates produce identical output.
void testWaveletNoiseDeterminism() {
    const NoiseConfig config{
        .type = NoiseType::Wavelet,
        .frequency = 1.5f,
        .amplitude = 0.8f,
        .octaves = 3,
        .persistence = 0.6f,
        .lacunarity = 2.2f,
    };

    const float sample1 = NoiseGenerator::sample2D(0.3f, 0.7f, 54321ULL, config);
    const float sample2 = NoiseGenerator::sample2D(0.3f, 0.7f, 54321ULL, config);

    TEST_EQUAL(sample1, sample2, "Identical inputs should produce identical output");
}

// Test that a 2D grid of Wavelet noise is generated correctly.
void testWaveletNoiseGridGeneration() {
    const NoiseConfig config{
        .type = NoiseType::Wavelet,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 4,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    const auto grid = NoiseGenerator::generate2D(16, 16, 99999ULL, config);

    TEST_EQUAL(grid.size(), 256UL, "Grid should have 256 values");
    for (size_t i = 0; i < grid.size(); ++i) {
        const float value = grid[i];
        TEST_GREATER_EQUAL(value, -1.0f, "Grid value out of range (low)");
        TEST_LESS_EQUAL(value, 1.0f, "Grid value out of range (high)");
    }
}

// Test that different parameters produce different results.
void testWaveletNoiseParameterSensitivity() {
    const float x = 0.4f;
    const float y = 0.6f;
    const std::uint64_t seed = 11111ULL;

    NoiseConfig baseConfig{
        .type = NoiseType::Wavelet,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 4,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    const float baseSample = NoiseGenerator::sample2D(x, y, seed, baseConfig);

    // Different frequency should produce different result.
    baseConfig.frequency = 3.0f;
    const float freqSample = NoiseGenerator::sample2D(x, y, seed, baseConfig);
    TEST_NOT_EQUAL(baseSample, freqSample, "Frequency change should affect result");

    // Reset and verify zero amplitude disables the signal.
    baseConfig.frequency = 2.0f;
    baseConfig.amplitude = 0.0f;
    const float ampSample = NoiseGenerator::sample2D(x, y, seed, baseConfig);
    TEST_EQUAL(ampSample, 0.0f, "Zero amplitude should produce zero output");

    // Reset and test octave sensitivity.
    baseConfig.amplitude = 1.0f;
    baseConfig.octaves = 2;
    const float octaveSample = NoiseGenerator::sample2D(x, y, seed, baseConfig);
    TEST_NOT_EQUAL(baseSample, octaveSample, "Octave change should affect result");
}

// Test that different seeds produce different noise fields.
void testWaveletNoiseDifferentSeeds() {
    const NoiseConfig config{
        .type = NoiseType::Wavelet,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 4,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    const float sample1 = NoiseGenerator::sample2D(0.5f, 0.5f, 11111ULL, config);
    const float sample2 = NoiseGenerator::sample2D(0.5f, 0.5f, 22222ULL, config);

    TEST_NOT_EQUAL(sample1, sample2, "Different seeds should generally produce different values");
}

// Test that Wavelet noise produces distinct output from Perlin/Simplex/Worley.
void testWaveletNoiseVsOtherTypes() {
    const float x = 0.5f;
    const float y = 0.5f;
    const std::uint64_t seed = 33333ULL;

    NoiseConfig config{
        .type = NoiseType::Perlin,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 4,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    const float perlinSample = NoiseGenerator::sample2D(x, y, seed, config);

    config.type = NoiseType::Wavelet;
    const float waveletSample = NoiseGenerator::sample2D(x, y, seed, config);

    TEST_NOT_EQUAL(perlinSample, waveletSample, "Wavelet should differ from Perlin at same coordinates/seed");
}

// Test that all NoiseType enum values can be sampled without errors.
void testAllNoiseTypesSupported() {
    const float x = 0.5f;
    const float y = 0.5f;
    const std::uint64_t seed = 44444ULL;

    NoiseConfig config{
        .type = NoiseType::Perlin,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 3,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    std::vector<NoiseType> types = {
        NoiseType::Perlin,
        NoiseType::Simplex,
        NoiseType::Worley,
        NoiseType::Wavelet,
    };

    for (const auto type : types) {
        config.type = type;
        const float sample = NoiseGenerator::sample2D(x, y, seed, config);
        TEST_GREATER_EQUAL(sample, -1.0f, "Type out of range (low)");
        TEST_LESS_EQUAL(sample, 1.0f, "Type out of range (high)");
    }
}

// Test that the generated grid has meaningful variation (not all same values).
void testWaveletNoiseGridVariation() {
    const NoiseConfig config{
        .type = NoiseType::Wavelet,
        .frequency = 2.0f,
        .amplitude = 1.0f,
        .octaves = 4,
        .persistence = 0.5f,
        .lacunarity = 2.0f,
    };

    const auto grid = NoiseGenerator::generate2D(32, 32, 55555ULL, config);

    std::unordered_set<float> uniqueValues(grid.begin(), grid.end());
    // With floating point noise, we expect significant variation.
    // At minimum, we should have more than 10 distinct values in a 32x32 grid.
    TEST_GREATER(uniqueValues.size(), 10UL, "Grid should have many unique values");

    float minVal = *std::min_element(grid.begin(), grid.end());
    float maxVal = *std::max_element(grid.begin(), grid.end());

    // Range should be meaningful (not just a tiny band).
    TEST_GREATER(maxVal - minVal, 0.5f, "Grid range should be meaningful");
}

} // namespace

// Entry point for tests.
int main() {
    try {
        testWaveletNoiseGeneration();
        testWaveletNoiseDeterminism();
        testWaveletNoiseGridGeneration();
        testWaveletNoiseParameterSensitivity();
        testWaveletNoiseDifferentSeeds();
        testWaveletNoiseVsOtherTypes();
        testAllNoiseTypesSupported();
        testWaveletNoiseGridVariation();

        std::cout << "All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
