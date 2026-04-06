#include "ws/core/observability.hpp"

#include <utility>

namespace ws {

// Records a trace event to the observability pipeline.
// Updates metrics based on trace channel and event name.
void ObservabilityPipeline::record(TraceRecord record) {
    records_.push_back(std::move(record));

    switch (records_.back().channel) {
        case TraceChannel::Control:
            metrics_.controlTransactions += 1;
            break;
        case TraceChannel::Configuration:
            metrics_.configurationTransactions += 1;
            break;
        case TraceChannel::Input:
            metrics_.inputPatches += 1;
            break;
        case TraceChannel::Event:
            if (records_.back().name == "runtime.event.queued") {
                metrics_.eventsQueued += 1;
            }
            if (records_.back().name == "runtime.event.applied") {
                metrics_.eventsApplied += 1;
            }
            break;
        case TraceChannel::Scheduler:
            if (records_.back().name == "runtime.step.commit") {
                metrics_.stepsExecuted += 1;
            }
            break;
        case TraceChannel::Replay:
            if (records_.back().name == "runtime.checkpoint.created") {
                metrics_.checkpointsCreated += 1;
            }
            if (records_.back().name == "runtime.checkpoint.loaded") {
                metrics_.checkpointsLoaded += 1;
            }
            break;
    }
}

void ObservabilityPipeline::clear() noexcept {
    records_.clear();
    metrics_ = RuntimeMetrics{};
}

} // namespace ws
