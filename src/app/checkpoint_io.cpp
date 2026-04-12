#include "ws/app/checkpoint_io.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace ws::app {

namespace {

// Checkpoint file magic number and format version constants.
constexpr std::uint64_t kCheckpointMagic = 0x315650435357ull; // "WSCPV1"
constexpr std::uint32_t kCheckpointFormatVersion = 4;

// Writes a plain-old-data value to the output stream in binary form.
template <typename T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) {
        throw std::runtime_error("failed to write binary payload");
    }
}

// Reads a plain-old-data value from the input stream in binary form.
template <typename T>
T readPod(std::istream& input) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) {
        throw std::runtime_error("failed to read binary payload");
    }
    return value;
}

// Writes a string by first writing its length as a 64-bit integer, then the data.
void writeString(std::ostream& output, const std::string& value) {
    const auto size = static_cast<std::uint64_t>(value.size());
    writePod(output, size);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) {
        throw std::runtime_error("failed to write string payload");
    }
}

// Reads a string by first reading its length, then the data.
std::string readString(std::istream& input) {
    const auto size = readPod<std::uint64_t>(input);
    std::string value(size, '\0');
    if (size > 0) {
        input.read(value.data(), static_cast<std::streamsize>(size));
        if (!input) {
            throw std::runtime_error("failed to read string payload");
        }
    }
    return value;
}

} // namespace

void writeCheckpointFile(const RuntimeCheckpoint& checkpoint, const std::filesystem::path& path) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::runtime_error("failed to create checkpoint directory: " + parent.string());
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("failed to open checkpoint file for writing: " + path.string());
    }

    writePod(output, kCheckpointMagic);
    writePod(output, kCheckpointFormatVersion);

    const auto& signature = checkpoint.runSignature;
    writePod(output, signature.globalSeed());
    writeString(output, signature.initializationParameterHash());
    writePod(output, signature.grid().width);
    writePod(output, signature.grid().height);
    writePod(output, signature.boundaryMode());
    writePod(output, signature.unitRegime());
    writePod(output, signature.temporalPolicy());
    writeString(output, signature.timeIntegratorId());
    writeString(output, signature.eventTimelineHash());
    writeString(output, signature.activeSubsystemSetHash());
    writePod(output, signature.profileFingerprint());
    writePod(output, signature.compatibilityFingerprint());
    writePod(output, signature.identityHash());

    writePod(output, checkpoint.profileFingerprint);

    std::vector<std::pair<std::string, std::uint32_t>> cadenceEntries;
    cadenceEntries.reserve(checkpoint.variableCheckpointIntervalSteps.size());
    for (const auto& [variableName, interval] : checkpoint.variableCheckpointIntervalSteps) {
        cadenceEntries.emplace_back(variableName, interval);
    }
    std::sort(cadenceEntries.begin(), cadenceEntries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    writePod(output, static_cast<std::uint8_t>(checkpoint.checkpointIncludeUnspecifiedVariables ? 1u : 0u));
    writePod(output, static_cast<std::uint64_t>(cadenceEntries.size()));
    for (const auto& [variableName, interval] : cadenceEntries) {
        writeString(output, variableName);
        writePod(output, interval);
    }

    const auto& snapshot = checkpoint.stateSnapshot;
    writePod(output, snapshot.header.stepIndex);
    writePod(output, snapshot.header.timestampTicks);
    writePod(output, snapshot.header.status);
    writePod(output, snapshot.grid.width);
    writePod(output, snapshot.grid.height);
    writePod(output, snapshot.boundaryMode);
    writePod(output, snapshot.topologyBackend);
    writePod(output, snapshot.memoryLayout.alignmentBytes);
    writePod(output, snapshot.memoryLayout.tileWidth);
    writePod(output, snapshot.memoryLayout.tileHeight);
    writePod(output, snapshot.runIdentityHash);
    writePod(output, snapshot.profileFingerprint);
    writeString(output, snapshot.checkpointLabel);
    writePod(output, snapshot.payloadBytes);
    writePod(output, snapshot.stateHash);

    writePod(output, static_cast<std::uint64_t>(snapshot.fields.size()));
    for (const auto& field : snapshot.fields) {
        writePod(output, field.spec.id);
        writeString(output, field.spec.name);
        writePod(output, field.spec.dataType);

        writePod(output, static_cast<std::uint64_t>(field.values.size()));
        if (!field.values.empty()) {
            output.write(
                reinterpret_cast<const char*>(field.values.data()),
                static_cast<std::streamsize>(field.values.size() * sizeof(float)));
            if (!output) {
                throw std::runtime_error("failed to write field values");
            }
        }

        writePod(output, static_cast<std::uint64_t>(field.validityMask.size()));
        if (!field.validityMask.empty()) {
            output.write(
                reinterpret_cast<const char*>(field.validityMask.data()),
                static_cast<std::streamsize>(field.validityMask.size()));
            if (!output) {
                throw std::runtime_error("failed to write validity mask");
            }
        }

        writePod(output, static_cast<std::uint64_t>(field.sparseOverlay.size()));
        for (const auto& [index, value] : field.sparseOverlay) {
            writePod(output, index);
            writePod(output, value);
        }
    }

    writePod(output, static_cast<std::uint64_t>(checkpoint.manualEventLog.size()));
    for (const auto& manualEvent : checkpoint.manualEventLog) {
        writePod(output, manualEvent.step);
        writePod(output, manualEvent.time);
        writeString(output, manualEvent.variable);
        writePod(output, manualEvent.cellIndex);
        writePod(output, manualEvent.oldValue);
        writePod(output, manualEvent.newValue);
        writeString(output, manualEvent.description);
        writePod(output, manualEvent.timestamp);
        writePod(output, manualEvent.kind);
    }
}

RuntimeCheckpoint readCheckpointFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open checkpoint file for reading: " + path.string());
    }

    const auto magic = readPod<std::uint64_t>(input);
    if (magic != kCheckpointMagic) {
        throw std::runtime_error("invalid checkpoint file magic");
    }

    const auto version = readPod<std::uint32_t>(input);
    if (version != 1u && version != 2u && version != 3u && version != kCheckpointFormatVersion) {
        throw std::runtime_error("unsupported checkpoint file version");
    }

    const auto globalSeed = readPod<std::uint64_t>(input);
    const auto initializationParameterHash = readString(input);
    const auto gridWidth = readPod<std::uint32_t>(input);
    const auto gridHeight = readPod<std::uint32_t>(input);
    const auto boundaryMode = readPod<BoundaryMode>(input);
    const auto unitRegime = readPod<UnitRegime>(input);
    const auto temporalPolicy = readPod<TemporalPolicy>(input);
    std::string timeIntegratorId = "explicit_euler";
    if (version >= 3u) {
        timeIntegratorId = readString(input);
    }
    const auto eventTimelineHash = readString(input);
    const auto activeSubsystemSetHash = readString(input);
    const auto profileFingerprint = readPod<std::uint64_t>(input);
    const auto compatibilityFingerprint = readPod<std::uint64_t>(input);
    const auto identityHash = readPod<std::uint64_t>(input);

    RuntimeCheckpoint checkpoint;
    checkpoint.runSignature = RunSignature(
        globalSeed,
        initializationParameterHash,
        GridSpec{gridWidth, gridHeight},
        boundaryMode,
        unitRegime,
        temporalPolicy,
        timeIntegratorId,
        eventTimelineHash,
        activeSubsystemSetHash,
        profileFingerprint,
        compatibilityFingerprint,
        identityHash);

    checkpoint.profileFingerprint = readPod<std::uint64_t>(input);

    if (version >= 4u) {
        checkpoint.checkpointIncludeUnspecifiedVariables = (readPod<std::uint8_t>(input) != 0u);
        const auto cadenceCount = readPod<std::uint64_t>(input);
        checkpoint.variableCheckpointIntervalSteps.clear();
        for (std::uint64_t cadenceIndex = 0; cadenceIndex < cadenceCount; ++cadenceIndex) {
            const auto variableName = readString(input);
            const auto interval = readPod<std::uint32_t>(input);
            checkpoint.variableCheckpointIntervalSteps[variableName] = interval;
        }
    } else {
        checkpoint.checkpointIncludeUnspecifiedVariables = true;
        checkpoint.variableCheckpointIntervalSteps.clear();
    }

    auto& snapshot = checkpoint.stateSnapshot;
    snapshot.header.stepIndex = readPod<std::uint64_t>(input);
    snapshot.header.timestampTicks = readPod<std::uint64_t>(input);
    snapshot.header.status = readPod<RuntimeStatus>(input);
    snapshot.grid.width = readPod<std::uint32_t>(input);
    snapshot.grid.height = readPod<std::uint32_t>(input);
    snapshot.boundaryMode = readPod<BoundaryMode>(input);
    snapshot.topologyBackend = readPod<GridTopologyBackend>(input);
    snapshot.memoryLayout.alignmentBytes = readPod<std::uint32_t>(input);
    snapshot.memoryLayout.tileWidth = readPod<std::uint32_t>(input);
    snapshot.memoryLayout.tileHeight = readPod<std::uint32_t>(input);
    snapshot.runIdentityHash = readPod<std::uint64_t>(input);
    snapshot.profileFingerprint = readPod<std::uint64_t>(input);
    snapshot.checkpointLabel = readString(input);
    snapshot.payloadBytes = readPod<std::uint64_t>(input);
    snapshot.stateHash = readPod<std::uint64_t>(input);

    const auto fieldCount = readPod<std::uint64_t>(input);
    snapshot.fields.clear();
    snapshot.fields.reserve(static_cast<std::size_t>(fieldCount));
    for (std::uint64_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
        StateStoreSnapshot::FieldPayload payload;
        payload.spec.id = readPod<std::uint32_t>(input);
        payload.spec.name = readString(input);
        payload.spec.dataType = readPod<VariableDataType>(input);

        const auto valuesCount = readPod<std::uint64_t>(input);
        payload.values.resize(static_cast<std::size_t>(valuesCount));
        if (valuesCount > 0) {
            input.read(
                reinterpret_cast<char*>(payload.values.data()),
                static_cast<std::streamsize>(valuesCount * sizeof(float)));
            if (!input) {
                throw std::runtime_error("failed to read field values");
            }
        }

        const auto maskCount = readPod<std::uint64_t>(input);
        payload.validityMask.resize(static_cast<std::size_t>(maskCount));
        if (maskCount > 0) {
            input.read(
                reinterpret_cast<char*>(payload.validityMask.data()),
                static_cast<std::streamsize>(maskCount));
            if (!input) {
                throw std::runtime_error("failed to read field validity mask");
            }
        }

        const auto overlayCount = readPod<std::uint64_t>(input);
        payload.sparseOverlay.reserve(static_cast<std::size_t>(overlayCount));
        for (std::uint64_t overlayIndex = 0; overlayIndex < overlayCount; ++overlayIndex) {
            const auto index = readPod<std::uint64_t>(input);
            const auto value = readPod<float>(input);
            payload.sparseOverlay.emplace_back(index, value);
        }

        snapshot.fields.push_back(std::move(payload));
    }

    checkpoint.manualEventLog.clear();
    if (version >= 2u) {
        const auto manualEventCount = readPod<std::uint64_t>(input);
        checkpoint.manualEventLog.reserve(static_cast<std::size_t>(manualEventCount));
        for (std::uint64_t eventIndex = 0; eventIndex < manualEventCount; ++eventIndex) {
            ManualEventRecord record;
            record.step = readPod<std::uint64_t>(input);
            record.time = readPod<float>(input);
            record.variable = readString(input);
            record.cellIndex = readPod<std::uint64_t>(input);
            record.oldValue = readPod<float>(input);
            record.newValue = readPod<float>(input);
            record.description = readString(input);
            record.timestamp = readPod<std::uint64_t>(input);
            record.kind = readPod<ManualEventKind>(input);
            checkpoint.manualEventLog.push_back(std::move(record));
        }
    }

    return checkpoint;
}

} // namespace ws::app
