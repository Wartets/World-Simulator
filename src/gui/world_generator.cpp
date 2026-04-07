#include "ws/gui/world_generator.hpp"

#include <algorithm>
#include <cmath>

namespace ws::gui {
namespace {

// Applies blend mode between base and layer values.
// @param mode Layer blend mode (add, subtract, multiply, max, min, set)
// @param baseValue Base value to blend onto
// @param layerValue Layer value to blend
// @return Blended result
float applyBlend(const LayerBlendMode mode, const float baseValue, const float layerValue) {
    switch (mode) {
        case LayerBlendMode::Add:
            return baseValue + layerValue;
        case LayerBlendMode::Subtract:
            return baseValue - layerValue;
        case LayerBlendMode::Multiply:
            return baseValue * layerValue;
        case LayerBlendMode::Max:
            return std::max(baseValue, layerValue);
        case LayerBlendMode::Min:
            return std::min(baseValue, layerValue);
        case LayerBlendMode::Set:
        default:
            return layerValue;
    }
}

// Converts normalized noise output [-1, 1] to unit range [0, 1].
// @param v Normalized noise value
// @return Unit-range value clamped to [0, 1]
float normalizedNoiseToUnit(const float v) {
    return std::clamp((v + 1.0f) * 0.5f, 0.0f, 1.0f);
}

} // namespace

// Composes multiple noise layers into output variable maps.
// Generates noise for each layer, optionally applies mask, blends to target.
// @param width Grid width
// @param height Grid height
// @param seed Random seed for noise generation
// @param layers Layer configurations with noise parameters
// @param maskInputs Optional mask inputs for layer masking
// @return Map of variable names to generated value vectors
std::unordered_map<std::string, std::vector<float>> WorldGenerator::composeLayers(
    const std::size_t width,
    const std::size_t height,
    const std::uint64_t seed,
    const std::vector<NoiseLayerConfig>& layers,
    const std::unordered_map<std::string, std::vector<float>>& maskInputs) {

    std::unordered_map<std::string, std::vector<float>> outputs;
    const std::size_t size = width * height;
    if (size == 0) {
        return outputs;
    }

    for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
        const auto& layer = layers[layerIndex];
        if (layer.variableName.empty()) {
            continue;
        }

        auto& target = outputs[layer.variableName];
        if (target.empty()) {
            target.assign(size, 0.0f);
        }

        app::NoiseConfig noiseConfig;
        noiseConfig.type = layer.noiseType;
        noiseConfig.frequency = layer.frequency;
        noiseConfig.amplitude = layer.amplitude;
        noiseConfig.octaves = layer.octaves;
        noiseConfig.persistence = layer.persistence;
        noiseConfig.lacunarity = layer.lacunarity;

        const auto noise = app::NoiseGenerator::generate2D(
            width,
            height,
            seed ^ (static_cast<std::uint64_t>(layerIndex) * 0x9e3779b185ebca87ULL),
            noiseConfig);

        const std::vector<float>* mask = nullptr;
        if (layer.useMask) {
            const auto maskIt = maskInputs.find(layer.maskVariableName);
            if (maskIt != maskInputs.end() && maskIt->second.size() == size) {
                mask = &maskIt->second;
            }
        }

        for (std::size_t i = 0; i < size; ++i) {
            float sample = normalizedNoiseToUnit(noise[i]);
            if (layer.invert) {
                sample = 1.0f - sample;
            }
            if (mask != nullptr) {
                sample *= std::clamp((*mask)[i], 0.0f, 1.0f);
            }

            float value = applyBlend(layer.blendMode, target[i], sample);
            if (layer.clampToDomain) {
                const float lo = std::min(layer.domainMin, layer.domainMax);
                const float hi = std::max(layer.domainMin, layer.domainMax);
                value = std::clamp(value, lo, hi);
            }
            target[i] = value;
        }
    }

    return outputs;
}

// Returns predefined world generation presets.
// Includes flat, mountainous, island, and random configurations.
// @return Vector of generation presets with metadata
std::vector<GenerationPreset> WorldGenerator::presets() {
    return {
        {
            "flat",
            "Flat",
            "Low-variance terrain for controlled debugging scenarios.",
            {
                {"terrain_elevation", app::NoiseType::Perlin, 0.6f, 0.25f, 2, 0.45f, 2.0f, LayerBlendMode::Set, false, true, 0.0f, 1.0f, false, ""}
            }
        },
        {
            "mountainous",
            "Mountainous",
            "High-frequency ridges and contrast-heavy relief.",
            {
                {"terrain_elevation", app::NoiseType::Perlin, 2.4f, 1.0f, 6, 0.56f, 2.1f, LayerBlendMode::Set, false, true, 0.0f, 1.0f, false, ""},
                {"terrain_elevation", app::NoiseType::Worley, 1.5f, 0.35f, 3, 0.5f, 2.0f, LayerBlendMode::Add, false, true, 0.0f, 1.0f, false, ""}
            }
        },
        {
            "island",
            "Island",
            "Ocean-dominant setup with masked central highlands.",
            {
                {"terrain_elevation", app::NoiseType::Simplex, 1.2f, 0.95f, 5, 0.52f, 2.0f, LayerBlendMode::Set, false, true, 0.0f, 1.0f, false, ""},
                {"terrain_elevation", app::NoiseType::Worley, 0.8f, 0.25f, 2, 0.45f, 2.0f, LayerBlendMode::Subtract, false, true, 0.0f, 1.0f, false, ""}
            }
        },
        {
            "random",
            "Random",
            "Balanced mixed noise useful for exploratory starts.",
            {
                {"terrain_elevation", app::NoiseType::Perlin, 1.6f, 0.7f, 5, 0.5f, 2.0f, LayerBlendMode::Set, false, true, 0.0f, 1.0f, false, ""},
                {"humidity", app::NoiseType::Simplex, 2.0f, 0.8f, 4, 0.55f, 2.3f, LayerBlendMode::Set, false, true, 0.0f, 1.0f, false, ""}
            }
        }
    };
}

} // namespace ws::gui
