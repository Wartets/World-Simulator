#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& value) {
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
}

std::optional<std::filesystem::path> pickNativeFilePath(
    const wchar_t* dialogTitle,
    const wchar_t* filter,
    const std::filesystem::path& defaultPath,
    const bool saveDialog) {
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
}
#else
std::optional<std::filesystem::path> pickNativeFilePath(
    const wchar_t*,
    const wchar_t*,
    const std::filesystem::path&,
    const bool) {
    return std::nullopt;
}
#endif

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
