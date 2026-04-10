#include "ws/gui/main_window/platform_dialogs.hpp"

#ifdef _WIN32
#include <windows.h>
#include <algorithm>
#include <cstring>
#endif

namespace ws::gui::platform_dialogs {

std::wstring utf8ToWide(const std::string& value) {
#ifdef _WIN32
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), size);
    return output;
#else
    return {};
#endif
}

std::optional<std::filesystem::path> pickNativeFilePath(
    const wchar_t* dialogTitle,
    const wchar_t* filter,
    const std::filesystem::path& defaultPath,
    bool saveDialog) {
#ifdef _WIN32
    wchar_t fileBuffer[MAX_PATH] = {};
    std::wstring initialFile = utf8ToWide(defaultPath.filename().string());
    if (!initialFile.empty()) {
        const auto copyCount = std::min<std::size_t>(initialFile.size(), static_cast<std::size_t>(MAX_PATH - 1));
        std::wmemcpy(fileBuffer, initialFile.c_str(), copyCount);
        fileBuffer[copyCount] = L'\0';
    }

    const std::wstring initialDir = utf8ToWide(defaultPath.parent_path().string());

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrTitle = dialogTitle;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (saveDialog) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn)) {
            return std::nullopt;
        }
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) {
            return std::nullopt;
        }
    }

    return std::filesystem::path(fileBuffer);
#else
    (void)dialogTitle;
    (void)filter;
    (void)defaultPath;
    (void)saveDialog;
    return std::nullopt;
#endif
}

} // namespace ws::gui::platform_dialogs
