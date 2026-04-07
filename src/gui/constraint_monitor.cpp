#include "ws/gui/constraint_monitor.hpp"

#include <algorithm>

namespace ws::gui {

// Records step diagnostics and parses constraint violations.
// Extracts variable name, cell index, and severity from violation messages.
void ConstraintMonitor::recordStep(const StepDiagnostics& diagnostics, const std::uint64_t step, const float time) {
    violationsThisStep_ = diagnostics.constraintViolations.size();

    for (const auto& message : diagnostics.constraintViolations) {
        std::string variable;
        std::uint64_t cellIndex = 0;
        std::string severity = "warning";
        if (!parseConstraintMessage(message, variable, cellIndex, severity)) {
            continue;
        }

        history_.push_back(ConstraintViolationRecord{
            step,
            time,
            std::move(variable),
            cellIndex,
            std::move(severity),
            message});
    }
}

// Clears current violation count and history.
void ConstraintMonitor::clear() noexcept {
    violationsThisStep_ = 0;
    history_.clear();
}

// Parses constraint violation message from scheduler.
// Expected format: "clamp:<variable>:index=<flat_cell_index>"
bool ConstraintMonitor::parseConstraintMessage(
    const std::string& message,
    std::string& variable,
    std::uint64_t& index,
    std::string& severity) {
    // Current scheduler encoding: clamp:<variable>:index=<flat_cell_index>
    if (message.rfind("clamp:", 0) == 0) {
        const auto firstColon = message.find(':');
        const auto secondColon = message.find(':', firstColon + 1);
        const auto equals = message.find("index=", secondColon == std::string::npos ? 0 : secondColon);
        if (firstColon == std::string::npos || secondColon == std::string::npos || equals == std::string::npos) {
            return false;
        }

        variable = message.substr(firstColon + 1, secondColon - firstColon - 1);
        const auto indexToken = message.substr(equals + 6);
        try {
            index = static_cast<std::uint64_t>(std::stoull(indexToken));
        } catch (...) {
            return false;
        }

        severity = "warning";
        return true;
    }

    return false;
}

} // namespace ws::gui
