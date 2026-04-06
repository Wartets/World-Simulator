#pragma once

#include <cstdint>
#include <string>

namespace ws::gui {

// =============================================================================
// Time Control Status
// =============================================================================

// Current state of the time control system.
struct TimeControlStatus {
    std::uint64_t currentStep = 0;     // Current simulation step.
    std::uint64_t targetStep = 0;      // Target step to pause at.
    float simulationTime = 0.0f;       // Current simulation time.
    float playbackSpeed = 1.0f;        // Playback speed multiplier.
};

// Calculates the progress (0.0 to 1.0) of time control.
[[nodiscard]] float timeControlProgress(const TimeControlStatus& status) noexcept;
// Formats the time control status as a string.
[[nodiscard]] std::string formatTimeControlStatus(const TimeControlStatus& status);

} // namespace ws::gui
