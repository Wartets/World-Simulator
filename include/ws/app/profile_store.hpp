#pragma once

// App dependencies
#include "ws/app/shell_support.hpp"

// Standard library
#include <filesystem>
#include <string>
#include <vector>

namespace ws::app {

// =============================================================================
// Profile Scope
// =============================================================================

// Scope key for organizing profiles by model.
struct ProfileScope {
    std::string modelKey;
};

// =============================================================================
// Profile Store
// =============================================================================

// Manages persistence of launch configuration profiles.
class ProfileStore {
public:
    // Constructs a profile store with the given root directory.
    explicit ProfileStore(std::filesystem::path rootDirectory = "profiles");

    // Saves a launch configuration as a named profile.
    void save(const std::string& profileName, const LaunchConfig& config, const std::string& modelKey = {}) const;
    // Loads a profile by name.
    [[nodiscard]] LaunchConfig load(const std::string& profileName, const std::string& modelKey = {}) const;
    // Lists all profile names, optionally filtered by model key.
    [[nodiscard]] std::vector<std::string> list(const std::string& modelKey = {}) const;
    // Returns the file path for a profile.
    [[nodiscard]] std::filesystem::path pathFor(const std::string& profileName, const std::string& modelKey = {}) const;
    // Returns the canonical write path for a profile without legacy fallback.
    [[nodiscard]] std::filesystem::path writePathFor(const std::string& profileName, const std::string& modelKey = {}) const;

private:
    // Normalizes the model key for use in file names.
    [[nodiscard]] static std::string normalizeScopeKey(std::string modelKey);

    std::filesystem::path rootDirectory_;
};

} // namespace ws::app
