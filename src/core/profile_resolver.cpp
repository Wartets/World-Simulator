#include "ws/core/profile.hpp"
#include "ws/core/subsystems/subsystems.hpp"

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
    static const std::vector<std::string> names = [] {
        std::vector<std::string> resolved;
        for (const auto& subsystem : makePhase4Subsystems()) {
            if (!subsystem) {
                continue;
            }
            const std::string subsystemName = subsystem->name();
            if (!subsystemName.empty()) {
                resolved.push_back(subsystemName);
            }
        }
        resolved.push_back("temporal");
        std::sort(resolved.begin(), resolved.end());
        resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
        return resolved;
    }();
    return names;
}

ModelProfile ProfileResolver::resolve(const ProfileResolverInput& input) const {
    if (input.requestedSubsystemTiers.empty()) {
        throw std::invalid_argument("ProfileResolver requires at least one subsystem profile entry");
    }

    ModelProfile profile;
    for (const auto& [subsystemName, tier] : input.requestedSubsystemTiers) {
        if (subsystemName.empty()) {
            continue;
        }
        profile.subsystemTiers.insert_or_assign(subsystemName, tier);
    }
    profile.compatibilityAssumptions = input.compatibilityAssumptions;
    profile.conservedVariables = input.conservedVariables;

    if (profile.subsystemTiers.empty()) {
        throw std::invalid_argument("ProfileResolver requires at least one non-empty subsystem profile entry");
    }

    profile.conservedVariables.erase(
        std::remove_if(profile.conservedVariables.begin(), profile.conservedVariables.end(), [](const std::string& variable) {
            return variable.empty();
        }),
        profile.conservedVariables.end());
    std::sort(profile.conservedVariables.begin(), profile.conservedVariables.end());
    profile.conservedVariables.erase(
        std::unique(profile.conservedVariables.begin(), profile.conservedVariables.end()),
        profile.conservedVariables.end());

    if (profile.compatibilityAssumptions.empty()) {
        throw std::invalid_argument("ProfileResolver requires at least one explicit compatibility assumption");
    }

    return profile;
}

} // namespace ws
