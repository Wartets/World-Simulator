#pragma once

#include "ws/core/runtime.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Parameter Preset
// =============================================================================

// A saved set of parameter controls with metadata.
struct ParameterPreset {
    std::string name;                              // Display name of the preset.
    std::string purpose;                           // Description of the preset's purpose.
    std::string date;                              // Date when the preset was created.
    std::vector<ParameterControl> parameters;      // List of parameter controls.
};

// Saves a parameter preset to a file.
[[nodiscard]] bool saveParameterPreset(const ParameterPreset& preset, const std::filesystem::path& outputPath, std::string& message);
// Loads a parameter preset from a file.
[[nodiscard]] bool loadParameterPreset(const std::filesystem::path& inputPath, ParameterPreset& preset, std::string& message);

} // namespace ws::gui
