#include "ws/gui/message_formatter.hpp"

#include <cassert>
#include <string>

int main() {
    {
        const std::string message = ws::gui::formatOperationMessage("runtime_start", "state", "ready");
        assert(message == "runtime_start state=ready");
    }

    {
        const std::string message = ws::gui::formatOperationMessage("runtime_start", "state", "");
        assert(message == "runtime_start");
    }

    {
        const std::string message = ws::gui::formatOperationFailure("start_failed", "permission denied");
        assert(message == "start_failed error=permission denied");
    }

    {
        const std::string message = ws::gui::formatOperationFailure("start_failed", "");
        assert(message == "start_failed");
    }

    return 0;
}
