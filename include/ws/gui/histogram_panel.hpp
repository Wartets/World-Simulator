#pragma once

#include "ws/core/state_store.hpp"

#include <string>
#include <vector>

namespace ws::gui {

enum class HistogramNormalization : std::uint8_t {
    Count = 0,
    Density = 1,
    MaxNormalized = 2
};

struct HistogramStats {
    std::size_t count = 0;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double mean = 0.0;
    double median = 0.0;
    double stddev = 0.0;
    double skewness = 0.0;
    double kurtosis = 0.0;
};

struct HistogramResult {
    HistogramStats stats{};
    std::vector<float> binCenters;
    std::vector<float> binValues;
};

[[nodiscard]] bool computeHistogram(
    const StateStoreSnapshot::FieldPayload& field,
    int binCount,
    HistogramNormalization normalization,
    HistogramResult& result,
    std::string& message);

} // namespace ws::gui
