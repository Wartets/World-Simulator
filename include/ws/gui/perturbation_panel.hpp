#pragma once

#include "ws/core/runtime.hpp"

#include <string>

namespace ws::gui {

// Validates a perturbation specification against a grid.
[[nodiscard]] bool validatePerturbation(const PerturbationSpec& perturbation, const GridSpec& grid, std::string& message);
// Estimates the number of cells affected by a perturbation.
[[nodiscard]] std::uint64_t estimatePerturbationCellCount(const PerturbationSpec& perturbation, const GridSpec& grid);

} // namespace ws::gui
