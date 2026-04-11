#pragma once

#include <string>
#include <string_view>

namespace ws::gui {

// Builds a compact key-value operation message.
// Example: formatOperationMessage("start_failed", "error", "filesystem")
// => "start_failed error=filesystem"
[[nodiscard]] inline std::string formatOperationMessage(
    const std::string_view operation,
    const std::string_view detailKey,
    const std::string_view detail) {
    std::string message;
    message.reserve(operation.size() + detailKey.size() + detail.size() + 2u);
    message.append(operation);
    if (!detail.empty()) {
        message.push_back(' ');
        message.append(detailKey);
        message.push_back('=');
        message.append(detail);
    }
    return message;
}

// Builds a normalized failure message using "error" as the default detail key.
[[nodiscard]] inline std::string formatOperationFailure(
    const std::string_view operation,
    const std::string_view detail,
    const std::string_view detailKey = "error") {
    return formatOperationMessage(operation, detailKey, detail);
}

} // namespace ws::gui
