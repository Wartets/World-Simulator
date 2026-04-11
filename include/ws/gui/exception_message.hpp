#pragma once

#include "ws/gui/status_message.hpp"

#include <exception>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>

namespace ws::gui {

[[nodiscard]] inline OperationMessage translateExceptionMessage(
    const std::exception& exception,
    const std::string& context,
    const std::string& recoveryHint = {}) {
    const auto makeMessage = [&](const std::string& what, const std::string& why, const std::string& next) {
        std::string userMessage = "What happened: " + what + " | Why: " + why + " | Next: " + next;
        std::string technicalDetail = context;
        if (!technicalDetail.empty()) {
            technicalDetail += " | ";
        }
        technicalDetail += exception.what();
        return makeOperationMessage(OperationSeverity::Error, std::move(userMessage), std::move(technicalDetail));
    };

    if (const auto* filesystemError = dynamic_cast<const std::filesystem::filesystem_error*>(&exception)) {
        std::string what = context.empty() ? std::string("File operation failed") : context;
        std::string why = "the filesystem rejected the requested operation";
        std::string next = recoveryHint.empty()
            ? "Check the path, permissions, and whether another program is holding the file open."
            : recoveryHint;
        if (!filesystemError->path1().empty() || !filesystemError->path2().empty()) {
            why += " for the requested path";
        }
        return makeMessage(what, why, next);
    }

    if (dynamic_cast<const std::invalid_argument*>(&exception) != nullptr) {
        return makeMessage(
            context.empty() ? std::string("Invalid input") : context,
            "the provided value did not satisfy the expected format",
            recoveryHint.empty() ? "Correct the input value and retry." : recoveryHint);
    }

    if (dynamic_cast<const std::out_of_range*>(&exception) != nullptr) {
        return makeMessage(
            context.empty() ? std::string("Input value out of range") : context,
            "the provided value exceeded the supported range",
            recoveryHint.empty() ? "Choose a value within the supported range and retry." : recoveryHint);
    }

    if (dynamic_cast<const std::bad_alloc*>(&exception) != nullptr) {
        return makeMessage(
            context.empty() ? std::string("Operation ran out of memory") : context,
            "the process could not allocate more memory",
            recoveryHint.empty() ? "Close other memory-intensive tasks and retry." : recoveryHint);
    }

    return makeMessage(
        context.empty() ? std::string("Operation failed") : context,
        "an unexpected exception was raised",
        recoveryHint.empty() ? "Retry the operation after checking the input and application state." : recoveryHint);
}

} // namespace ws::gui