#pragma once

#include "ws/app/noise_generator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::gui {

enum class LayerBlendMode : std::uint8_t {
    Set = 0,
    Add = 1,
    Subtract = 2,
    Multiply = 3,
    Max = 4,
    Min = 5,
};

struct NoiseLayerConfig {
    std::string variableName;
    app::NoiseType noiseType = app::NoiseType::Perlin;
    float frequency = 2.0f;
    float amplitude = 1.0f;
    int octaves = 4;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    LayerBlendMode blendMode = LayerBlendMode::Set;
    bool invert = false;
    bool clampToDomain = false;
    float domainMin = 0.0f;
    float domainMax = 1.0f;
    bool useMask = false;
    std::string maskVariableName;
};

struct GenerationPreset {
    std::string id;
    std::string label;
    std::string description;
    std::vector<NoiseLayerConfig> layers;
};

class WorldGenerator {
public:
    static std::unordered_map<std::string, std::vector<float>> composeLayers(
        std::size_t width,
        std::size_t height,
        std::uint64_t seed,
        const std::vector<NoiseLayerConfig>& layers,
        const std::unordered_map<std::string, std::vector<float>>& maskInputs = {});

    static std::vector<GenerationPreset> presets();
};

} // namespace ws::gui
