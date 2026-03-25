#pragma once

#include "ws/app/profile_store.hpp"
#include "ws/app/shell_support.hpp"
#include "ws/core/runtime.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ws::gui {

class RuntimeService {
public:
    RuntimeService();

    [[nodiscard]] const app::LaunchConfig& config() const noexcept { return config_; }
    void setConfig(const app::LaunchConfig& config) { config_ = config; }

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isPaused() const;

    bool start(std::string& message);
    bool restart(std::string& message);
    bool stop(std::string& message);

    bool step(std::uint32_t count, std::string& message);
    bool runUntil(std::uint64_t targetStep, std::string& message);
    bool pause(std::string& message);
    bool resume(std::string& message);

    bool status(std::string& message) const;
    bool metrics(std::string& message) const;
    bool listFields(std::string& message) const;
    bool summarizeField(const std::string& variableName, std::string& message) const;

    bool createCheckpoint(const std::string& label, std::string& message);
    bool restoreCheckpoint(const std::string& label, std::string& message);
    bool listCheckpoints(std::string& message) const;

    bool saveProfile(const std::string& name, std::string& message);
    bool loadProfile(const std::string& name, std::string& message);
    bool listProfiles(std::string& message) const;

private:
    bool requireRuntime(const char* operation, std::string& message) const;

    app::LaunchConfig config_{};
    std::unique_ptr<Runtime> runtime_;
    std::map<std::string, RuntimeCheckpoint, std::less<>> checkpoints_;
    app::ProfileStore profileStore_{};
};

} // namespace ws::gui
