#pragma once

#include <string>

namespace ws::gui {

enum class DataOperationMode {
    Copy = 0,
    Replace = 1,
    Merge = 2
};

[[nodiscard]] inline const char* dataOperationModeLabel(const DataOperationMode mode) {
    switch (mode) {
        case DataOperationMode::Copy: return "Copy";
        case DataOperationMode::Replace: return "Replace";
        case DataOperationMode::Merge: return "Merge";
    }
    return "Copy";
}

[[nodiscard]] inline const char* dataOperationModeBehavior(const DataOperationMode mode) {
    switch (mode) {
        case DataOperationMode::Copy: return "Creates a safe copy and preserves existing data.";
        case DataOperationMode::Replace: return "Overwrites the destination when it already exists.";
        case DataOperationMode::Merge: return "Attempts to combine source and destination data.";
    }
    return "Creates a safe copy and preserves existing data.";
}

[[nodiscard]] inline std::string dataOperationOverwriteImpact(const DataOperationMode mode, const bool destinationExists) {
    if (!destinationExists) {
        return "No existing destination detected.";
    }
    switch (mode) {
        case DataOperationMode::Copy:
            return "Existing destination preserved; a numbered copy target will be used.";
        case DataOperationMode::Replace:
            return "Existing destination will be replaced.";
        case DataOperationMode::Merge:
            return "Existing destination will be merged when merge is supported.";
    }
    return "No existing destination detected.";
}

[[nodiscard]] inline std::string dataOperationReceipt(
    const std::string& operation,
    const DataOperationMode mode,
    const std::string& summary,
    const std::string& recoveryHint) {
    std::string out = operation;
    out += " [";
    out += dataOperationModeLabel(mode);
    out += "]: ";
    out += summary;
    if (!recoveryHint.empty()) {
        out += " Next: ";
        out += recoveryHint;
    }
    return out;
}

} // namespace ws::gui
