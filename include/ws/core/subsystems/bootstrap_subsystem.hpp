#pragma once

#include "ws/core/scheduler.hpp"

namespace ws {

class BootstrapSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

} // namespace ws
