#pragma once

#include "ws/core/runtime.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui {

struct ParameterPreset {
    std::string name;
    std::string purpose;
    std::string date;
    std::vector<ParameterControl> parameters;
};

[[nodiscard]] bool saveParameterPreset(const ParameterPreset& preset, const std::filesystem::path& outputPath, std::string& message);
[[nodiscard]] bool loadParameterPreset(const std::filesystem::path& inputPath, ParameterPreset& preset, std::string& message);

} // namespace ws::gui
