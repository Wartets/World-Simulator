#include "ws/gui/time_control_panel.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ws::gui {

// Computes playback progress as fraction [0, 1].
// Returns 0 if target step is zero (not started).
// @param status Time control status with current and target steps
// @return Progress fraction clamped to [0, 1]
float timeControlProgress(const TimeControlStatus& status) noexcept {
    if (status.targetStep == 0u) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(status.currentStep) / static_cast<float>(status.targetStep), 0.0f, 1.0f);
}

// Formats time control status as human-readable string.
// Includes step count, target, simulation time, playback speed, and progress.
// @param status Time control status to format
// @return Formatted status string
std::string formatTimeControlStatus(const TimeControlStatus& status) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2)
           << "step=" << status.currentStep
           << " target=" << status.targetStep
           << " time=" << status.simulationTime
           << " speed=" << status.playbackSpeed << "x"
           << " progress=" << (timeControlProgress(status) * 100.0f) << "%";
    return output.str();
}

} // namespace ws::gui
