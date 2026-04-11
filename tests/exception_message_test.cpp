#include "ws/gui/exception_message.hpp"

#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <string>

using ws::gui::OperationSeverity;
using ws::gui::translateExceptionMessage;

int main() {
    {
        const std::invalid_argument exception("bad seed value");
        const auto message = translateExceptionMessage(exception, "Starting simulation", "Choose a valid seed and retry.");
        assert(message.severity == OperationSeverity::Error);
        assert(message.userMessage.find("Starting simulation") != std::string::npos);
        assert(message.userMessage.find("provided value did not satisfy the expected format") != std::string::npos);
        assert(message.technicalDetail.find("bad seed value") != std::string::npos);
    }

    {
        const auto filesystemError = std::filesystem::filesystem_error(
            "open",
            std::filesystem::path("model.zip"),
            std::make_error_code(std::errc::permission_denied));
        const auto message = translateExceptionMessage(filesystemError, "Loading model", "Check file permissions and retry.");
        assert(message.severity == OperationSeverity::Error);
        assert(message.userMessage.find("filesystem rejected") != std::string::npos);
        assert(message.userMessage.find("Loading model") != std::string::npos);
        assert(message.technicalDetail.find("model.zip") != std::string::npos || !message.technicalDetail.empty());
    }

    {
        const std::runtime_error exception("unexpected parser state");
        const auto message = translateExceptionMessage(exception, "Restoring editor state");
        assert(message.severity == OperationSeverity::Error);
        assert(message.userMessage.find("unexpected exception") != std::string::npos);
        assert(message.technicalDetail.find("unexpected parser state") != std::string::npos);
    }

    return 0;
}