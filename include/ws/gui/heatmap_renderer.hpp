#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Heatmap Normalization
// =============================================================================

// Method for normalizing heatmap values.
enum class HeatmapNormalization {
    Linear = 0,       // Linear scaling.
    Logarithmic,      // Logarithmic scaling.
    Sqrt,             // Square root scaling.
    Power,            // Power law scaling.
    Quantile          // Quantile-based normalization.
};

// =============================================================================
// Heatmap Color Map
// =============================================================================

// Available color maps for heatmap rendering.
enum class HeatmapColorMap {
    Viridis = 0,   // Viridis perceptually uniform colormap.
    Hot,           // Yellow to red gradient.
    Cool,          // Cyan to magenta gradient.
    Jet,           // Blue to red rainbow gradient.
    Turbo,         // Improved rainbow colormap.
    Custom         // User-defined custom colormap.
};

// =============================================================================
// Heatmap Render Params
// =============================================================================

// Parameters for heatmap rendering.
struct HeatmapRenderParams {
    float minValue = 0.0f;       // Minimum data value.
    float maxValue = 1.0f;       // Maximum data value.
    HeatmapNormalization normalization = HeatmapNormalization::Linear;  // Normalization method.
    HeatmapColorMap colorMap = HeatmapColorMap::Turbo;  // Color mapping.
    float powerExponent = 1.0f;  // Exponent for power normalization.
    float quantileLow = 0.05f;   // Low quantile for quantile normalization.
    float quantileHigh = 0.95f;  // High quantile for quantile normalization.
};

// =============================================================================
// Heatmap Renderer
// =============================================================================

// Renders scalar data as heatmap visualizations.
class HeatmapRenderer {
public:
    // Initializes the renderer and detects GPU compute support.
    void initialize();
    // Returns whether GPU compute path is available.
    [[nodiscard]] bool supportsGpuComputePath() const;

    // Sets a custom color lookup table.
    void setCustomColorMap(std::vector<std::uint8_t> rgbaLut);

    // Builds RGBA output from float values.
    [[nodiscard]] std::vector<std::uint8_t> buildRgba(
        const std::vector<float>& values,
        std::uint32_t width,
        std::uint32_t height,
        const HeatmapRenderParams& params) const;

private:
    bool gpuComputeSupported_ = false;
    std::vector<std::uint8_t> customColorMap_{};

    // Normalizes a sample value based on normalization method.
    [[nodiscard]] float normalizeSample(float value, const std::vector<float>& finiteValues,
                                        const HeatmapRenderParams& params) const;
    // Samples a color from a colormap at position t (0 to 1).
    [[nodiscard]] std::uint32_t sampleColor(float t, HeatmapColorMap map) const;
};

} // namespace ws::gui

