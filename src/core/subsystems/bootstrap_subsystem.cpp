#include "ws/core/subsystems/bootstrap_subsystem.hpp"

namespace ws {

std::string BootstrapSubsystem::name() const {
    return "bootstrap";
}

std::vector<std::string> BootstrapSubsystem::declaredReadSet() const {
    return {};
}

std::vector<std::string> BootstrapSubsystem::declaredWriteSet() const {
    return {"bootstrap_marker"};
}

void BootstrapSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const auto iterator = profile.subsystemTiers.find("bootstrap");
    const float profileOffset = (iterator == profile.subsystemTiers.end())
        ? 0.0f
        : static_cast<float>(static_cast<std::uint8_t>(iterator->second));
    writeSession.fillScalar("bootstrap_marker", 1.0f + profileOffset);
}

void BootstrapSubsystem::step(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t stepIndex) {
    const auto iterator = profile.subsystemTiers.find("bootstrap");
    const float base = static_cast<float>(stepIndex + 1);
    const float tierOffset = (iterator == profile.subsystemTiers.end())
        ? 0.0f
        : static_cast<float>(static_cast<std::uint8_t>(iterator->second));
    writeSession.fillScalar("bootstrap_marker", base + tierOffset);
}

} // namespace ws
