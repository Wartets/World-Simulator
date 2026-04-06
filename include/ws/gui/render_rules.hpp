#pragma once

#include <array>
#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Render Rule Operator
// =============================================================================

// Comparison operator for render rule conditions.
enum class RenderRuleOp {
    Greater = 0,  // a > value
    GreaterEqual, // a >= value
    Less,         // a < value
    LessEqual,    // a <= value
    Equal,        // a == value
    NotEqual,     // a != value
    InRange       // value in [rangeMin, rangeMax]
};

// =============================================================================
// Render Blend Mode
// =============================================================================

// Blending mode for combining render rules.
enum class RenderBlendMode {
    Replace = 0,    // Replace the base color entirely.
    Add,            // Add layer to base (additive).
    Multiply,       // Multiply layer with base.
    Overlay,        // Overlay blend.
    Screen          // Screen blend.
};

// =============================================================================
// Render Condition
// =============================================================================

// Condition for triggering a render rule.
struct RenderCondition {
    std::string variable;           // Variable name to evaluate.
    RenderRuleOp op = RenderRuleOp::Greater;  // Comparison operator.
    float value = 0.0f;             // Threshold value for comparison.
    float rangeMin = 0.0f;          // Minimum for InRange operation.
    float rangeMax = 0.0f;          // Maximum for InRange operation.
};

// =============================================================================
// Render Rule
// =============================================================================

// A single render rule with condition and styling.
struct RenderRule {
    bool enabled = true;                     // Whether the rule is active.
    RenderCondition condition{};             // Condition that triggers this rule.
    std::array<float, 4> rgba{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA color.
    RenderBlendMode blend = RenderBlendMode::Replace;  // Blend mode.
};

// =============================================================================
// Render Preset
// =============================================================================

// A collection of render rules forming a preset.
struct RenderPreset {
    std::string name;              // Preset name.
    std::vector<RenderRule> rules; // List of render rules.
};

// Evaluates a render condition against a sample value.
[[nodiscard]] bool evaluateCondition(const RenderCondition& cond, float sample);
// Applies a blend mode between two RGBA colors.
[[nodiscard]] std::array<float, 4> applyBlend(const std::array<float, 4>& base, const std::array<float, 4>& layer, RenderBlendMode mode);

// Evaluates a list of rules to determine final RGBA color.
[[nodiscard]] std::array<float, 4> evaluateRules(
    const std::vector<RenderRule>& rules,
    float sample,
    const std::array<float, 4>& fallback);

// Saves a render preset to a file.
[[nodiscard]] bool saveRenderPreset(const RenderPreset& preset, const std::string& filePath, std::string& message);
// Loads a render preset from a file.
[[nodiscard]] bool loadRenderPreset(const std::string& filePath, RenderPreset& outPreset, std::string& message);
// Lists available render preset files in a directory.
[[nodiscard]] std::vector<std::string> listRenderPresetFiles(const std::string& directoryPath);

} // namespace ws::gui
