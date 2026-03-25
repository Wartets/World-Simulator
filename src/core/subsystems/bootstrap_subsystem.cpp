#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <stdexcept>

namespace ws {

std::string BootstrapSubsystem::name() const {
    return "bootstrap";
}

std::vector<std::string> BootstrapSubsystem::declaredReadSet() const {
    return {};
}

std::vector<std::string> BootstrapSubsystem::declaredWriteSet() const {
    return {"bootstrap_marker", "temperature_T", "humidity_q"};
}

void BootstrapSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const auto iterator = profile.subsystemTiers.find("generation");
    if (iterator == profile.subsystemTiers.end()) {
        throw std::runtime_error("BootstrapSubsystem expected generation tier in profile");
    }

    const ModelTier tier = iterator->second;
    switch (tier) {
        case ModelTier::A:
            writeSession.fillScalar("bootstrap_marker", 1.0f);
            writeSession.fillScalar("temperature_T", 273.15f);
            break;
        case ModelTier::B:
            writeSession.fillScalar("bootstrap_marker", 2.0f);
            writeSession.fillScalar("temperature_T", 275.15f);
            writeSession.fillScalar("humidity_q", 0.5f);
            break;
        case ModelTier::C:
            writeSession.fillScalar("bootstrap_marker", 3.0f);
            writeSession.fillScalar("temperature_T", 278.15f);
            writeSession.fillScalar("humidity_q", 0.65f);
            break;
    }
}

void BootstrapSubsystem::step(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t stepIndex) {
    const auto iterator = profile.subsystemTiers.find("generation");
    if (iterator == profile.subsystemTiers.end()) {
        throw std::runtime_error("BootstrapSubsystem expected generation tier in profile");
    }

    const float base = static_cast<float>(stepIndex + 1);
    const float tierOffset = static_cast<float>(static_cast<std::uint8_t>(iterator->second));
    writeSession.fillScalar("bootstrap_marker", base + tierOffset);
}

} // namespace ws
