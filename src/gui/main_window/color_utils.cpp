#include "ws/gui/main_window/color_utils.hpp"

#include <algorithm>
#include <cmath>

namespace ws::gui::main_window::detail {

// Generates Turbo-like colormap color for normalized value.
// Implements perceptually uniform color gradient.
// @param t01 Normalized value in range [0, 1]
// @return ImGui packed color
ImU32 colormapTurboLike(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const float r = std::clamp(1.5f * t, 0.0f, 1.0f);
    const float g = std::clamp(1.5f * (1.0f - std::abs(2.0f * t - 1.0f)), 0.0f, 1.0f);
    const float b = std::clamp(1.5f * (1.0f - t), 0.0f, 1.0f);
    return IM_COL32(
        static_cast<int>(r * 255.0f),
        static_cast<int>(g * 255.0f),
        static_cast<int>(b * 255.0f),
        255);
}

// Generates grayscale color for normalized value.
// @param t01 Normalized value in range [0, 1]
// @return ImGui packed grayscale color
ImU32 colormapGrayscale(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const int c = static_cast<int>(t * 255.0f);
    return IM_COL32(c, c, c, 255);
}

ImU32 colormapDiverging(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const float k = (t - 0.5f) * 2.0f;
    if (k >= 0.0f) {
        const int r = static_cast<int>((0.4f + 0.6f * k) * 255.0f);
        const int g = static_cast<int>((0.2f + 0.5f * (1.0f - k)) * 255.0f);
        const int b = static_cast<int>((0.2f + 0.4f * (1.0f - k)) * 255.0f);
        return IM_COL32(r, g, b, 255);
    }

    const float a = -k;
    const int r = static_cast<int>((0.2f + 0.4f * (1.0f - a)) * 255.0f);
    const int g = static_cast<int>((0.3f + 0.5f * (1.0f - a)) * 255.0f);
    const int b = static_cast<int>((0.45f + 0.55f * a) * 255.0f);
    return IM_COL32(r, g, b, 255);
}

ImU32 colormapWater(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const int r = static_cast<int>(t * 120.0f);
    const int g = static_cast<int>(t * t * 180.0f + 70.0f);
    const int b = static_cast<int>(t * 60.0f + 195.0f);
    return IM_COL32(r, g, b, 255);
}

float applyDisplayTransfer(float t, const float brightness, const float contrast, const float gamma, const bool invertColors) {
    float v = std::clamp(t, 0.0f, 1.0f);
    v = (v - 0.5f) * contrast + 0.5f;
    v *= brightness;
    v = std::clamp(v, 0.0f, 1.0f);

    const float safeGamma = std::max(0.05f, gamma);
    v = std::pow(v, 1.0f / safeGamma);

    if (invertColors) {
        v = 1.0f - v;
    }
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace ws::gui::main_window::detail
