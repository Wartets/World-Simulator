#pragma once

#include <cstdint>
#include <string>

namespace ws::gui {

struct TimeControlStatus {
    std::uint64_t currentStep = 0;
    std::uint64_t targetStep = 0;
    float simulationTime = 0.0f;
    float playbackSpeed = 1.0f;
};

[[nodiscard]] float timeControlProgress(const TimeControlStatus& status) noexcept;
[[nodiscard]] std::string formatTimeControlStatus(const TimeControlStatus& status);

} // namespace ws::gui
