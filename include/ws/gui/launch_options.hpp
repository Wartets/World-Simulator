#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui {

enum class LaunchAction {
    None = 0,
    OpenModelEditor,
    SelectModelForSession,
    OpenWorldByName,
    ImportWorldFile,
    OpenCheckpointFile
};

struct MainWindowLaunchRequest {
    LaunchAction action = LaunchAction::None;
    std::filesystem::path targetPath;
    std::filesystem::path modelPath;
    std::string worldName;
};

struct LaunchParseResult {
    bool ok = true;
    bool showHelp = false;
    MainWindowLaunchRequest request{};
    std::string error;
};

[[nodiscard]] LaunchParseResult parseLaunchOptions(const std::vector<std::string>& args);
[[nodiscard]] const char* launchActionLabel(LaunchAction action);

} // namespace ws::gui
