#include "ws/gui/status_message.hpp"

#include <cassert>
#include <string>

using ws::gui::OperationSeverity;
using ws::gui::OperationStatus;
using ws::gui::formatOperationMessageForDisplay;
using ws::gui::translateOperationResult;
using ws::gui::translateOperationMessage;

int main() {
    {
        const auto message = translateOperationMessage("world_delete_failed error=no_selection");
        assert(message.severity == OperationSeverity::Warning);
        assert(message.userMessage == "No world is selected.");
        assert(message.technicalDetail == "world_delete_failed error=no_selection");
        assert(formatOperationMessageForDisplay(message) == message.userMessage);
    }

    {
        const auto message = translateOperationMessage("probe_csv_export_failed reason=file_open");
        assert(message.severity == OperationSeverity::Error);
        assert(message.userMessage == "The probe export file could not be opened.");
        assert(message.technicalDetail == "probe_csv_export_failed reason=file_open");
    }

    {
        const auto message = translateOperationMessage("wizard_step_blocked reason=preflight_blocking");
        assert(message.severity == OperationSeverity::Error);
        assert(message.userMessage.find("Wizard progression is blocked") != std::string::npos);
    }

    {
        const auto message = translateOperationMessage("world_saved name=alpha");
        assert(message.severity == OperationSeverity::Info);
        assert(message.userMessage == "World saved");
        assert(message.technicalDetail == "world_saved name=alpha");
    }

    {
        const auto result = translateOperationResult("world_saved name=alpha");
        assert(result.status == OperationStatus::Success);
        assert(result.ok());
        assert(result.message == "World saved");
        assert(result.technicalDetail == "world_saved name=alpha");
    }

    {
        const auto result = translateOperationResult("world_delete_failed error=no_selection");
        assert(result.status == OperationStatus::Warning);
        assert(result.ok());
        assert(result.message == "No world is selected.");
        assert(result.technicalDetail == "world_delete_failed error=no_selection");
    }

    {
        const auto result = translateOperationResult("world_open_failed reason=file_open");
        assert(result.status == OperationStatus::Failure);
        assert(!result.ok());
        assert(result.message.find("Open world failed") != std::string::npos);
        assert(result.technicalDetail == "world_open_failed reason=file_open");
    }

    return 0;
}
