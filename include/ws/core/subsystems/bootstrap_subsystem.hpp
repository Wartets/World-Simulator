#pragma once

#include "ws/core/scheduler.hpp"

namespace ws {

// =============================================================================
// Bootstrap Subsystem
// =============================================================================

// Initializes the simulation state with initial conditions.
class BootstrapSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

} // namespace ws
