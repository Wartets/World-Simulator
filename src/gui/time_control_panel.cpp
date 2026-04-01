#include "ws/gui/time_control_panel.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ws::gui {

float timeControlProgress(const TimeControlStatus& status) noexcept {
    if (status.targetStep == 0u) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(status.currentStep) / static_cast<float>(status.targetStep), 0.0f, 1.0f);
}

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
