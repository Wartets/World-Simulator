#pragma once

#include "ws/app/shell_support.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::app {

struct ProfileScope {
    std::string modelKey;
};

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path rootDirectory = "profiles");

    void save(const std::string& profileName, const LaunchConfig& config, const std::string& modelKey = {}) const;
    [[nodiscard]] LaunchConfig load(const std::string& profileName, const std::string& modelKey = {}) const;
    [[nodiscard]] std::vector<std::string> list(const std::string& modelKey = {}) const;
    [[nodiscard]] std::filesystem::path pathFor(const std::string& profileName, const std::string& modelKey = {}) const;

private:
    [[nodiscard]] static std::string normalizeScopeKey(std::string modelKey);

    std::filesystem::path rootDirectory_;
};

} // namespace ws::app
