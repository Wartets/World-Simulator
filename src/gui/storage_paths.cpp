#include "ws/gui/storage_paths.hpp"

#include <cstdlib>
#include <system_error>

namespace ws::gui::storage {
namespace {

[[nodiscard]] std::filesystem::path envPath(const char* key) {
    if (key == nullptr) {
        return {};
    }
    const char* value = std::getenv(key);
    if (value == nullptr || *value == '\0') {
        return {};
    }
    return std::filesystem::path(value);
}

} // namespace

std::filesystem::path resolveWorkspaceRootFromCurrentPath() {
    std::error_code ec;
    std::filesystem::path current = std::filesystem::current_path(ec);
    if (ec) {
        return std::filesystem::path{"."};
    }

    for (std::filesystem::path probe = current; !probe.empty(); probe = probe.parent_path()) {
        if (std::filesystem::exists(probe / "CMakeLists.txt")) {
            return probe;
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }

    return current;
}

bool isDevelopmentWorkspace(const std::filesystem::path& workspaceRoot) {
    if (workspaceRoot.empty()) {
        return false;
    }

    return std::filesystem::exists(workspaceRoot / "CMakeLists.txt") &&
           std::filesystem::exists(workspaceRoot / "src") &&
           std::filesystem::exists(workspaceRoot / "include");
}

std::filesystem::path resolveUserSettingsRoot(const std::string& appFolderName) {
#ifdef _WIN32
    std::filesystem::path base = envPath("APPDATA");
    if (base.empty()) {
        base = envPath("LOCALAPPDATA");
    }
    if (base.empty()) {
        const std::filesystem::path userProfile = envPath("USERPROFILE");
        if (!userProfile.empty()) {
            base = userProfile / "AppData" / "Roaming";
        }
    }
#elif defined(__APPLE__)
    std::filesystem::path base = envPath("HOME");
    if (!base.empty()) {
        base /= "Library";
        base /= "Application Support";
    }
#else
    std::filesystem::path base = envPath("XDG_CONFIG_HOME");
    if (base.empty()) {
        const std::filesystem::path home = envPath("HOME");
        if (!home.empty()) {
            base = home / ".config";
        }
    }
#endif

    if (base.empty()) {
        std::error_code ec;
        base = std::filesystem::current_path(ec);
        if (ec) {
            base = std::filesystem::path{"."};
        }
    }

    return base / appFolderName;
}

std::filesystem::path resolveProfilesPath(const std::string& appFolderName) {
    const std::filesystem::path workspaceRoot = resolveWorkspaceRootFromCurrentPath();

    std::filesystem::path profilesRoot;
    if (isDevelopmentWorkspace(workspaceRoot)) {
        profilesRoot = workspaceRoot / "profiles";
    } else {
        profilesRoot = resolveUserSettingsRoot(appFolderName) / "profiles";
    }

    std::error_code ec;
    std::filesystem::create_directories(profilesRoot, ec);
    return profilesRoot;
}

} // namespace ws::gui::storage
