#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ws::gui {

struct ConstraintViolationRecord {
    std::uint64_t step = 0;
    float time = 0.0f;
    std::string variable;
    std::uint64_t cellIndex = 0;
    std::string severity = "warning";
    std::string rawMessage;
};

class ConstraintMonitor {
public:
    void recordStep(const StepDiagnostics& diagnostics, std::uint64_t step, float time);
    void clear() noexcept;

    [[nodiscard]] const std::vector<ConstraintViolationRecord>& history() const noexcept { return history_; }
    [[nodiscard]] std::size_t violationsThisStep() const noexcept { return violationsThisStep_; }

private:
    [[nodiscard]] static bool parseConstraintMessage(
        const std::string& message,
        std::string& variable,
        std::uint64_t& index,
        std::string& severity);

    std::size_t violationsThisStep_ = 0;
    std::vector<ConstraintViolationRecord> history_;
};

} // namespace ws::gui
