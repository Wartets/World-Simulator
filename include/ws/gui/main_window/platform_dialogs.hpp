#pragma once

#include <string>
#include <optional>
#include <filesystem>

namespace ws::gui::platform_dialogs {

/// @brief Converts UTF-8 string to wide (UTF-16) string for Windows API calls.
/// Used by file dialog functions on Windows.
/// @return Empty string if conversion fails
std::wstring utf8ToWide(const std::string& value);

/// @brief Opens a native file picker dialog (Windows only).
/// @param dialogTitle Title shown in the file dialog window
/// @param filter File type filter string (Windows format: "Name\0*.ext\0\0")
/// @param defaultPath Initial directory and optional filename
/// @param saveDialog If true, opens save dialog; if false, opens open dialog
/// @return Selected path if user confirmed, std::nullopt if cancelled or error
std::optional<std::filesystem::path> pickNativeFilePath(
    const wchar_t* dialogTitle,
    const wchar_t* filter,
    const std::filesystem::path& defaultPath,
    bool saveDialog);

} // namespace ws::gui::platform_dialogs
