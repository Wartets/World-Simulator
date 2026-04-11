#pragma once

// Private implementation details for RuntimeService.
// This header is internal and should NOT be included by external code.
// Use the public runtime_service.hpp interface instead.

#include <filesystem>
#include <string>
#include <cstdint>
#include <vector>

namespace ws::gui::detail {

// Internal representation of world metadata exposed to the GUI.
// Kept private to allow future refactoring without breaking the public API.
struct WorldInfoDetail {
    std::string modelKey;
    std::string worldName;
    std::filesystem::path profilePath;
    std::filesystem::path checkpointPath;
    std::filesystem::path displayPrefsPath;
    bool hasProfile = false;
    bool hasCheckpoint = false;
    bool hasDisplayPrefs = false;
    std::uint32_t gridWidth = 0;
    std::uint32_t gridHeight = 0;
    std::uint64_t seed = 0;
    std::string temporalPolicy;
    std::string initialConditionMode;
    std::uintmax_t profileBytes = 0;
    std::uintmax_t checkpointBytes = 0;
    std::uintmax_t displayPrefsBytes = 0;
    std::filesystem::file_time_type profileLastWrite{};
    std::filesystem::file_time_type checkpointLastWrite{};
    std::filesystem::file_time_type displayPrefsLastWrite{};
    bool hasProfileTimestamp = false;
    bool hasCheckpointTimestamp = false;
    bool hasDisplayPrefsTimestamp = false;
    std::uint64_t stepIndex = 0;
    std::uint64_t runIdentityHash = 0;
    bool profileUsesFallback = false;
    bool checkpointUsesFallback = false;
    bool displayPrefsUsesFallback = false;

    [[nodiscard]] bool usesLegacyFallback() const noexcept {
        return profileUsesFallback || checkpointUsesFallback || displayPrefsUsesFallback;
    }
};

// Internal checkpoint metadata exposed to the GUI.
// Kept private to allow future query result refactoring.
struct CheckpointInfoDetail {
    std::string label;
    std::uint64_t stepIndex = 0;
    std::uint64_t timestampTicks = 0;
    std::uint64_t stateHash = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t runIdentityHash = 0;
    std::uint64_t profileFingerprint = 0;
};

} // namespace ws::gui::detail

