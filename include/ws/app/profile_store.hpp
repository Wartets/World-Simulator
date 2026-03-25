#pragma once

#include "ws/app/shell_support.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::app {

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path rootDirectory = "profiles");

    void save(const std::string& profileName, const LaunchConfig& config) const;
    [[nodiscard]] LaunchConfig load(const std::string& profileName) const;
    [[nodiscard]] std::vector<std::string> list() const;
    [[nodiscard]] std::filesystem::path pathFor(const std::string& profileName) const;

private:
    std::filesystem::path rootDirectory_;
};

} // namespace ws::app
