#pragma once

#include <algorithm>
#include <cstddef>
#include <cctype>
#include <string>
#include <utility>

namespace ws::gui {

enum class OperationSeverity {
    Info = 0,
    Warning = 1,
    Error = 2,
};

enum class OperationStatus {
    Success = 0,
    Warning = 1,
    Failure = 2,
};

struct OperationMessage {
    OperationSeverity severity = OperationSeverity::Info;
    std::string userMessage;
    std::string technicalDetail;

    [[nodiscard]] bool ok() const noexcept {
        return severity != OperationSeverity::Error;
    }
};

struct OperationResult {
    OperationStatus status = OperationStatus::Success;
    std::string message;
    std::string technicalDetail;

    [[nodiscard]] bool ok() const noexcept {
        return status != OperationStatus::Failure;
    }
};

[[nodiscard]] inline OperationStatus statusFromSeverity(const OperationSeverity severity) {
    switch (severity) {
        case OperationSeverity::Warning:
            return OperationStatus::Warning;
        case OperationSeverity::Error:
            return OperationStatus::Failure;
        default:
            return OperationStatus::Success;
    }
}

[[nodiscard]] inline std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[nodiscard]] inline std::string humanizeStatusToken(std::string token) {
    std::string out;
    out.reserve(token.size());
    bool upperNext = true;
    bool firstLetter = true;
    for (char ch : token) {
        if (ch == '_' || ch == '-') {
            out.push_back(' ');
            upperNext = true;
            continue;
        }
        if (firstLetter) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            firstLetter = false;
            upperNext = false;
        } else if (upperNext) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            upperNext = false;
        } else {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

[[nodiscard]] inline std::string extractHeadToken(const std::string& rawMessage) {
    const std::size_t delim = rawMessage.find_first_of(" =\t\r\n");
    return rawMessage.substr(0, delim == std::string::npos ? rawMessage.size() : delim);
}

[[nodiscard]] inline std::string extractReasonToken(const std::string& rawMessage) {
    const std::string key = "reason=";
    const std::size_t reasonPos = toLowerCopy(rawMessage).find(key);
    if (reasonPos == std::string::npos) {
        return {};
    }

    const std::size_t valueStart = reasonPos + key.size();
    const std::size_t valueEnd = rawMessage.find_first_of(" \t\n\r", valueStart);
    if (valueEnd == std::string::npos || valueEnd <= valueStart) {
        return rawMessage.substr(valueStart);
    }
    return rawMessage.substr(valueStart, valueEnd - valueStart);
}

[[nodiscard]] inline OperationMessage makeOperationMessage(
    const OperationSeverity severity,
    std::string userMessage,
    std::string technicalDetail = {}) {
    OperationMessage message;
    message.severity = severity;
    message.userMessage = std::move(userMessage);
    message.technicalDetail = std::move(technicalDetail);
    return message;
}

[[nodiscard]] inline OperationMessage translateOperationMessage(const std::string& rawMessage) {
    if (rawMessage.empty()) {
        return {};
    }

    const std::string lower = toLowerCopy(rawMessage);
    const auto actionable = [](const std::string& what, const std::string& why, const std::string& next) {
        return std::string("What happened: ") + what +
            " | Why: " + why +
            " | Next: " + next;
    };

    if (lower.find("wizard_step_blocked reason=unresolved_bindings") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Wizard progression is blocked",
                "required model variable bindings are unresolved",
                "Open binding controls, resolve missing mappings, then continue."),
            rawMessage);
    }
    if (lower.find("wizard_step_blocked reason=preflight_blocking") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Wizard progression is blocked",
                "preflight detected blocking validation issues",
                "Fix blocking preflight items in Step 3, then retry."),
            rawMessage);
    }
    if (lower.find("world_create_blocked reason=verification_failed") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "verification found blocking errors",
                "Resolve verification blockers and rerun world creation."),
            rawMessage);
    }
    if (lower.find("world_create_blocked reason=model_catalog_unavailable") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "model variable catalog is unavailable",
                "Reload/select model catalog, then retry world creation."),
            rawMessage);
    }
    if (lower.find("world_create_blocked reason=missing_conway_target_variable") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "Conway mode target variable is missing",
                "Select a Conway target variable and retry."),
            rawMessage);
    }
    if (lower.find("world_create_blocked reason=missing_gray_scott_target_variable") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "Gray-Scott mode requires both target variables",
                "Choose valid target variables A and B, then retry."),
            rawMessage);
    }
    if (lower.find("world_create_blocked reason=missing_waves_target_variable") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "Waves mode target variable is missing",
                "Select a Waves target variable and retry."),
            rawMessage);
    }
    if (lower.find("world_create_blocked reason=unsupported_generation_mode") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "selected generation mode is unsupported by current runtime",
                "Switch to a supported generation mode and retry."),
            rawMessage);
    }
    if (lower.find("world_create_blocked unresolved_bindings=") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "World creation was blocked",
                "initialization bindings remain unresolved",
                "Resolve remaining bindings in the wizard and retry."),
            rawMessage);
    }
    if (lower.find("world_open_failed") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Open world failed",
                "selected world data could not be loaded",
                "Verify file availability/compatibility and retry open."),
            rawMessage);
    }
    if (lower.find("world_delete_failed") != std::string::npos) {
        if (lower.find("no_selection") != std::string::npos) {
            return makeOperationMessage(
                OperationSeverity::Warning,
                "No world is selected.",
                rawMessage);
        }
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Delete world failed",
                "storage delete operation returned failure",
                "Check file permissions/locks and retry delete."),
            rawMessage);
    }
    if (lower.find("world_rename_failed") != std::string::npos) {
        if (lower.find("no_selection") != std::string::npos) {
            return makeOperationMessage(
                OperationSeverity::Warning,
                "No world is selected.",
                rawMessage);
        }
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Rename world failed",
                "target name or storage operation was rejected",
                "Use a valid unique name and retry rename."),
            rawMessage);
    }
    if (lower.find("world_duplicate_failed") != std::string::npos) {
        if (lower.find("no_selection") != std::string::npos) {
            return makeOperationMessage(
                OperationSeverity::Warning,
                "No world is selected.",
                rawMessage);
        }
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Duplicate world failed",
                "copy operation could not complete",
                "Check destination access and retry duplication."),
            rawMessage);
    }
    if (lower.find("world_export_failed") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Export world failed",
                "export write operation returned failure",
                "Verify destination path and retry export."),
            rawMessage);
    }
    if (lower.find("world_import_failed") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            actionable(
                "Import world failed",
                "selected file could not be parsed or validated",
                "Check file format/contents and retry import."),
            rawMessage);
    }
    if (lower.find("probe_csv_export_failed reason=create_directory") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            "The probe export folder could not be created.",
            rawMessage);
    }
    if (lower.find("probe_csv_export_failed reason=file_open") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Error,
            "The probe export file could not be opened.",
            rawMessage);
    }
    if (lower.find("runtime_not_active") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Warning,
            "The runtime is not active.",
            rawMessage);
    }
    if (lower.find("no_selection") != std::string::npos) {
        return makeOperationMessage(
            OperationSeverity::Warning,
            "No selection is available for that action.",
            rawMessage);
    }
    if (lower.find("_failed") != std::string::npos || lower.find("_blocked") != std::string::npos) {
        const std::string reason = extractReasonToken(rawMessage);
        if (!reason.empty()) {
            return makeOperationMessage(
                OperationSeverity::Error,
                humanizeStatusToken(extractHeadToken(rawMessage)) + ": " + humanizeStatusToken(reason),
                rawMessage);
        }
        return makeOperationMessage(
            OperationSeverity::Error,
            humanizeStatusToken(extractHeadToken(rawMessage)),
            rawMessage);
    }

    const std::string head = extractHeadToken(rawMessage);
    if (!head.empty()) {
        return makeOperationMessage(
            OperationSeverity::Info,
            humanizeStatusToken(head),
            rawMessage);
    }

    return makeOperationMessage(OperationSeverity::Info, rawMessage, rawMessage);
}

[[nodiscard]] inline std::string formatOperationMessageForDisplay(const OperationMessage& message) {
    if (!message.userMessage.empty()) {
        return message.userMessage;
    }
    return message.technicalDetail;
}

[[nodiscard]] inline OperationResult toOperationResult(const OperationMessage& message) {
    OperationResult result;
    result.status = statusFromSeverity(message.severity);
    result.message = formatOperationMessageForDisplay(message);
    result.technicalDetail = message.technicalDetail;
    return result;
}

[[nodiscard]] inline OperationResult translateOperationResult(const std::string& rawMessage) {
    return toOperationResult(translateOperationMessage(rawMessage));
}

} // namespace ws::gui
