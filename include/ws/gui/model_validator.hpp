#pragma once

#include <string>
#include <vector>
#include <memory>

namespace ws::gui {

struct ValidationMessage {
    enum class Severity { Error, Warning, Info };
    
    Severity severity;
    std::string location;  // e.g., "node:eq_1"
    std::string message;
    std::string suggestion;
};

class ModelValidator {
public:
    ModelValidator();
    ~ModelValidator();
    
    // Validation methods
    bool validateSyntax(const std::string& formula);
    bool validateTypes(const std::vector<std::string>& variables);
    bool validateUnits(const std::vector<std::string>& units);
    bool validateDependencies(const std::vector<std::string>& variables);
    bool validateStructure(const std::vector<std::string>& stage_names);
    
    // Get validation results
    const std::vector<ValidationMessage>& getMessages() const { return messages; }
    
    // Clear messages
    void clearMessages() { messages.clear(); }
    
    // Utility functions
    static bool isSyntacticallyValid(const std::string& formula);
    static std::string suggestVariableName(const std::string& typo, 
                                          const std::vector<std::string>& available);
    
private:
    std::vector<ValidationMessage> messages;
    
    // Validation helpers
    void addMessage(ValidationMessage::Severity sev, const std::string& loc,
                   const std::string& msg, const std::string& suggest = "");
};

} // namespace ws::gui
