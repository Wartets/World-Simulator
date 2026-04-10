#include "ws/gui/error_context.hpp"

#include <cassert>
#include <string>

int main() {
    using ws::gui::ErrorContext;

    {
        ErrorContext context{"world_open"};
        auto outer = context.push("restore_checkpoint");
        auto inner = context.push("read_checkpoint_file");

        const std::string message = context.formatFailure("world_open_failed", "file_missing");
        assert(message.find("world_open_failed") != std::string::npos);
        assert(message.find("context=world_open>restore_checkpoint>read_checkpoint_file") != std::string::npos);
        assert(message.find("error=file_missing") != std::string::npos);
    }

    {
        ErrorContext context{"runtime_start"};
        {
            auto scope = context.push("load_model_execution_spec");
            (void)scope;
            assert(context.chain() == "runtime_start>load_model_execution_spec");
        }
        assert(context.chain() == "runtime_start");
    }

    {
        ErrorContext context;
        const std::string message = context.formatFailure("start_failed", "unknown");
        assert(message == "start_failed error=unknown");
    }

    return 0;
}
