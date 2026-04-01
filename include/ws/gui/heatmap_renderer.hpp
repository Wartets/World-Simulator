#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ws::gui {

enum class HeatmapNormalization {
    Linear = 0,
    Logarithmic,
    Sqrt,
    Power,
    Quantile
};

enum class HeatmapColorMap {
    Viridis = 0,
    Hot,
    Cool,
    Jet,
    Turbo,
    Custom
};

struct HeatmapRenderParams {
    float minValue = 0.0f;
    float maxValue = 1.0f;
    HeatmapNormalization normalization = HeatmapNormalization::Linear;
    HeatmapColorMap colorMap = HeatmapColorMap::Turbo;
    float powerExponent = 1.0f;
    float quantileLow = 0.05f;
    float quantileHigh = 0.95f;
};

class HeatmapRenderer {
public:
    // GPU compute path is runtime-detected; fallback path is always available.
    void initialize();
    [[nodiscard]] bool supportsGpuComputePath() const;

    void setCustomColorMap(std::vector<std::uint8_t> rgbaLut);

    [[nodiscard]] std::vector<std::uint8_t> buildRgba(
        const std::vector<float>& values,
        std::uint32_t width,
        std::uint32_t height,
        const HeatmapRenderParams& params) const;

private:
    bool gpuComputeSupported_ = false;
    std::vector<std::uint8_t> customColorMap_{};

    [[nodiscard]] float normalizeSample(float value, const std::vector<float>& finiteValues,
                                        const HeatmapRenderParams& params) const;
    [[nodiscard]] std::uint32_t sampleColor(float t, HeatmapColorMap map) const;
};

} // namespace ws::gui
