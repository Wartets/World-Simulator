#pragma once

#include "ws/core/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ws {

enum class TraceChannel : std::uint8_t {
    Control = 0,
    Configuration = 1,
    Input = 2,
    Event = 3,
    Scheduler = 4,
    Replay = 5
};

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

class ObservabilityPipeline {
public:
    void record(TraceRecord record);
    void clear() noexcept;

    [[nodiscard]] const std::vector<TraceRecord>& records() const noexcept { return records_; }
    [[nodiscard]] const RuntimeMetrics& metrics() const noexcept { return metrics_; }

private:
    std::vector<TraceRecord> records_;
    RuntimeMetrics metrics_{};
};

} // namespace ws
