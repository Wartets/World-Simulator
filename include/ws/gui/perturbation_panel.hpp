#pragma once

#include "ws/core/runtime.hpp"

#include <string>

namespace ws::gui {

[[nodiscard]] bool validatePerturbation(const PerturbationSpec& perturbation, const GridSpec& grid, std::string& message);
[[nodiscard]] std::uint64_t estimatePerturbationCellCount(const PerturbationSpec& perturbation, const GridSpec& grid);

} // namespace ws::gui
