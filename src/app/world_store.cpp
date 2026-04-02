#include "ws/app/world_store.hpp"

#include "ws/app/checkpoint_io.hpp"
#include "ws/app/shell_support.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ws::app {

namespace {

std::string sanitizeScopeKey(std::string key) {
    key = trim(std::move(key));
    if (key.empty()) {
        return {};
    }

    for (char& ch : key) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!allowed) {
            ch = '_';
        }
    }
    return key;
}

bool writeStringBlob(std::ostream& output, const std::string& value) {
    const std::uint64_t size = static_cast<std::uint64_t>(value.size());
    output.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (!output.good()) {
        return false;
    }
    if (!value.empty()) {
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
    }
    return output.good();
}

std::optional<std::string> readStringBlob(std::istream& input) {
    std::uint64_t size = 0;
    input.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!input.good()) {
        return std::nullopt;
    }

    std::string value(size, '\0');
    if (size > 0) {
        input.read(value.data(), static_cast<std::streamsize>(size));
        if (!input.good()) {
            return std::nullopt;
        }
    }
    return value;
}

std::string trimCopy(std::string value) {
    return trim(std::move(value));
}

bool hasModelCompatibilityConflict(
    const WorldModelMetadata& source,
    const WorldModelMetadata& target,
    std::string& reason) {
    const auto sourceIdentity = trimCopy(source.modelIdentityHash);
    const auto targetIdentity = trimCopy(target.modelIdentityHash);
    if (!sourceIdentity.empty() && !targetIdentity.empty() && sourceIdentity != targetIdentity) {
        reason = "identity_hash_mismatch";
        return true;
    }

    const auto sourceId = trimCopy(source.modelId);
    const auto targetId = trimCopy(target.modelId);
    if (!sourceId.empty() && !targetId.empty() && sourceId != targetId) {
        reason = "model_id_mismatch";
        return true;
    }

    return false;
}

} // namespace

WorldStore::WorldStore(std::filesystem::path worldProfileRoot, std::filesystem::path worldCheckpointRoot)
    : profileRoot_(std::move(worldProfileRoot)),
      checkpointRoot_(std::move(worldCheckpointRoot)),
      worldProfileStore_(profileRoot_) {}

std::string WorldStore::normalizeScopeKey(std::string key) {
    return sanitizeScopeKey(std::move(key));
}

std::string WorldStore::normalizeWorldName(std::string worldName) {
    worldName = trim(std::move(worldName));
    if (worldName.empty()) {
        return {};
    }

    for (char& ch : worldName) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!allowed) {
            ch = '_';
        }
    }
    return worldName;
}

std::filesystem::path WorldStore::scopedProfileRoot(const std::string& modelKey) const {
    const auto normalized = normalizeScopeKey(modelKey);
    return normalized.empty() ? profileRoot_ : (profileRoot_ / normalized);
}

std::filesystem::path WorldStore::scopedCheckpointRoot(const std::string& modelKey) const {
    const auto normalized = normalizeScopeKey(modelKey);
    return normalized.empty() ? checkpointRoot_ : (checkpointRoot_ / normalized);
}

std::filesystem::path resolveWithScopedFallback(
    const std::filesystem::path& scopedPath,
    const std::filesystem::path& fallbackPath) {
    if (std::filesystem::exists(scopedPath)) {
        return scopedPath;
    }
    if (std::filesystem::exists(fallbackPath)) {
        return fallbackPath;
    }
    return scopedPath;
}

std::string WorldStore::normalizeNameForUi(std::string worldName) const {
    return normalizeWorldName(std::move(worldName));
}

bool WorldStore::isDefaultWorldName(const std::string& name, int& outIndex) {
    if (name.size() != 10 || name.rfind("world_", 0) != 0) {
        return false;
    }

    int value = 0;
    for (std::size_t i = 6; i < name.size(); ++i) {
        const char ch = name[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = (value * 10) + static_cast<int>(ch - '0');
    }

    outIndex = value;
    return true;
}

std::filesystem::path WorldStore::profilePathFor(const std::string& worldName, const std::string& modelKey) const {
    const auto normalized = normalizeWorldName(worldName);
    if (normalized.empty()) {
        return {};
    }
    return resolveWithScopedFallback(
        worldProfileStore_.pathFor(normalized, modelKey),
        worldProfileStore_.pathFor(normalized));
}

std::filesystem::path WorldStore::checkpointPathFor(const std::string& worldName, const std::string& modelKey) const {
    const auto normalized = normalizeWorldName(worldName);
    if (normalized.empty()) {
        return {};
    }
    return resolveWithScopedFallback(
        scopedCheckpointRoot(modelKey) / (normalized + ".wscp"),
        checkpointRoot_ / (normalized + ".wscp"));
}

std::filesystem::path WorldStore::displayPrefsPathFor(const std::string& worldName, const std::string& modelKey) const {
    const auto normalized = normalizeWorldName(worldName);
    if (normalized.empty()) {
        return {};
    }
    return resolveWithScopedFallback(
        scopedCheckpointRoot(modelKey) / (normalized + ".displayprefs"),
        checkpointRoot_ / (normalized + ".displayprefs"));
}

bool WorldStore::worldExists(const std::string& worldName, const std::string& modelKey) const {
    const auto profilePath = profilePathFor(worldName, modelKey);
    const auto checkpointPath = checkpointPathFor(worldName, modelKey);
    return (!profilePath.empty() && std::filesystem::exists(profilePath)) ||
           (!checkpointPath.empty() && std::filesystem::exists(checkpointPath));
}

std::vector<StoredWorldRecord> WorldStore::list(const std::string& modelKey, std::string& message) const {
    std::vector<StoredWorldRecord> worlds;

    try {
        auto names = worldProfileStore_.list(modelKey);
        if (names.empty() && !normalizeScopeKey(modelKey).empty()) {
            names = worldProfileStore_.list();
        }
        worlds.reserve(names.size());

        for (const auto& worldName : names) {
            StoredWorldRecord record;
            record.modelKey = normalizeScopeKey(modelKey);
            if (record.modelKey.empty()) {
                record.modelKey = "default";
            }
            record.worldName = worldName;
            record.profilePath = profilePathFor(worldName, modelKey);
            record.checkpointPath = checkpointPathFor(worldName, modelKey);
            record.hasProfile = std::filesystem::exists(record.profilePath);
            record.hasCheckpoint = std::filesystem::exists(record.checkpointPath);

            if (record.hasProfile) {
                record.profileBytes = std::filesystem::file_size(record.profilePath);
                record.profileLastWrite = std::filesystem::last_write_time(record.profilePath);
                record.hasProfileTimestamp = true;
                try {
                    const auto launch = worldProfileStore_.load(worldName, modelKey);
                    record.gridWidth = launch.grid.width;
                    record.gridHeight = launch.grid.height;
                    record.seed = launch.seed;
                    record.temporalPolicy = temporalPolicyToString(launch.temporalPolicy);
                    switch (launch.initialConditions.type) {
                        case InitialConditionType::Terrain: record.initialConditionMode = "terrain"; break;
                        case InitialConditionType::Conway: record.initialConditionMode = "conway"; break;
                        case InitialConditionType::GrayScott: record.initialConditionMode = "gray_scott"; break;
                        case InitialConditionType::Waves: record.initialConditionMode = "waves"; break;
                        case InitialConditionType::Blank: record.initialConditionMode = "blank"; break;
                        default: record.initialConditionMode = "unknown"; break;
                    }
                } catch (...) {
                    // keep list operation resilient
                }
            }

            if (record.hasCheckpoint) {
                record.checkpointBytes = std::filesystem::file_size(record.checkpointPath);
                record.checkpointLastWrite = std::filesystem::last_write_time(record.checkpointPath);
                record.hasCheckpointTimestamp = true;

                try {
                    const auto checkpoint = readCheckpointFile(record.checkpointPath);
                    record.stepIndex = checkpoint.stateSnapshot.header.stepIndex;
                    record.runIdentityHash = checkpoint.runSignature.identityHash();
                    record.profileFingerprint = checkpoint.profileFingerprint;
                } catch (...) {
                    // keep list operation resilient
                }
            }

            worlds.push_back(std::move(record));
        }

        std::sort(worlds.begin(), worlds.end(), [](const StoredWorldRecord& lhs, const StoredWorldRecord& rhs) {
            return lhs.worldName < rhs.worldName;
        });

        message = "world_list_ready count=" + std::to_string(worlds.size());
    } catch (const std::exception& exception) {
        worlds.clear();
        message = std::string("world_list_failed error=") + exception.what();
    }

    return worlds;
}

std::string WorldStore::suggestNextWorldName(const std::string& modelKey) const {
    const auto names = worldProfileStore_.list(modelKey);
    int maxIndex = 0;
    for (const auto& name : names) {
        int value = 0;
        if (isDefaultWorldName(name, value)) {
            maxIndex = std::max(maxIndex, value);
        }
    }

    for (int i = maxIndex + 1; i < 100000; ++i) {
        std::ostringstream out;
        out << "world_" << std::setw(4) << std::setfill('0') << i;
        const std::string candidate = out.str();
        if (!worldExists(candidate, modelKey)) {
            return candidate;
        }
    }

    return "world_99999";
}

std::string WorldStore::suggestWorldNameFromHint(const std::string& hint, const std::string& modelKey) const {
    std::string base = normalizeWorldName(hint);
    if (base.empty()) {
        return suggestNextWorldName(modelKey);
    }

    while (!base.empty() && base.back() == '_') {
        base.pop_back();
    }
    if (base.empty()) {
        return suggestNextWorldName(modelKey);
    }

    std::string lower = base;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lower == "world" || lower == "new_world") {
        return suggestNextWorldName(modelKey);
    }

    auto trimNumericSuffix = [](std::string value) {
        while (!value.empty() && std::isdigit(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
        while (!value.empty() && (value.back() == '_' || value.back() == '-')) {
            value.pop_back();
        }
        return value;
    };

    base = trimNumericSuffix(base);
    if (base.empty()) {
        return suggestNextWorldName(modelKey);
    }

    if (!worldExists(base, modelKey)) {
        return base;
    }

    for (int i = 2; i < 100000; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (!worldExists(candidate, modelKey)) {
            return candidate;
        }
    }

    return suggestNextWorldName(modelKey);
}

bool WorldStore::copyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& target, std::string& message) const {
    if (!std::filesystem::exists(source)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        message = "world_copy_failed error=mkdir_failed path=" + target.parent_path().string();
        return false;
    }

    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        message = "world_copy_failed error=copy_failed source=" + source.string() + " target=" + target.string();
        return false;
    }

    return true;
}

bool WorldStore::erase(const std::string& worldName, const std::string& modelKey, std::string& message) const {
    const auto normalized = normalizeWorldName(worldName);
    if (normalized.empty()) {
        message = "world_delete_failed error=invalid_world_name";
        return false;
    }

    bool removedAny = false;
    try {
        const auto profilePath = profilePathFor(normalized, modelKey);
        const auto checkpointPath = checkpointPathFor(normalized, modelKey);
        const auto displayPath = displayPrefsPathFor(normalized, modelKey);

        if (!profilePath.empty() && std::filesystem::exists(profilePath)) {
            removedAny = std::filesystem::remove(profilePath) || removedAny;
        }
        if (!checkpointPath.empty() && std::filesystem::exists(checkpointPath)) {
            removedAny = std::filesystem::remove(checkpointPath) || removedAny;
        }
        if (!displayPath.empty() && std::filesystem::exists(displayPath)) {
            removedAny = std::filesystem::remove(displayPath) || removedAny;
        }

        if (!removedAny) {
            message = "world_delete_noop name=" + normalized;
            return false;
        }

        message = "world_deleted name=" + normalized;
        return true;
    } catch (const std::exception& exception) {
        message = std::string("world_delete_failed error=") + exception.what();
        return false;
    }
}

bool WorldStore::rename(const std::string& fromWorldName, const std::string& toWorldName, const std::string& modelKey, std::string& message) const {
    const auto from = normalizeWorldName(fromWorldName);
    const auto to = normalizeWorldName(toWorldName);
    if (from.empty() || to.empty() || from == to) {
        message = "world_rename_failed error=invalid_name";
        return false;
    }
    if (!worldExists(from, modelKey)) {
        message = "world_rename_failed error=source_missing";
        return false;
    }
    if (worldExists(to, modelKey)) {
        message = "world_rename_failed error=target_exists";
        return false;
    }

    std::string copyMessage;
    if (!duplicate(from, to, modelKey, copyMessage)) {
        message = copyMessage;
        return false;
    }

    std::string deleteMessage;
    if (!erase(from, modelKey, deleteMessage)) {
        message = "world_rename_partial source=" + from + " target=" + to + " detail=" + deleteMessage;
        return false;
    }

    message = "world_renamed from=" + from + " to=" + to;
    return true;
}

bool WorldStore::duplicate(const std::string& fromWorldName, const std::string& toWorldName, const std::string& modelKey, std::string& message) const {
    const auto from = normalizeWorldName(fromWorldName);
    const auto to = normalizeWorldName(toWorldName);
    if (from.empty() || to.empty() || from == to) {
        message = "world_duplicate_failed error=invalid_name";
        return false;
    }
    if (!worldExists(from, modelKey)) {
        message = "world_duplicate_failed error=source_missing";
        return false;
    }
    if (worldExists(to, modelKey)) {
        message = "world_duplicate_failed error=target_exists";
        return false;
    }

    const auto fromProfile = profilePathFor(from, modelKey);
    const auto fromCheckpoint = checkpointPathFor(from, modelKey);
    const auto fromDisplay = displayPrefsPathFor(from, modelKey);

    const auto toProfile = profilePathFor(to, modelKey);
    const auto toCheckpoint = checkpointPathFor(to, modelKey);
    const auto toDisplay = displayPrefsPathFor(to, modelKey);

    if (!copyFileIfExists(fromProfile, toProfile, message) ||
        !copyFileIfExists(fromCheckpoint, toCheckpoint, message) ||
        !copyFileIfExists(fromDisplay, toDisplay, message)) {
        return false;
    }

    message = "world_duplicated source=" + from + " target=" + to;
    return true;
}

bool WorldStore::exportWorld(
    const std::string& worldName,
    const std::filesystem::path& outputPath,
    const std::string& modelKey,
    const WorldModelMetadata& modelMetadata,
    std::string& message) const {
    const auto normalized = normalizeWorldName(worldName);
    if (normalized.empty()) {
        message = "world_export_failed error=invalid_world_name";
        return false;
    }

    const auto profilePath = profilePathFor(normalized, modelKey);
    const auto checkpointPath = checkpointPathFor(normalized, modelKey);
    if (!std::filesystem::exists(profilePath)) {
        message = "world_export_failed error=missing_profile";
        return false;
    }

    std::ifstream profileInput(profilePath, std::ios::binary);
    if (!profileInput.is_open()) {
        message = "world_export_failed error=profile_open_failed";
        return false;
    }
    const std::string profileText((std::istreambuf_iterator<char>(profileInput)), std::istreambuf_iterator<char>());

    std::string checkpointBinary;
    if (std::filesystem::exists(checkpointPath)) {
        std::ifstream checkpointInput(checkpointPath, std::ios::binary);
        if (!checkpointInput.is_open()) {
            message = "world_export_failed error=checkpoint_open_failed";
            return false;
        }
        checkpointBinary.assign((std::istreambuf_iterator<char>(checkpointInput)), std::istreambuf_iterator<char>());
    }

    std::error_code ec;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            message = "world_export_failed error=mkdir_failed";
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        message = "world_export_failed error=output_open_failed";
        return false;
    }

    const std::uint64_t magic = 0x315850455753ull; // "WSEXP1"
    const std::uint32_t version = 3;
    output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    output.write(reinterpret_cast<const char*>(&version), sizeof(version));
    if (!writeStringBlob(output, normalizeScopeKey(modelKey)) ||
        !writeStringBlob(output, trimCopy(modelMetadata.modelId)) ||
        !writeStringBlob(output, trimCopy(modelMetadata.modelName)) ||
        !writeStringBlob(output, trimCopy(modelMetadata.modelPath)) ||
        !writeStringBlob(output, trimCopy(modelMetadata.modelIdentityHash))) {
        message = "world_export_failed error=write_failed";
        return false;
    }
    if (!writeStringBlob(output, normalized) ||
        !writeStringBlob(output, profileText) ||
        !writeStringBlob(output, checkpointBinary)) {
        message = "world_export_failed error=write_failed";
        return false;
    }

    message = "world_exported name=" + normalized + " path=" + outputPath.string();
    return true;
}

bool WorldStore::importWorld(
    const std::filesystem::path& inputPath,
    const std::string& modelKey,
    const WorldModelMetadata& expectedModelMetadata,
    std::string& importedWorldName,
    std::string& message) const {
    std::ifstream input(inputPath, std::ios::binary);
    if (!input.is_open()) {
        message = "world_import_failed error=input_open_failed";
        return false;
    }

    std::uint64_t magic = 0;
    std::uint32_t version = 0;
    input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!input.good() || magic != 0x315850455753ull || version < 1u || version > 3u) {
        message = "world_import_failed error=format_mismatch";
        return false;
    }

    WorldModelMetadata sourceMetadata;
    std::optional<std::string> storedName;
    if (version >= 3u) {
        const auto storedModelKey = readStringBlob(input);
        const auto storedModelId = readStringBlob(input);
        const auto storedModelName = readStringBlob(input);
        const auto storedModelPath = readStringBlob(input);
        const auto storedModelIdentityHash = readStringBlob(input);
        storedName = readStringBlob(input);

        if (!storedModelKey.has_value() ||
            !storedModelId.has_value() ||
            !storedModelName.has_value() ||
            !storedModelPath.has_value() ||
            !storedModelIdentityHash.has_value() ||
            !storedName.has_value()) {
            message = "world_import_failed error=payload_read_failed";
            return false;
        }

        sourceMetadata.modelKey = *storedModelKey;
        sourceMetadata.modelId = *storedModelId;
        sourceMetadata.modelName = *storedModelName;
        sourceMetadata.modelPath = *storedModelPath;
        sourceMetadata.modelIdentityHash = *storedModelIdentityHash;
    } else if (version >= 2u) {
        const auto storedModelKey = readStringBlob(input);
        storedName = readStringBlob(input);
        if (!storedModelKey.has_value() || !storedName.has_value()) {
            message = "world_import_failed error=payload_read_failed";
            return false;
        }
        sourceMetadata.modelKey = *storedModelKey;
    } else {
        storedName = readStringBlob(input);
    }
    const auto profileText = readStringBlob(input);
    const auto checkpointBinary = readStringBlob(input);
    if (!storedName.has_value() || !profileText.has_value() || !checkpointBinary.has_value()) {
        message = "world_import_failed error=payload_read_failed";
        return false;
    }

    auto candidate = normalizeWorldName(*storedName);
    if (candidate.empty()) {
        candidate = suggestNextWorldName(modelKey);
    }
    if (worldExists(candidate, modelKey)) {
        candidate = suggestNextWorldName(modelKey);
    }

    const std::string sourceModelKey = normalizeScopeKey(sourceMetadata.modelKey);
    const std::string targetModelKey = normalizeScopeKey(modelKey);

    std::string compatibilityReason;
    if (hasModelCompatibilityConflict(sourceMetadata, expectedModelMetadata, compatibilityReason)) {
        std::ostringstream error;
        error << "world_import_failed error=model_incompatible reason=" << compatibilityReason
              << " source_model_id=" << trimCopy(sourceMetadata.modelId)
              << " source_model_identity_hash=" << trimCopy(sourceMetadata.modelIdentityHash)
              << " target_model_id=" << trimCopy(expectedModelMetadata.modelId)
              << " target_model_identity_hash=" << trimCopy(expectedModelMetadata.modelIdentityHash);
        message = error.str();
        return false;
    }

    const auto profilePath = profilePathFor(candidate, modelKey);
    const auto checkpointPath = checkpointPathFor(candidate, modelKey);

    std::error_code ec;
    std::filesystem::create_directories(profilePath.parent_path(), ec);
    if (ec) {
        message = "world_import_failed error=mkdir_profile_failed";
        return false;
    }
    std::filesystem::create_directories(checkpointPath.parent_path(), ec);
    if (ec) {
        message = "world_import_failed error=mkdir_checkpoint_failed";
        return false;
    }

    {
        std::ofstream profileOutput(profilePath, std::ios::trunc);
        if (!profileOutput.is_open()) {
            message = "world_import_failed error=profile_write_open_failed";
            return false;
        }
        profileOutput << *profileText;
    }

    if (!checkpointBinary->empty()) {
        std::ofstream checkpointOutput(checkpointPath, std::ios::binary | std::ios::trunc);
        if (!checkpointOutput.is_open()) {
            message = "world_import_failed error=checkpoint_write_open_failed";
            return false;
        }
        checkpointOutput.write(checkpointBinary->data(), static_cast<std::streamsize>(checkpointBinary->size()));
        if (!checkpointOutput.good()) {
            message = "world_import_failed error=checkpoint_write_failed";
            return false;
        }
    }

    importedWorldName = candidate;
    message = "world_imported name=" + candidate + " source=" + inputPath.string() + " model=" + targetModelKey;
    if (!sourceModelKey.empty() && sourceModelKey != targetModelKey) {
        message += " source_model=" + sourceModelKey;
    }
    return true;
}

} // namespace ws::app
