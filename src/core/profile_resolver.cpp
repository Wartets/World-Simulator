#include "ws/core/profile.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <stdexcept>

namespace ws {

namespace {

bool crossConstraintLess(const CrossVariableConstraint& lhs, const CrossVariableConstraint& rhs) {
    if (lhs.lhsVariable != rhs.lhsVariable) return lhs.lhsVariable < rhs.lhsVariable;
    if (lhs.rhsVariable != rhs.rhsVariable) return lhs.rhsVariable < rhs.rhsVariable;
    if (lhs.relation != rhs.relation) return static_cast<std::uint8_t>(lhs.relation) < static_cast<std::uint8_t>(rhs.relation);
    if (lhs.offset != rhs.offset) return lhs.offset < rhs.offset;
    if (lhs.tolerance != rhs.tolerance) return lhs.tolerance < rhs.tolerance;
    if (lhs.autoClamp != rhs.autoClamp) return lhs.autoClamp < rhs.autoClamp;
    return lhs.id < rhs.id;
}

bool crossConstraintEquivalent(const CrossVariableConstraint& lhs, const CrossVariableConstraint& rhs) {
    return lhs.lhsVariable == rhs.lhsVariable &&
        lhs.rhsVariable == rhs.rhsVariable &&
        lhs.relation == rhs.relation &&
        lhs.offset == rhs.offset &&
        lhs.tolerance == rhs.tolerance &&
        lhs.autoClamp == rhs.autoClamp;
}

} // namespace

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

    std::vector<CrossVariableConstraint> normalizedConstraints = crossVariableConstraints;
    std::sort(normalizedConstraints.begin(), normalizedConstraints.end(), crossConstraintLess);
    normalizedConstraints.erase(
        std::unique(normalizedConstraints.begin(), normalizedConstraints.end(), crossConstraintEquivalent),
        normalizedConstraints.end());

    for (const auto& constraint : normalizedConstraints) {
        value = DeterministicHash::combine(value, DeterministicHash::hashString(constraint.id));
        value = DeterministicHash::combine(value, DeterministicHash::hashString(constraint.lhsVariable));
        value = DeterministicHash::combine(value, DeterministicHash::hashString(constraint.rhsVariable));
        value = DeterministicHash::combine(value, static_cast<std::uint64_t>(constraint.relation));
        value = DeterministicHash::combine(value, DeterministicHash::hashPod(constraint.offset));
        value = DeterministicHash::combine(value, DeterministicHash::hashPod(constraint.tolerance));
        value = DeterministicHash::combine(value, DeterministicHash::hashPod(constraint.autoClamp));
    }

    return value;
}

const std::vector<std::string>& ProfileResolver::requiredSubsystems() noexcept {
    static const std::vector<std::string> names = [] {
        return std::vector<std::string>{};
    }();
    return names;
}

ModelProfile ProfileResolver::resolve(const ProfileResolverInput& input) const {
    ModelProfile profile;
    for (const auto& [subsystemName, tier] : input.requestedSubsystemTiers) {
        if (subsystemName.empty()) {
            continue;
        }
        profile.subsystemTiers.insert_or_assign(subsystemName, tier);
    }
    profile.compatibilityAssumptions = input.compatibilityAssumptions;
    profile.conservedVariables = input.conservedVariables;
    profile.crossVariableConstraints = input.crossVariableConstraints;

    profile.conservedVariables.erase(
        std::remove_if(profile.conservedVariables.begin(), profile.conservedVariables.end(), [](const std::string& variable) {
            return variable.empty();
        }),
        profile.conservedVariables.end());
    std::sort(profile.conservedVariables.begin(), profile.conservedVariables.end());
    profile.conservedVariables.erase(
        std::unique(profile.conservedVariables.begin(), profile.conservedVariables.end()),
        profile.conservedVariables.end());

    profile.crossVariableConstraints.erase(
        std::remove_if(profile.crossVariableConstraints.begin(), profile.crossVariableConstraints.end(), [](const CrossVariableConstraint& constraint) {
            return constraint.lhsVariable.empty() || constraint.rhsVariable.empty();
        }),
        profile.crossVariableConstraints.end());
    for (auto& constraint : profile.crossVariableConstraints) {
        if (constraint.tolerance < 0.0f) {
            constraint.tolerance = 0.0f;
        }
    }
    std::sort(profile.crossVariableConstraints.begin(), profile.crossVariableConstraints.end(), crossConstraintLess);
    profile.crossVariableConstraints.erase(
        std::unique(profile.crossVariableConstraints.begin(), profile.crossVariableConstraints.end(), crossConstraintEquivalent),
        profile.crossVariableConstraints.end());

    return profile;
}

} // namespace ws
