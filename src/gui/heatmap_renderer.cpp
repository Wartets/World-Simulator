#include "ws/gui/heatmap_renderer.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace ws::gui {
namespace {

[[nodiscard]] static std::uint8_t toByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

[[nodiscard]] static std::uint32_t packColor(float r, float g, float b, float a = 1.0f) {
    return static_cast<std::uint32_t>(toByte(r)) |
           (static_cast<std::uint32_t>(toByte(g)) << 8u) |
           (static_cast<std::uint32_t>(toByte(b)) << 16u) |
           (static_cast<std::uint32_t>(toByte(a)) << 24u);
}

[[nodiscard]] static std::array<float, 3> lerp3(const std::array<float, 3>& a,
                                                const std::array<float, 3>& b,
                                                float t) {
    return {
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t
    };
}

[[nodiscard]] static std::uint32_t gradientColor(
    float t,
    const std::array<std::array<float, 3>, 5>& anchors) {
    const float v = std::clamp(t, 0.0f, 1.0f);
    const float scaled = v * 4.0f;
    const int idx = std::clamp(static_cast<int>(std::floor(scaled)), 0, 3);
    const float frac = scaled - static_cast<float>(idx);
    const auto rgb = lerp3(anchors[static_cast<std::size_t>(idx)],
                           anchors[static_cast<std::size_t>(idx + 1)], frac);
    return packColor(rgb[0], rgb[1], rgb[2]);
}

} // namespace

void HeatmapRenderer::initialize() {
    // Scaffolding: compute support is detected for forward compatibility.
    // Current rendering path remains deterministic CPU-side generation.
    const auto* versionBytes = glGetString(GL_VERSION);
    if (versionBytes == nullptr) {
        gpuComputeSupported_ = false;
        return;
    }

    int major = 0;
    int minor = 0;
    if (std::sscanf(reinterpret_cast<const char*>(versionBytes), "%d.%d", &major, &minor) == 2) {
        gpuComputeSupported_ = (major > 4) || (major == 4 && minor >= 3);
    } else {
        gpuComputeSupported_ = false;
    }
}

bool HeatmapRenderer::supportsGpuComputePath() const {
    return gpuComputeSupported_;
}

void HeatmapRenderer::setCustomColorMap(std::vector<std::uint8_t> rgbaLut) {
    customColorMap_ = std::move(rgbaLut);
}

std::vector<std::uint8_t> HeatmapRenderer::buildRgba(
    const std::vector<float>& values,
    const std::uint32_t width,
    const std::uint32_t height,
    const HeatmapRenderParams& params) const {
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> out(count * 4u, 0u);
    if (count == 0 || values.empty()) {
        return out;
    }

    std::vector<float> finiteValues;
    finiteValues.reserve(values.size());
    for (const float v : values) {
        if (std::isfinite(v)) {
            finiteValues.push_back(v);
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        const float sample = i < values.size() ? values[i] : std::numeric_limits<float>::quiet_NaN();
        std::uint32_t packed = packColor(0.5f, 0.5f, 0.5f, 1.0f);
        if (std::isfinite(sample)) {
            const float normalized = normalizeSample(sample, finiteValues, params);
            packed = sampleColor(normalized, params.colorMap);
        }

        out[i * 4u + 0u] = static_cast<std::uint8_t>(packed & 0xffu);
        out[i * 4u + 1u] = static_cast<std::uint8_t>((packed >> 8u) & 0xffu);
        out[i * 4u + 2u] = static_cast<std::uint8_t>((packed >> 16u) & 0xffu);
        out[i * 4u + 3u] = static_cast<std::uint8_t>((packed >> 24u) & 0xffu);
    }

    return out;
}

float HeatmapRenderer::normalizeSample(float value, const std::vector<float>& finiteValues,
                                       const HeatmapRenderParams& params) const {
    float lo = params.minValue;
    float hi = params.maxValue;

    if (params.normalization == HeatmapNormalization::Quantile && !finiteValues.empty()) {
        std::vector<float> ordered = finiteValues;
        std::sort(ordered.begin(), ordered.end());
        const std::size_t n = ordered.size();
        const std::size_t iLo = static_cast<std::size_t>(std::clamp(params.quantileLow, 0.0f, 1.0f) * static_cast<float>(n - 1));
        const std::size_t iHi = static_cast<std::size_t>(std::clamp(params.quantileHigh, 0.0f, 1.0f) * static_cast<float>(n - 1));
        lo = ordered[iLo];
        hi = ordered[std::max(iLo, iHi)];
    }

    hi = std::max(hi, lo + 1e-6f);
    const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);

    switch (params.normalization) {
        case HeatmapNormalization::Linear:
        case HeatmapNormalization::Quantile:
            return t;
        case HeatmapNormalization::Logarithmic:
            return std::log1p(9.0f * t) / std::log1p(9.0f);
        case HeatmapNormalization::Sqrt:
            return std::sqrt(t);
        case HeatmapNormalization::Power: {
            const float p = std::clamp(params.powerExponent, 0.1f, 8.0f);
            return std::pow(t, p);
        }
        default:
            return t;
    }
}

std::uint32_t HeatmapRenderer::sampleColor(float t, HeatmapColorMap map) const {
    const float v = std::clamp(t, 0.0f, 1.0f);

    if (map == HeatmapColorMap::Custom && !customColorMap_.empty() && customColorMap_.size() >= 8u) {
        const std::size_t lutSize = customColorMap_.size() / 4u;
        const std::size_t idx = static_cast<std::size_t>(v * static_cast<float>(lutSize - 1));
        const std::size_t base = idx * 4u;
        return static_cast<std::uint32_t>(customColorMap_[base + 0u]) |
               (static_cast<std::uint32_t>(customColorMap_[base + 1u]) << 8u) |
               (static_cast<std::uint32_t>(customColorMap_[base + 2u]) << 16u) |
               (static_cast<std::uint32_t>(customColorMap_[base + 3u]) << 24u);
    }

    switch (map) {
        case HeatmapColorMap::Viridis:
            return gradientColor(v, {{{0.267f, 0.004f, 0.329f}, {0.283f, 0.141f, 0.458f}, {0.254f, 0.265f, 0.530f}, {0.207f, 0.372f, 0.553f}, {0.993f, 0.906f, 0.144f}}});
        case HeatmapColorMap::Hot:
            return gradientColor(v, {{{0.050f, 0.000f, 0.000f}, {0.500f, 0.000f, 0.000f}, {0.900f, 0.300f, 0.000f}, {1.000f, 0.800f, 0.000f}, {1.000f, 1.000f, 1.000f}}});
        case HeatmapColorMap::Cool:
            return gradientColor(v, {{{0.000f, 1.000f, 1.000f}, {0.250f, 0.750f, 1.000f}, {0.500f, 0.500f, 1.000f}, {0.750f, 0.250f, 1.000f}, {1.000f, 0.000f, 1.000f}}});
        case HeatmapColorMap::Jet:
            return gradientColor(v, {{{0.000f, 0.000f, 0.500f}, {0.000f, 0.600f, 1.000f}, {0.000f, 1.000f, 0.300f}, {1.000f, 0.800f, 0.000f}, {0.700f, 0.000f, 0.000f}}});
        case HeatmapColorMap::Turbo:
        case HeatmapColorMap::Custom:
        default:
            return gradientColor(v, {{{0.190f, 0.072f, 0.232f}, {0.235f, 0.368f, 0.754f}, {0.273f, 0.751f, 0.436f}, {0.935f, 0.785f, 0.184f}, {0.630f, 0.071f, 0.004f}}});
    }
}

} // namespace ws::gui
