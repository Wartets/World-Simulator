#pragma once

// Core dependencies
#include "ws/core/types.hpp"

// Standard library
#include <cstdint>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Trace Channels
// =============================================================================

// Categories for trace records used in observability logging.
enum class TraceChannel : std::uint8_t {
    Control = 0,
    Configuration = 1,
    Input = 2,
    Event = 3,
    Scheduler = 4,
    Replay = 5
};

// =============================================================================
// Trace Record
// =============================================================================

// Single trace entry for debugging and analysis.
struct TraceRecord {
    std::uint64_t sequence = 0;
    std::uint64_t runIdentityHash = 0;
    std::uint64_t profileFingerprint = 0;
    std::uint64_t stepIndex = 0;
    TraceChannel channel = TraceChannel::Control;
    std::string name;
    std::string detail;
    std::uint64_t payloadFingerprint = 0;
};

// =============================================================================
// Runtime Metrics
// =============================================================================

// Aggregated runtime statistics for performance monitoring.
struct RuntimeMetrics {
    std::uint64_t controlTransactions = 0;
    std::uint64_t configurationTransactions = 0;
    std::uint64_t inputPatches = 0;
    std::uint64_t eventsQueued = 0;
    std::uint64_t eventsApplied = 0;
    std::uint64_t stepsExecuted = 0;
    std::uint64_t checkpointsCreated = 0;
    std::uint64_t checkpointsLoaded = 0;
};

// =============================================================================
// Observability Pipeline
// =============================================================================

// Collects and manages trace records and runtime metrics.
class ObservabilityPipeline {
public:
    // Records a trace entry.
    void record(TraceRecord record);
    // Clears all collected records and resets metrics.
    void clear() noexcept;

    // Returns all collected trace records.
    [[nodiscard]] const std::vector<TraceRecord>& records() const noexcept { return records_; }
    // Returns aggregated runtime metrics.
    [[nodiscard]] const RuntimeMetrics& metrics() const noexcept { return metrics_; }

private:
    std::vector<TraceRecord> records_;
    RuntimeMetrics metrics_{};
};

} // namespace ws
