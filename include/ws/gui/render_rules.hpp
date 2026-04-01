#pragma once

#include <array>
#include <string>
#include <vector>

namespace ws::gui {

enum class RenderRuleOp {
    Greater = 0,
    GreaterEqual,
    Less,
    LessEqual,
    Equal,
    NotEqual,
    InRange
};

enum class RenderBlendMode {
    Replace = 0,
    Add,
    Multiply,
    Overlay,
    Screen
};

struct RenderCondition {
    std::string variable;
    RenderRuleOp op = RenderRuleOp::Greater;
    float value = 0.0f;
    float rangeMin = 0.0f;
    float rangeMax = 0.0f;
};

struct RenderRule {
    bool enabled = true;
    RenderCondition condition{};
    std::array<float, 4> rgba{1.0f, 1.0f, 1.0f, 1.0f};
    RenderBlendMode blend = RenderBlendMode::Replace;
};

struct RenderPreset {
    std::string name;
    std::vector<RenderRule> rules;
};

[[nodiscard]] bool evaluateCondition(const RenderCondition& cond, float sample);
[[nodiscard]] std::array<float, 4> applyBlend(const std::array<float, 4>& base,
                                              const std::array<float, 4>& layer,
                                              RenderBlendMode mode);

[[nodiscard]] std::array<float, 4> evaluateRules(
    const std::vector<RenderRule>& rules,
    float sample,
    const std::array<float, 4>& fallback);

[[nodiscard]] bool saveRenderPreset(const RenderPreset& preset, const std::string& filePath, std::string& message);
[[nodiscard]] bool loadRenderPreset(const std::string& filePath, RenderPreset& outPreset, std::string& message);
[[nodiscard]] std::vector<std::string> listRenderPresetFiles(const std::string& directoryPath);

} // namespace ws::gui
