#pragma once

#include "ws/core/state_store.hpp"

#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Histogram Normalization
// =============================================================================

// Method for normalizing histogram values.
enum class HistogramNormalization : std::uint8_t {
    Count = 0,        // Raw count per bin.
    Density = 1,      // Normalized by bin width (density).
    MaxNormalized = 2 // Normalized so maximum bin equals 1.
};

// =============================================================================
// Histogram Stats
// =============================================================================

// Statistical summary of a histogram.
struct HistogramStats {
    std::size_t count = 0;      // Total number of values.
    float minValue = 0.0f;      // Minimum value.
    float maxValue = 0.0f;      // Maximum value.
    double mean = 0.0;          // Arithmetic mean.
    double median = 0.0;        // Median value.
    double stddev = 0.0;        // Standard deviation.
    double skewness = 0.0;      // Skewness (asymmetry).
    double kurtosis = 0.0;      // Kurtosis (tail heaviness).
};

// =============================================================================
// Histogram Result
// =============================================================================

// Complete histogram computation result.
struct HistogramResult {
    HistogramStats stats{};                 // Statistical summary.
    std::vector<float> binCenters;          // Center value of each bin.
    std::vector<float> binValues;           // Count or normalized value per bin.
};

// Computes a histogram from a field payload.
[[nodiscard]] bool computeHistogram(
    const StateStoreSnapshot::FieldPayload& field,
    int binCount,
    HistogramNormalization normalization,
    HistogramResult& result,
    std::string& message);

} // namespace ws::gui
