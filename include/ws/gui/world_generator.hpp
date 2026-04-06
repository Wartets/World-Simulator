#pragma once

#include "ws/app/noise_generator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::gui {

// =============================================================================
// Layer Blend Mode
// =============================================================================

// Mode for blending noise layers together.
enum class LayerBlendMode : std::uint8_t {
    Set = 0,       // Replace existing values.
    Add = 1,       // Add to existing values.
    Subtract = 2,  // Subtract from existing values.
    Multiply = 3, // Multiply with existing values.
    Max = 4,       // Maximum of existing and new.
    Min = 5        // Minimum of existing and new.
};

// =============================================================================
// Noise Layer Config
// =============================================================================

// Configuration for a single noise layer in world generation.
struct NoiseLayerConfig {
    std::string variableName;                    // Name of the output variable.
    app::NoiseType noiseType = app::NoiseType::Perlin;  // Type of noise algorithm.
    float frequency = 2.0f;                      // Base frequency of noise.
    float amplitude = 1.0f;                      // Base amplitude.
    int octaves = 4;                             // Number of octaves for fractal noise.
    float persistence = 0.5f;                    // Amplitude decay per octave.
    float lacunarity = 2.0f;                     // Frequency multiplier per octave.
    LayerBlendMode blendMode = LayerBlendMode::Set;  // How to blend with previous layers.
    bool invert = false;                         // Invert the noise values.
    bool clampToDomain = false;                 // Clamp output to domain range.
    float domainMin = 0.0f;                     // Minimum domain value.
    float domainMax = 1.0f;                     // Maximum domain value.
    bool useMask = false;                       // Use a mask layer.
    std::string maskVariableName;                // Name of mask variable.
};

// =============================================================================
// Generation Preset
// =============================================================================

// A preset configuration for world generation.
struct GenerationPreset {
    std::string id;                              // Unique identifier.
    std::string label;                           // Display label.
    std::string description;                     // Description of the preset.
    std::vector<NoiseLayerConfig> layers;       // List of noise layers.
};

// =============================================================================
// World Generator
// =============================================================================

// Generates procedural worlds from noise layer configurations.
class WorldGenerator {
public:
    // Composes multiple noise layers into a world.
    static std::unordered_map<std::string, std::vector<float>> composeLayers(
        std::size_t width,
        std::size_t height,
        std::uint64_t seed,
        const std::vector<NoiseLayerConfig>& layers,
        const std::unordered_map<std::string, std::vector<float>>& maskInputs = {});

    // Returns available generation presets.
    static std::vector<GenerationPreset> presets();
};

} // namespace ws::gui
