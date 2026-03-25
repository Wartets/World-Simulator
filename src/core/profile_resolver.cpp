#include "ws/core/profile.hpp"

#include <stdexcept>

namespace ws {

std::uint64_t ModelProfile::fingerprint() const noexcept {
    std::uint64_t value = DeterministicHash::offsetBasis;

    for (const auto& [subsystem, tier] : subsystemTiers) {
        value = DeterministicHash::combine(value, DeterministicHash::hashString(subsystem));
        value = DeterministicHash::combine(value, static_cast<std::uint64_t>(tier));
    }

    for (const auto& assumption : compatibilityAssumptions) {
        value = DeterministicHash::combine(value, DeterministicHash::hashString(assumption));
    }

    return value;
}

const std::vector<std::string>& ProfileResolver::requiredSubsystems() noexcept {
    static const std::vector<std::string> names = {
        "generation",
        "hydrology",
        "temperature",
        "humidity",
        "wind",
        "climate",
        "soil",
        "resources",
        "vegetation",
        "events",
        "temporal"
    };
    return names;
}

ModelProfile ProfileResolver::resolve(const ProfileResolverInput& input) const {
    for (const auto& subsystem : requiredSubsystems()) {
        if (!input.requestedSubsystemTiers.contains(subsystem)) {
            throw std::invalid_argument("ProfileResolver missing tier for subsystem: " + subsystem);
        }
    }

    ModelProfile profile;
    profile.subsystemTiers = input.requestedSubsystemTiers;
    profile.compatibilityAssumptions = input.compatibilityAssumptions;

    if (profile.compatibilityAssumptions.empty()) {
        throw std::invalid_argument("ProfileResolver requires at least one explicit compatibility assumption");
    }

    return profile;
}

} // namespace ws
