#include "ws/gui/histogram_panel.hpp"

#include <algorithm>
#include <cmath>

namespace ws::gui {

bool computeHistogram(
    const StateStoreSnapshot::FieldPayload& field,
    const int binCount,
    const HistogramNormalization normalization,
    HistogramResult& result,
    std::string& message) {
    if (binCount < 2) {
        message = "histogram_failed reason=invalid_bin_count";
        return false;
    }

    std::vector<float> samples;
    samples.reserve(field.values.size());
    const auto logicalCount = std::min(field.values.size(), field.validityMask.size());
    for (std::size_t i = 0; i < logicalCount; ++i) {
        if (field.validityMask[i] == 0u) {
            continue;
        }

        const float value = field.values[i];
        if (!std::isfinite(value)) {
            continue;
        }
        samples.push_back(value);
    }

    if (samples.empty()) {
        message = "histogram_failed reason=no_valid_samples";
        return false;
    }

    result = HistogramResult{};
    result.stats.count = samples.size();
    auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    result.stats.minValue = *minIt;
    result.stats.maxValue = *maxIt;

    double sum = 0.0;
    for (const float value : samples) {
        sum += static_cast<double>(value);
    }
    result.stats.mean = sum / static_cast<double>(samples.size());

    std::sort(samples.begin(), samples.end());
    const std::size_t mid = samples.size() / 2;
    if ((samples.size() & 1u) == 0u) {
        result.stats.median = (static_cast<double>(samples[mid - 1]) + static_cast<double>(samples[mid])) * 0.5;
    } else {
        result.stats.median = static_cast<double>(samples[mid]);
    }

    double m2 = 0.0;
    double m3 = 0.0;
    double m4 = 0.0;
    for (const float value : samples) {
        const double delta = static_cast<double>(value) - result.stats.mean;
        const double d2 = delta * delta;
        m2 += d2;
        m3 += d2 * delta;
        m4 += d2 * d2;
    }

    const double n = static_cast<double>(samples.size());
    const double variance = m2 / n;
    result.stats.stddev = std::sqrt(std::max(0.0, variance));
    if (result.stats.stddev > 0.0) {
        const double sigma3 = result.stats.stddev * result.stats.stddev * result.stats.stddev;
        const double sigma4 = sigma3 * result.stats.stddev;
        result.stats.skewness = (m3 / n) / sigma3;
        result.stats.kurtosis = (m4 / n) / sigma4;
    }

    result.binCenters.resize(static_cast<std::size_t>(binCount));
    result.binValues.assign(static_cast<std::size_t>(binCount), 0.0f);

    const float range = std::max(1e-6f, result.stats.maxValue - result.stats.minValue);
    const float invWidth = static_cast<float>(binCount) / range;
    for (const float value : samples) {
        int index = static_cast<int>((value - result.stats.minValue) * invWidth);
        index = std::clamp(index, 0, binCount - 1);
        result.binValues[static_cast<std::size_t>(index)] += 1.0f;
    }

    for (int i = 0; i < binCount; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(binCount);
        result.binCenters[static_cast<std::size_t>(i)] = result.stats.minValue + t * range;
    }

    if (normalization == HistogramNormalization::Density) {
        const float scale = 1.0f / static_cast<float>(samples.size());
        for (float& value : result.binValues) {
            value *= scale;
        }
    } else if (normalization == HistogramNormalization::MaxNormalized) {
        const auto maxBin = *std::max_element(result.binValues.begin(), result.binValues.end());
        if (maxBin > 0.0f) {
            const float invMax = 1.0f / maxBin;
            for (float& value : result.binValues) {
                value *= invMax;
            }
        }
    }

    message = "histogram_ready bins=" + std::to_string(binCount) + " samples=" + std::to_string(result.stats.count);
    return true;
}

} // namespace ws::gui
