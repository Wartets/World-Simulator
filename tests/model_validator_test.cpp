#include "ws/gui/model_validator.hpp"

#include <cassert>
#include <string>
#include <vector>

using ws::gui::ModelValidator;
using ws::gui::ValidationMessage;

int main() {
    {
        ModelValidator validator;
        const bool ok = validator.validateSyntax("state_x = @ + 1");
        assert(!ok);
        const auto& messages = validator.getMessages();
        assert(!messages.empty());
        const auto& message = messages.front();
        assert(message.severity == ValidationMessage::Severity::Error);
        assert(message.constraint.find("allowed characters") != std::string::npos);
        assert(!message.suggestion.empty());
    }

    {
        ModelValidator validator;
        const bool ok = validator.validateTypes({"state_x", "state_x"});
        assert(!ok);
        const auto& messages = validator.getMessages();
        assert(!messages.empty());
        const auto& message = messages.front();
        assert(message.severity == ValidationMessage::Severity::Error);
        assert(message.constraint.find("must be unique") != std::string::npos);
        assert(message.suggestion.find("Rename") != std::string::npos);
    }

    {
        ModelValidator validator;
        const bool ok = validator.validateUnits({"foo"});
        assert(ok);
        const auto& messages = validator.getMessages();
        assert(!messages.empty());
        const auto& message = messages.front();
        assert(message.severity == ValidationMessage::Severity::Warning);
        assert(message.constraint.find("accepted units") != std::string::npos);
        assert(message.suggestion.find("dimensionless") != std::string::npos);
    }

    {
        ModelValidator validator;
        const bool ok = validator.validateStructure({"stage_main", "stage_main"});
        assert(!ok);
        const auto& messages = validator.getMessages();
        assert(!messages.empty());
        const auto& message = messages.front();
        assert(message.severity == ValidationMessage::Severity::Error);
        assert(message.constraint.find("must be unique") != std::string::npos);
        assert(message.suggestion.find("Rename") != std::string::npos);
    }

    return 0;
}
