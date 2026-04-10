#pragma once

#include "ws/gui/session_manager/session_manager.hpp"
#include <imgui.h>
#include <filesystem>
#include <string>
#include <chrono>

namespace ws::gui::session_wizard_helpers {

/// @brief Provides human-readable status label for a stored world's persistence state
[[nodiscard]] const char* worldStorageStatusLabel(const StoredWorldInfo& world);

/// @brief Provides color for status indicator based on world persistence completeness
[[nodiscard]] ImVec4 worldStorageStatusColor(const StoredWorldInfo& world);

/// @brief Summarizes how a world will be opened (from checkpoint or profile)
[[nodiscard]] std::string worldResumeSummary(const StoredWorldInfo& world);

/// @brief Shows which persistent data (profile/checkpoint/view) is available
[[nodiscard]] std::string worldPersistenceSummary(const StoredWorldInfo& world);

/// @brief Describes storage scope and legacy fallback status if applicable
[[nodiscard]] std::string worldStorageScopeSummary(const StoredWorldInfo& world);

/// @brief Checks if world has incomplete storage (missing profile or checkpoint)
[[nodiscard]] bool worldHasStorageIncomplete(const StoredWorldInfo& world);

/// @brief Checks if world was modified within the given time window
[[nodiscard]] bool worldIsRecentlyActive(
    const StoredWorldInfo& world,
    const std::chrono::hours window = std::chrono::hours(72));

/// @brief Generates a unique path by appending "_copy" or "_copy_N" if destination exists
[[nodiscard]] std::filesystem::path uniquePathWithCopySuffix(const std::filesystem::path& destination);

} // namespace ws::gui::session_wizard_helpers
