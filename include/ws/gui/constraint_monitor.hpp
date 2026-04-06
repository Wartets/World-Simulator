#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Constraint Violation Record
// =============================================================================

// Record of a constraint violation at a specific simulation step.
struct ConstraintViolationRecord {
    std::uint64_t step = 0;            // Simulation step at which violation occurred.
    float time = 0.0f;                 // Simulation time at violation.
    std::string variable;              // Name of the variable that violated constraint.
    std::uint64_t cellIndex = 0;       // Index of the cell where violation occurred.
    std::string severity = "warning";  // Severity level ("warning" or "error").
    std::string rawMessage;            // Raw constraint violation message.
};

// =============================================================================
// Constraint Monitor
// =============================================================================

// Monitors and records constraint violations during simulation.
class ConstraintMonitor {
public:
    // Records constraint violations from step diagnostics.
    void recordStep(const StepDiagnostics& diagnostics, std::uint64_t step, float time);
    // Clears all recorded violations.
    void clear() noexcept;

    // Returns the history of all constraint violations.
    [[nodiscard]] const std::vector<ConstraintViolationRecord>& history() const noexcept { return history_; }
    // Returns the number of violations recorded for the current step.
    [[nodiscard]] std::size_t violationsThisStep() const noexcept { return violationsThisStep_; }

private:
    // Parses a constraint message to extract violation details.
    [[nodiscard]] static bool parseConstraintMessage(
        const std::string& message,
        std::string& variable,
        std::uint64_t& index,
        std::string& severity);

    std::size_t violationsThisStep_ = 0;
    std::vector<ConstraintViolationRecord> history_;
};

} // namespace ws::gui
