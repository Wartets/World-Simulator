#include "ws/gui/render_rules.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ws::gui {

// Evaluates a render condition against a sample value.
// Supports comparison operators: greater, less, equal, range checks.
// @param cond Render condition with operator and threshold values
// @param sample Value to test against condition
// @return true if condition is satisfied
bool evaluateCondition(const RenderCondition& cond, const float sample) {
    switch (cond.op) {
        case RenderRuleOp::Greater:      return sample > cond.value;
        case RenderRuleOp::GreaterEqual: return sample >= cond.value;
        case RenderRuleOp::Less:         return sample < cond.value;
        case RenderRuleOp::LessEqual:    return sample <= cond.value;
        case RenderRuleOp::Equal:        return std::fabs(sample - cond.value) <= 1e-6f;
        case RenderRuleOp::NotEqual:     return std::fabs(sample - cond.value) > 1e-6f;
        case RenderRuleOp::InRange: {
            const float lo = std::min(cond.rangeMin, cond.rangeMax);
            const float hi = std::max(cond.rangeMin, cond.rangeMax);
            return sample >= lo && sample <= hi;
        }
        default:
            return false;
    }
}

// Applies blend mode between base and layer RGBA colors.
// Supports: replace, add, multiply, overlay, screen blending.
// @param base Base color array [R, G, B, A]
// @param layer Layer color array [R, G, B, A]
// @param mode Blend mode selector
// @return Blended color result
std::array<float, 4> applyBlend(const std::array<float, 4>& base,
                                const std::array<float, 4>& layer,
                                const RenderBlendMode mode) {
    std::array<float, 4> out = base;
    switch (mode) {
        case RenderBlendMode::Replace:
            out = layer;
            break;
        case RenderBlendMode::Add:
            for (int i = 0; i < 3; ++i) out[i] = std::clamp(base[i] + layer[i] * layer[3], 0.0f, 1.0f);
            out[3] = std::max(base[3], layer[3]);
            break;
        case RenderBlendMode::Multiply:
            for (int i = 0; i < 3; ++i) out[i] = std::clamp(base[i] * (1.0f - layer[3] + layer[i] * layer[3]), 0.0f, 1.0f);
            out[3] = std::max(base[3], layer[3]);
            break;
        case RenderBlendMode::Overlay:
            for (int i = 0; i < 3; ++i) {
                const float b = base[i];
                const float l = layer[i];
                const float o = (b < 0.5f) ? (2.0f * b * l) : (1.0f - 2.0f * (1.0f - b) * (1.0f - l));
                out[i] = std::clamp((1.0f - layer[3]) * b + layer[3] * o, 0.0f, 1.0f);
            }
            out[3] = std::max(base[3], layer[3]);
            break;
        case RenderBlendMode::Screen:
            for (int i = 0; i < 3; ++i) {
                const float s = 1.0f - (1.0f - base[i]) * (1.0f - layer[i]);
                out[i] = std::clamp((1.0f - layer[3]) * base[i] + layer[3] * s, 0.0f, 1.0f);
            }
            out[3] = std::max(base[3], layer[3]);
            break;
        default:
            break;
    }
    return out;
}

// Evaluates sequence of render rules against sample value.
// Rules are evaluated in order, each potentially modifying the color.
// @param rules Vector of render rules to apply
// @param sample Input sample value
// @param fallback Default color if no rules match
// @return Final RGBA color after all rules applied
std::array<float, 4> evaluateRules(
    const std::vector<RenderRule>& rules,
    const float sample,
    const std::array<float, 4>& fallback) {
    std::array<float, 4> color = fallback;
    for (const auto& rule : rules) {
        if (!rule.enabled) {
            continue;
        }
        if (evaluateCondition(rule.condition, sample)) {
            color = applyBlend(color, rule.rgba, rule.blend);
        }
    }
    return color;
}

// Saves render preset to text file in key-value format.
// @param preset Render preset to serialize
// @param filePath Output file path
// @param message Status message on success/failure
// @return true if preset saved successfully
bool saveRenderPreset(const RenderPreset& preset, const std::string& filePath, std::string& message) {
    std::ofstream out(filePath, std::ios::trunc);
    if (!out.is_open()) {
        message = "render_preset_save_failed reason=file_open";
        return false;
    }

    out << "name=" << preset.name << "\n";
    out << "rules=" << preset.rules.size() << "\n";
    for (std::size_t i = 0; i < preset.rules.size(); ++i) {
        const auto& rule = preset.rules[i];
        out << "rule." << i << ".enabled=" << rule.enabled << "\n";
        out << "rule." << i << ".variable=" << rule.condition.variable << "\n";
        out << "rule." << i << ".op=" << static_cast<int>(rule.condition.op) << "\n";
        out << "rule." << i << ".value=" << rule.condition.value << "\n";
        out << "rule." << i << ".rangeMin=" << rule.condition.rangeMin << "\n";
        out << "rule." << i << ".rangeMax=" << rule.condition.rangeMax << "\n";
        out << "rule." << i << ".rgba="
            << rule.rgba[0] << ',' << rule.rgba[1] << ',' << rule.rgba[2] << ',' << rule.rgba[3] << "\n";
        out << "rule." << i << ".blend=" << static_cast<int>(rule.blend) << "\n";
    }

    message = "render_preset_saved path=" + filePath;
    return true;
}

// Loads render preset from text file.
// @param filePath Input file path
// @param outPreset Output render preset
// @param message Status message on success/failure
// @return true if preset loaded successfully
bool loadRenderPreset(const std::string& filePath, RenderPreset& outPreset, std::string& message) {
    std::ifstream in(filePath);
    if (!in.is_open()) {
        message = "render_preset_load_failed reason=file_open";
        return false;
    }

    RenderPreset preset;
    std::size_t ruleCount = 0;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "name") {
            preset.name = value;
        } else if (key == "rules") {
            ruleCount = static_cast<std::size_t>(std::stoull(value));
            preset.rules.resize(ruleCount);
        } else if (key.rfind("rule.", 0) == 0) {
            const auto secondDot = key.find('.', 5);
            if (secondDot == std::string::npos) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(std::stoull(key.substr(5, secondDot - 5)));
            if (index >= preset.rules.size()) {
                continue;
            }
            const std::string field = key.substr(secondDot + 1);
            auto& rule = preset.rules[index];
            if (field == "enabled") rule.enabled = (std::stoi(value) != 0);
            else if (field == "variable") rule.condition.variable = value;
            else if (field == "op") rule.condition.op = static_cast<RenderRuleOp>(std::stoi(value));
            else if (field == "value") rule.condition.value = std::stof(value);
            else if (field == "rangeMin") rule.condition.rangeMin = std::stof(value);
            else if (field == "rangeMax") rule.condition.rangeMax = std::stof(value);
            else if (field == "rgba") {
                std::stringstream ss(value);
                std::string token;
                for (int i = 0; i < 4 && std::getline(ss, token, ','); ++i) {
                    rule.rgba[static_cast<std::size_t>(i)] = std::stof(token);
                }
            } else if (field == "blend") rule.blend = static_cast<RenderBlendMode>(std::stoi(value));
        }
    }

    outPreset = std::move(preset);
    message = "render_preset_loaded path=" + filePath;
    return true;
}

// Lists all render preset files in a directory.
// @param directoryPath Directory to scan
// @return Sorted vector of file paths
std::vector<std::string> listRenderPresetFiles(const std::string& directoryPath) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!std::filesystem::exists(directoryPath, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

} // namespace ws::gui
