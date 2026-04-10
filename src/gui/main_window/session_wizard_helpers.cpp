#include "ws/gui/main_window/session_wizard_helpers.hpp"
#include "ws/gui/session_manager/session_manager.hpp"
#include <sstream>
#include <cstring>

namespace ws::gui::session_wizard_helpers {

const char* worldStorageStatusLabel(const StoredWorldInfo& world) {
    if (!world.hasProfile && !world.hasCheckpoint) {
        return "Storage incomplete";
    }
    if (world.hasProfile && world.hasCheckpoint) {
        return world.usesLegacyFallback() ? "Ready to resume (legacy path)" : "Ready to resume";
    }
    if (world.hasProfile) {
        return "Opens from profile only";
    }
    return "Checkpoint without profile";
}

ImVec4 worldStorageStatusColor(const StoredWorldInfo& world) {
    if (!world.hasProfile && !world.hasCheckpoint) {
        return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
    }
    if (world.hasProfile && world.hasCheckpoint && !world.usesLegacyFallback()) {
        return ImVec4(0.58f, 0.88f, 0.62f, 1.0f);
    }
    if (world.hasProfile && world.hasCheckpoint) {
        return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
    }
    if (world.hasProfile) {
        return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
    }
    return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
}

std::string worldResumeSummary(const StoredWorldInfo& world) {
    std::ostringstream out;
    if (world.hasCheckpoint) {
        out << "Open restores the checkpointed runtime state";
        if (world.stepIndex > 0) {
            out << " at step " << world.stepIndex;
        }
        if (world.hasCheckpointTimestamp) {
            out << " saved " << session_manager::formatFileTime(world.checkpointLastWrite, true);
        }
        out << ".";
        return out.str();
    }
    if (world.hasProfile) {
        out << "Open rebuilds the world from profile settings";
        if (world.hasProfileTimestamp) {
            out << " saved " << session_manager::formatFileTime(world.profileLastWrite, true);
        }
        out << ". No checkpointed runtime state is available.";
        return out.str();
    }
    return "This record is missing both profile and checkpoint data and may not open successfully.";
}

std::string worldPersistenceSummary(const StoredWorldInfo& world) {
    std::ostringstream out;
    out << (world.hasProfile ? "Profile saved" : "Profile missing")
        << " | "
        << (world.hasCheckpoint ? "Checkpoint saved" : "Checkpoint missing")
        << " | "
        << (world.hasDisplayPrefs ? "View layout saved" : "No saved view layout");
    return out.str();
}

std::string worldStorageScopeSummary(const StoredWorldInfo& world) {
    if (world.usesLegacyFallback()) {
        std::vector<std::string> fallbackParts;
        if (world.profileUsesFallback) {
            fallbackParts.push_back("profile");
        }
        if (world.checkpointUsesFallback) {
            fallbackParts.push_back("checkpoint");
        }
        if (world.displayPrefsUsesFallback) {
            fallbackParts.push_back("view layout");
        }

        std::ostringstream out;
        out << "Uses legacy fallback path for ";
        for (std::size_t i = 0; i < fallbackParts.size(); ++i) {
            if (i > 0) {
                out << (i + 1 == fallbackParts.size() ? " and " : ", ");
            }
            out << fallbackParts[i];
        }
        out << ".";
        return out.str();
    }

    if (world.modelKey.empty() || world.modelKey == "default") {
        return "Stored in the default workspace scope.";
    }

    return "Stored under the active model scope.";
}

bool worldHasStorageIncomplete(const StoredWorldInfo& world) {
    return !(world.hasProfile && world.hasCheckpoint);
}

bool worldIsRecentlyActive(
    const StoredWorldInfo& world,
    const std::chrono::hours window) {
    bool hasAnyTimestamp = false;
    std::filesystem::file_time_type latest{};

    if (world.hasProfileTimestamp) {
        latest = world.profileLastWrite;
        hasAnyTimestamp = true;
    }
    if (world.hasCheckpointTimestamp && (!hasAnyTimestamp || world.checkpointLastWrite > latest)) {
        latest = world.checkpointLastWrite;
        hasAnyTimestamp = true;
    }
    if (world.hasDisplayPrefsTimestamp && (!hasAnyTimestamp || world.displayPrefsLastWrite > latest)) {
        latest = world.displayPrefsLastWrite;
        hasAnyTimestamp = true;
    }

    if (!hasAnyTimestamp) {
        return false;
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    return latest + window >= now;
}

std::filesystem::path uniquePathWithCopySuffix(const std::filesystem::path& destination) {
    if (destination.empty() || !std::filesystem::exists(destination)) {
        return destination;
    }

    const std::filesystem::path parent = destination.parent_path();
    const std::string stem = destination.stem().string();
    const std::filesystem::path extension = destination.extension();
    int copyIndex = 1;
    for (;;) {
        const std::string suffix = copyIndex == 1 ? "_copy" : ("_copy_" + std::to_string(copyIndex));
        const std::filesystem::path candidate = parent / (stem + suffix + extension.string());
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
        ++copyIndex;
    }
}

} // namespace ws::gui::session_wizard_helpers
