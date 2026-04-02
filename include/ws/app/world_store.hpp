#pragma once

#include "ws/app/profile_store.hpp"
#include "ws/core/runtime.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ws::app {

struct WorldScopeKey {
    std::string modelKey;
    std::string worldName;
};

struct WorldModelMetadata {
    std::string modelKey;
    std::string modelId;
    std::string modelName;
    std::string modelPath;
    std::string modelIdentityHash;
};

struct StoredWorldRecord {
    std::string modelKey;
    std::string worldName;
    std::filesystem::path profilePath;
    std::filesystem::path checkpointPath;
    bool hasProfile = false;
    bool hasCheckpoint = false;
    std::uint32_t gridWidth = 0;
    std::uint32_t gridHeight = 0;
    std::uint64_t seed = 0;
    std::string temporalPolicy;
    std::string initialConditionMode;
    std::uintmax_t profileBytes = 0;
    std::uintmax_t checkpointBytes = 0;
    std::filesystem::file_time_type profileLastWrite{};
    std::filesystem::file_time_type checkpointLastWrite{};
    bool hasProfileTimestamp = false;
    bool hasCheckpointTimestamp = false;
    std::uint64_t stepIndex = 0;
    std::uint64_t runIdentityHash = 0;
    std::uint64_t profileFingerprint = 0;
};

class WorldStore {
public:
    WorldStore(
        std::filesystem::path worldProfileRoot = std::filesystem::path("checkpoints") / "world_profiles",
        std::filesystem::path worldCheckpointRoot = std::filesystem::path("checkpoints") / "worlds");

    [[nodiscard]] std::vector<StoredWorldRecord> list(const std::string& modelKey, std::string& message) const;
    [[nodiscard]] std::string suggestNextWorldName(const std::string& modelKey) const;
    [[nodiscard]] std::string suggestWorldNameFromHint(const std::string& hint, const std::string& modelKey) const;
    [[nodiscard]] std::string normalizeNameForUi(std::string worldName) const;

    bool erase(const std::string& worldName, const std::string& modelKey, std::string& message) const;
    bool rename(const std::string& fromWorldName, const std::string& toWorldName, const std::string& modelKey, std::string& message) const;
    bool duplicate(const std::string& fromWorldName, const std::string& toWorldName, const std::string& modelKey, std::string& message) const;

    bool exportWorld(
        const std::string& worldName,
        const std::filesystem::path& outputPath,
        const std::string& modelKey,
        const WorldModelMetadata& modelMetadata,
        std::string& message) const;
    bool importWorld(
        const std::filesystem::path& inputPath,
        const std::string& modelKey,
        const WorldModelMetadata& expectedModelMetadata,
        std::string& importedWorldName,
        std::string& message) const;

    [[nodiscard]] std::filesystem::path profilePathFor(const std::string& worldName, const std::string& modelKey) const;
    [[nodiscard]] std::filesystem::path checkpointPathFor(const std::string& worldName, const std::string& modelKey) const;
    [[nodiscard]] std::filesystem::path displayPrefsPathFor(const std::string& worldName, const std::string& modelKey) const;

    [[nodiscard]] bool worldExists(const std::string& worldName, const std::string& modelKey) const;

private:
    [[nodiscard]] static std::string normalizeScopeKey(std::string key);
    [[nodiscard]] static std::string normalizeWorldName(std::string worldName);
    [[nodiscard]] static bool isDefaultWorldName(const std::string& name, int& outIndex);

    [[nodiscard]] std::filesystem::path scopedProfileRoot(const std::string& modelKey) const;
    [[nodiscard]] std::filesystem::path scopedCheckpointRoot(const std::string& modelKey) const;

    bool copyFileIfExists(
        const std::filesystem::path& source,
        const std::filesystem::path& target,
        std::string& message) const;

    std::filesystem::path profileRoot_;
    std::filesystem::path checkpointRoot_;
    ProfileStore worldProfileStore_;
};

} // namespace ws::app
