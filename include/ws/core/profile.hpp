#pragma once

#include "ws/core/determinism.hpp"
#include "ws/core/types.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace ws {

struct ModelProfile {
    std::map<std::string, ModelTier, std::less<>> subsystemTiers;
    std::set<std::string, std::less<>> compatibilityAssumptions;
    std::vector<std::string> conservedVariables;

    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
};

struct ProfileResolverInput {
    std::map<std::string, ModelTier, std::less<>> requestedSubsystemTiers;
    std::set<std::string, std::less<>> compatibilityAssumptions;
    std::vector<std::string> conservedVariables;
};

class ProfileResolver {
public:
    [[nodiscard]] ModelProfile resolve(const ProfileResolverInput& input) const;

    [[nodiscard]] static const std::vector<std::string>& requiredSubsystems() noexcept;
};

} // namespace ws
