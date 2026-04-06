#pragma once

#include <cstdint>

namespace ws::random {

// =============================================================================
// Deterministic RNG
// =============================================================================

// PCG-based deterministic random number generator.
// Provides reproducible random numbers seeded by cell position + global seed.
class DeterministicRNG {
public:
    // Constructor with global seed.
    explicit DeterministicRNG(std::uint64_t globalSeed) noexcept;

    // Seeds based on cell position (x, y, step) and global seed.
    // This ensures each cell at each timestep gets reproducible randomness.
    void seedCell(
        std::uint32_t x,
        std::uint32_t y,
        std::uint64_t step) noexcept;

    // Generates a uniform random float in [0, 1).
    [[nodiscard]] float uniform() noexcept;

    // Generates a uniform random integer in [min, max].
    [[nodiscard]] std::uint32_t uniformInt(
        std::uint32_t minVal,
        std::uint32_t maxVal) noexcept;

    // Generates a Gaussian (normal) random float with given mean and stddev.
    [[nodiscard]] float gaussian(float mean = 0.0f, float stddev = 1.0f) noexcept;

    // Returns current RNG state (for testing/debugging).
    [[nodiscard]] std::uint64_t state() const noexcept { return state_; }

private:
    std::uint64_t state_;
    std::uint64_t globalSeed_;
    bool hasSpare_ = false;
    float spare_ = 0.0f;

    // PCG advance and output.
    [[nodiscard]] std::uint32_t next() noexcept;

    // Hash function for seeding.
    [[nodiscard]] static std::uint64_t hash(
        std::uint32_t x,
        std::uint32_t y,
        std::uint64_t step,
        std::uint64_t globalSeed) noexcept;
};

// =============================================================================
// Cell RNG Registry
// =============================================================================

// Global registry of per-cell RNG states for multi-threaded access.
// (Optional scaffolding for future multithreading).
class CellRNGRegistry {
public:
    CellRNGRegistry() = delete;

    // Gets a deterministic RNG for a specific cell position and timestep.
    static DeterministicRNG getRNG(
        std::uint32_t x,
        std::uint32_t y,
        std::uint64_t step,
        std::uint64_t globalSeed) noexcept;
};

// =============================================================================
// Noise Utilities
// =============================================================================

namespace noise {

// Perlin-like noise (deterministic, repeatable).
// Returns value in [-1, 1] based on position and seed.
[[nodiscard]] float perlin2D(
    float x,
    float y,
    std::uint64_t seed) noexcept;

// Value noise (simpler, faster).
// Returns value in [0, 1].
[[nodiscard]] float valueNoise2D(
    float x,
    float y,
    std::uint64_t seed) noexcept;

} // namespace noise

} // namespace ws::random
