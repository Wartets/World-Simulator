#pragma once

// App dependencies
#include "ws/app/profile_store.hpp"
#include "ws/core/runtime.hpp"

// Standard library
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ws::app {

// =============================================================================
// World Scope Key
// =============================================================================

// Key for identifying a world within a model scope.
struct WorldScopeKey {
    std::string modelKey;
    std::string worldName;
};

// =============================================================================
// World Model Metadata
// =============================================================================

// Metadata about a model associated with a world.
struct WorldModelMetadata {
    std::string modelKey;
    std::string modelId;
    std::string modelName;
    std::string modelPath;
    std::string modelIdentityHash;
};

// =============================================================================
// Stored World Record
// =============================================================================

// Record describing a stored world.
struct StoredWorldRecord {
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
    std::uint64_t profileFingerprint = 0;
    bool profileUsesFallback = false;
    bool checkpointUsesFallback = false;
    bool displayPrefsUsesFallback = false;
};

// =============================================================================
// World Store
// =============================================================================

// Manages persistence of worlds (saved simulations) including
// profiles, checkpoints, and metadata.
class WorldStore {
public:
    // Constructs a world store with the given root directories.
    WorldStore(
        std::filesystem::path worldProfileRoot = std::filesystem::path("checkpoints") / "world_profiles",
        std::filesystem::path worldCheckpointRoot = std::filesystem::path("checkpoints") / "worlds");

    // Lists all stored worlds for a model key.
    [[nodiscard]] std::vector<StoredWorldRecord> list(const std::string& modelKey, std::string& message) const;
    // Suggests a name for the next new world.
    [[nodiscard]] std::string suggestNextWorldName(const std::string& modelKey) const;
    // Suggests a world name based on a hint.
    [[nodiscard]] std::string suggestWorldNameFromHint(const std::string& hint, const std::string& modelKey) const;
    // Normalizes a world name for display in the UI.
    [[nodiscard]] std::string normalizeNameForUi(std::string worldName) const;

    // Deletes a world.
    bool erase(const std::string& worldName, const std::string& modelKey, std::string& message) const;
    // Renames a world.
    bool rename(const std::string& fromWorldName, const std::string& toWorldName, const std::string& modelKey, std::string& message) const;
    // Duplicates a world.
    bool duplicate(const std::string& fromWorldName, const std::string& toWorldName, const std::string& modelKey, std::string& message) const;

    // Exports a world to an external file.
    bool exportWorld(
        const std::string& worldName,
        const std::filesystem::path& outputPath,
        const std::string& modelKey,
        const WorldModelMetadata& modelMetadata,
        std::string& message) const;
    // Imports a world from an external file.
    bool importWorld(
        const std::filesystem::path& inputPath,
        const std::string& modelKey,
        const WorldModelMetadata& expectedModelMetadata,
        std::string& importedWorldName,
        std::string& message) const;

    // Returns the profile path for a world.
    [[nodiscard]] std::filesystem::path profilePathFor(const std::string& worldName, const std::string& modelKey) const;
    // Returns the checkpoint path for a world.
    [[nodiscard]] std::filesystem::path checkpointPathFor(const std::string& worldName, const std::string& modelKey) const;
    // Returns the display preferences path for a world.
    [[nodiscard]] std::filesystem::path displayPrefsPathFor(const std::string& worldName, const std::string& modelKey) const;
    // Returns the canonical checkpoint write path without legacy fallback.
    [[nodiscard]] std::filesystem::path writeCheckpointPathFor(const std::string& worldName, const std::string& modelKey) const;

    // Checks if a world exists.
    [[nodiscard]] bool worldExists(const std::string& worldName, const std::string& modelKey) const;

private:
    // Normalizes a scope key for file system use.
    [[nodiscard]] static std::string normalizeScopeKey(std::string key);
    // Normalizes a world name for file system use.
    [[nodiscard]] static std::string normalizeWorldName(std::string worldName);
    // Checks if a name is a default world name pattern.
    [[nodiscard]] static bool isDefaultWorldName(const std::string& name, int& outIndex);

    // Returns the profile root for a model scope.
    [[nodiscard]] std::filesystem::path scopedProfileRoot(const std::string& modelKey) const;
    // Returns the checkpoint root for a model scope.
    [[nodiscard]] std::filesystem::path scopedCheckpointRoot(const std::string& modelKey) const;
    // Returns the canonical profile write path for a model scope.
    [[nodiscard]] std::filesystem::path writeProfilePathFor(const std::string& worldName, const std::string& modelKey) const;
    // Returns the canonical display-preferences write path for a model scope.
    [[nodiscard]] std::filesystem::path writeDisplayPrefsPathFor(const std::string& worldName, const std::string& modelKey) const;

    // Copies a file if it exists.
    bool copyFileIfExists(
        const std::filesystem::path& source,
        const std::filesystem::path& target,
        std::string& message) const;

    std::filesystem::path profileRoot_;
    std::filesystem::path checkpointRoot_;
    ProfileStore worldProfileStore_;
};

} // namespace ws::app
