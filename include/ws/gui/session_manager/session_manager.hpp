#pragma once

#include "ws/gui/runtime_service.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui::session_manager {

struct SessionUiState {
    std::vector<StoredWorldInfo> worlds;
    int selectedWorldIndex = -1;
    bool needsRefresh = true;
    int pendingDeleteWorldIndex = -1;
    char pendingWorldName[128] = "";
    char statusMessage[256] = "";
};

[[nodiscard]] std::string formatBytes(std::uintmax_t bytes);
[[nodiscard]] std::string formatFileTime(const std::filesystem::file_time_type& timePoint, bool available);

} // namespace ws::gui::session_manager
