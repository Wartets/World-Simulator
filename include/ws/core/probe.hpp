#pragma once

// Core dependencies
#include "ws/core/state_store.hpp"

// Standard library
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Probe Kind
// =============================================================================

// Classification of data collection probes.
enum class ProbeKind : std::uint8_t {
    GlobalScalar = 0,
    CellScalar = 1,
    RegionAverage = 2
};

// =============================================================================
// Probe Region
// =============================================================================

// Bounding box for region-based probes.
struct ProbeRegion {
    Cell min{0, 0};
    Cell max{0, 0};
};

// =============================================================================
// Probe Definition
// =============================================================================

// Specification for a data collection probe.
struct ProbeDefinition {
    std::string id;
    std::string variableName;
    ProbeKind kind = ProbeKind::GlobalScalar;
    Cell cell{0, 0};
    ProbeRegion region{};
};

// =============================================================================
// Probe Sample
// =============================================================================

// Single sample value from a probe at a point in time.
struct ProbeSample {
    std::uint64_t step = 0;
    float time = 0.0f;
    float value = 0.0f;
};

// =============================================================================
// Probe Series
// =============================================================================

// Time series of samples from a single probe.
struct ProbeSeries {
    ProbeDefinition definition{};
    std::vector<ProbeSample> samples;
};

// =============================================================================
// Probe Statistics
// =============================================================================

// Statistical summary of probe data.
struct ProbeStatistics {
    std::size_t count = 0;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double mean = 0.0;
    double stddev = 0.0;
    float lastValue = 0.0f;
};

// =============================================================================
// Probe Manager
// =============================================================================

// Manages data collection probes for runtime monitoring.
class ProbeManager {
public:
    // Adds a new probe to the collection.
    [[nodiscard]] bool addProbe(const ProbeDefinition& definition, const StateStore& stateStore, std::string& message);
    // Removes a probe by its identifier.
    [[nodiscard]] bool removeProbe(const std::string& probeId, std::string& message);
    // Clears all probes.
    void clear() noexcept;
    // Clears all collected samples but keeps probe definitions.
    void clearSamples() noexcept;

    // Records samples from all probes at the current step.
    void recordAll(const StateStore& stateStore, std::uint64_t step, float time);

    // Returns all probe definitions.
    [[nodiscard]] std::vector<ProbeDefinition> definitions() const;
    // Gets the time series for a specific probe.
    [[nodiscard]] bool getSeries(const std::string& probeId, ProbeSeries& outSeries, std::string& message) const;
    // Returns all probe time series.
    [[nodiscard]] std::vector<ProbeSeries> allSeries() const;

    // Computes statistical summary for a probe series.
    [[nodiscard]] static ProbeStatistics computeStatistics(const ProbeSeries& series);

private:
    // Validates a probe definition against the state store.
    [[nodiscard]] static bool validateDefinition(const ProbeDefinition& definition, const StateStore& stateStore, std::string& message);
    // Samples the current value for a probe.
    [[nodiscard]] static std::optional<float> sample(const ProbeDefinition& definition, const StateStore& stateStore);

    std::map<std::string, ProbeSeries, std::less<>> probes_;
};

} // namespace ws
