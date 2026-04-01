#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ws::app {

enum class NoiseType : std::uint8_t {
    Perlin = 0,
    Simplex = 1,
    Worley = 2,
};

struct NoiseConfig {
    NoiseType type = NoiseType::Perlin;
    float frequency = 2.0f;
    float amplitude = 1.0f;
    int octaves = 4;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
};

class NoiseGenerator {
public:
    static float sample2D(float x, float y, std::uint64_t seed, const NoiseConfig& config);

    static std::vector<float> generate2D(
        std::size_t width,
        std::size_t height,
        std::uint64_t seed,
        const NoiseConfig& config);
};

} // namespace ws::app
