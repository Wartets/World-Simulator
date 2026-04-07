#include "ws/gui/perturbation_panel.hpp"

#include <algorithm>
#include <cmath>

namespace ws::gui {

// Validates perturbation specification against grid dimensions.
// Checks target variable, grid validity, amplitude finiteness, and duration.
bool validatePerturbation(const PerturbationSpec& perturbation, const GridSpec& grid, std::string& message) {
    if (perturbation.targetVariable.empty()) {
        message = "perturbation_validation_failed reason=target_variable_required";
        return false;
    }
    if (grid.width == 0 || grid.height == 0) {
        message = "perturbation_validation_failed reason=invalid_grid";
        return false;
    }
    if (!std::isfinite(perturbation.amplitude)) {
        message = "perturbation_validation_failed reason=amplitude_non_finite";
        return false;
    }
    if (perturbation.durationSteps == 0) {
        message = "perturbation_validation_failed reason=duration_zero";
        return false;
    }

    message = "perturbation_validation_ok";
    return true;
}

// Estimates the number of cells affected by perturbation.
// Uses perturbation type and parameters to compute affected area.
std::uint64_t estimatePerturbationCellCount(const PerturbationSpec& perturbation, const GridSpec& grid) {
    switch (perturbation.type) {
        case PerturbationType::Gaussian: {
            const float sigma = std::max(0.5f, perturbation.sigma);
            const std::uint64_t radius = static_cast<std::uint64_t>(std::ceil(3.0f * sigma));
            const std::uint64_t diameter = radius * 2ull + 1ull;
            return std::min<std::uint64_t>(diameter * diameter, grid.cellCount());
        }
        case PerturbationType::Rectangle:
            return std::min<std::uint64_t>(
                static_cast<std::uint64_t>(std::max<std::uint32_t>(1u, perturbation.width)) *
                    static_cast<std::uint64_t>(std::max<std::uint32_t>(1u, perturbation.height)),
                grid.cellCount());
        case PerturbationType::Sine:
        case PerturbationType::WhiteNoise:
        case PerturbationType::Gradient:
            return grid.cellCount();
    }

    return 0;
}

} // namespace ws::gui
