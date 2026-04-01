#include "ws/gui/parameter_panel.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace ws::gui {

bool saveParameterPreset(const ParameterPreset& preset, const std::filesystem::path& outputPath, std::string& message) {
    std::error_code ec;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            message = "parameter_preset_save_failed reason=create_directory";
            return false;
        }
    }

    nlohmann::json root;
    root["name"] = preset.name;
    root["purpose"] = preset.purpose;
    root["date"] = preset.date;
    root["parameters"] = nlohmann::json::array();
    for (const auto& parameter : preset.parameters) {
        root["parameters"].push_back({
            {"name", parameter.name},
            {"target_variable", parameter.targetVariable},
            {"value", parameter.value},
            {"min", parameter.minValue},
            {"max", parameter.maxValue},
            {"default", parameter.defaultValue},
            {"units", parameter.units}});
    }

    std::ofstream out(outputPath, std::ios::trunc);
    if (!out.is_open()) {
        message = "parameter_preset_save_failed reason=file_open";
        return false;
    }

    out << root.dump(2);
    message = "parameter_preset_saved path=" + outputPath.string();
    return true;
}

bool loadParameterPreset(const std::filesystem::path& inputPath, ParameterPreset& preset, std::string& message) {
    std::ifstream in(inputPath);
    if (!in.is_open()) {
        message = "parameter_preset_load_failed reason=file_open";
        return false;
    }

    nlohmann::json root;
    in >> root;

    preset = ParameterPreset{};
    preset.name = root.value("name", std::string{});
    preset.purpose = root.value("purpose", std::string{});
    preset.date = root.value("date", std::string{});
    if (root.contains("parameters") && root["parameters"].is_array()) {
        for (const auto& item : root["parameters"]) {
            ParameterControl parameter;
            parameter.name = item.value("name", std::string{});
            parameter.targetVariable = item.value("target_variable", std::string{});
            parameter.value = item.value("value", 0.0f);
            parameter.minValue = item.value("min", -1.0f);
            parameter.maxValue = item.value("max", 1.0f);
            parameter.defaultValue = item.value("default", 0.0f);
            parameter.units = item.value("units", std::string{"1"});
            preset.parameters.push_back(std::move(parameter));
        }
    }

    message = "parameter_preset_loaded path=" + inputPath.string() + " count=" + std::to_string(preset.parameters.size());
    return true;
}

} // namespace ws::gui
