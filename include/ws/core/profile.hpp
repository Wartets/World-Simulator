#pragma once

// Core dependencies
#include "ws/core/determinism.hpp"
#include "ws/core/types.hpp"

// Standard library
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Model Profile
// =============================================================================

// Resolved profile for a model containing tier assignments and metadata.
struct ModelProfile {
    // Map from subsystem names to their assigned tier.
    std::map<std::string, ModelTier, std::less<>> subsystemTiers;
    // Set of compatibility assumptions required by the model.
    std::set<std::string, std::less<>> compatibilityAssumptions;
    // List of variables that should be conserved during simulation.
    std::vector<std::string> conservedVariables;

    // Computes a fingerprint hash of the profile for identification.
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
};

// =============================================================================
// Profile Resolver Input
// =============================================================================

// Input to the profile resolver specifying requested tiers and assumptions.
struct ProfileResolverInput {
    // Requested tier for each subsystem.
    std::map<std::string, ModelTier, std::less<>> requestedSubsystemTiers;
    // Compatibility assumptions the model requires.
    std::set<std::string, std::less<>> compatibilityAssumptions;
    // Variables that must be conserved.
    std::vector<std::string> conservedVariables;
};

// =============================================================================
// Profile Resolver
// =============================================================================

// Resolves profile requests by validating tier compatibility and assumptions.
class ProfileResolver {
public:
    // Resolves the input into a validated model profile.
    [[nodiscard]] ModelProfile resolve(const ProfileResolverInput& input) const;

    // Returns the list of required subsystems that must be present.
    [[nodiscard]] static const std::vector<std::string>& requiredSubsystems() noexcept;
};

} // namespace ws
