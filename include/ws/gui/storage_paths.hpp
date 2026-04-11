#pragma once

#include <filesystem>
#include <string>

namespace ws::gui::storage {

[[nodiscard]] std::filesystem::path resolveWorkspaceRootFromCurrentPath();
[[nodiscard]] bool isDevelopmentWorkspace(const std::filesystem::path& workspaceRoot);
[[nodiscard]] std::filesystem::path resolveUserSettingsRoot(const std::string& appFolderName);
[[nodiscard]] std::filesystem::path resolveProfilesPath(const std::string& appFolderName);

} // namespace ws::gui::storage
