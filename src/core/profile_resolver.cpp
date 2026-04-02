#include "ws/core/profile.hpp"

#include <algorithm>
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

    std::vector<std::string> normalizedConservedVariables = conservedVariables;
    std::sort(normalizedConservedVariables.begin(), normalizedConservedVariables.end());
    normalizedConservedVariables.erase(
        std::unique(normalizedConservedVariables.begin(), normalizedConservedVariables.end()),
        normalizedConservedVariables.end());
    for (const auto& variable : normalizedConservedVariables) {
        value = DeterministicHash::combine(value, DeterministicHash::hashString(variable));
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
    profile.conservedVariables = input.conservedVariables;

    profile.conservedVariables.erase(
        std::remove_if(profile.conservedVariables.begin(), profile.conservedVariables.end(), [](const std::string& variable) {
            return variable.empty();
        }),
        profile.conservedVariables.end());
    std::sort(profile.conservedVariables.begin(), profile.conservedVariables.end());
    profile.conservedVariables.erase(
        std::unique(profile.conservedVariables.begin(), profile.conservedVariables.end()),
        profile.conservedVariables.end());

    if (profile.conservedVariables.empty()) {
        profile.conservedVariables = {"resource_stock_r", "surface_water_w"};
    }

    if (profile.compatibilityAssumptions.empty()) {
        throw std::invalid_argument("ProfileResolver requires at least one explicit compatibility assumption");
    }

    return profile;
}

} // namespace ws
