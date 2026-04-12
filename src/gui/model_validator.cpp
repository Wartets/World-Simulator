#include "ws/gui/model_validator.hpp"

#include <cctype>
#include <algorithm>
#include <climits>
#include <regex>
#include <unordered_set>

namespace ws::gui {

// Default constructor; initializes validator with empty state.
ModelValidator::ModelValidator() = default;

// Destructor; default.
ModelValidator::~ModelValidator() = default;

// Validates syntax of formula string.
// Checks for balanced parentheses, valid operators, and proper formatting.
// @param formula Formula string to validate
// @return true if syntax is valid
bool ModelValidator::validateSyntax(const std::string& formula) {
    // Basic syntax validation using regex
    // Check for balanced parentheses, valid operators, etc.
    
    clearMessages();
    
    if (formula.empty()) {
        addMessage(
            ValidationMessage::Severity::Error,
            "formula",
            "Formula is empty",
            "formula must contain at least one symbol",
            "Enter an expression such as 'state_x = state_x + forcing_u'.");
        return false;
    }

    // Check for balanced parentheses
    int paren_count = 0;
    for (char c : formula) {
        if (c == '(') paren_count++;
        else if (c == ')') paren_count--;
        
        if (paren_count < 0) {
            addMessage(
                ValidationMessage::Severity::Error,
                "formula",
                "Unbalanced parentheses: closing paren without opening",
                "parentheses must be balanced",
                "Add a matching '(' before this closing parenthesis.");
            return false;
        }
    }
    
    if (paren_count != 0) {
        addMessage(
            ValidationMessage::Severity::Error,
            "formula",
            "Unbalanced parentheses: " + std::to_string(paren_count) + " unclosed",
            "parentheses must be balanced",
            "Add " + std::to_string(paren_count) + " closing ')' token(s) to complete the expression.");
        return false;
    }
    
    // Check for valid identifiers and operators
    std::regex valid_formula_pattern(
        R"(^[\w\s\+\-\*\/\(\)\,\.\=\>\<\!\&\|\s]+$)"
    );
    
    if (!std::regex_match(formula, valid_formula_pattern)) {
        addMessage(
            ValidationMessage::Severity::Error,
            "formula",
            "Formula contains invalid characters",
            "allowed characters: [A-Za-z0-9_ + - * / ( ) , . = > < ! & | whitespace]",
            "Remove unsupported characters or replace them with supported arithmetic/logical tokens.");
        return false;
    }
    
    return true;
}

bool ModelValidator::validateTypes(const std::vector<std::string>& variables) {
    clearMessages();
    
    // Check for duplicate variable names
    for (size_t i = 0; i < variables.size(); ++i) {
        for (size_t j = i + 1; j < variables.size(); ++j) {
            if (variables[i] == variables[j]) {
                addMessage(
                    ValidationMessage::Severity::Error,
                    "variable:" + variables[i],
                    "Duplicate variable name: " + variables[i],
                    "variable ids must be unique within a model",
                    "Rename one of the duplicate variables (for example, append a suffix such as '_2').");
                return false;
            }
        }
    }
    
    return true;
}

bool ModelValidator::validateUnits(const std::vector<std::string>& units) {
    clearMessages();
    
    // Validate SI unit strings
    static const std::vector<std::string> valid_units = {
        "kg", "m", "s", "K", "A", "mol", "cd",
        "m/s", "m/s^2", "kg/m^3", "kg/(m*s^2)",
        "kg/(m*s)", "kg/s", "kg/s^3"
    };
    
    for (const auto& unit : units) {
        if (unit.empty()) continue;
        
        // Allow basic combinations
        bool found = std::find(valid_units.begin(), valid_units.end(), unit) != valid_units.end();
        
        if (!found && unit != "dimensionless") {
            // Allow some flexibility for compound units
            if (unit.find('/') == std::string::npos && 
                unit.find('*') == std::string::npos &&
                unit.find('^') == std::string::npos) {
                // Check if it's a base SI unit
                bool is_base_unit = (unit == "kg" || unit == "m" || unit == "s" || 
                                    unit == "K" || unit == "A" || unit == "mol" || unit == "cd");
                if (!is_base_unit) {
                    addMessage(
                        ValidationMessage::Severity::Warning,
                        "unit:" + unit,
                        "Unrecognized unit: " + unit,
                        "accepted units include base SI symbols and common compounds (kg, m, s, K, A, mol, cd, m/s, m/s^2, kg/m^3, kg/(m*s^2), kg/(m*s), kg/s, kg/s^3)",
                        "Use a supported SI unit token or a valid compound expression (/, *, ^). Use 'dimensionless' when no physical unit applies.");
                }
            }
        }
    }
    
    return true;
}

bool ModelValidator::validateDependencies(const std::vector<std::string>& variables) {
    clearMessages();

    std::unordered_set<std::string> seen;
    for (const auto& variable : variables) {
        if (variable.empty()) {
            continue;
        }
        seen.insert(variable);
    }

    return !seen.empty() || variables.empty();
}

bool ModelValidator::validateDependencies(
    const std::vector<std::string>& formulas,
    const std::vector<std::string>& variables) {
    clearMessages();

    std::unordered_set<std::string> declaredVariables;
    declaredVariables.reserve(variables.size());
    for (const auto& variable : variables) {
        if (!variable.empty()) {
            declaredVariables.insert(variable);
        }
    }

    static const std::unordered_set<std::string> reservedWords = {
        "if", "else", "for", "while", "return", "true", "false", "and", "or", "not"
    };

    static const std::unordered_set<std::string> intrinsicFunctions = {
        "abs", "clamp", "cos", "exp", "floor", "lerp", "log", "max", "min", "pow",
        "round", "sin", "sqrt", "step", "tan"
    };

    const std::regex identifierPattern(R"([A-Za-z_][A-Za-z0-9_]*)");
    bool valid = true;

    for (std::size_t formulaIndex = 0; formulaIndex < formulas.size(); ++formulaIndex) {
        const auto& formula = formulas[formulaIndex];
        if (formula.empty()) {
            continue;
        }

        for (std::sregex_iterator iter(formula.begin(), formula.end(), identifierPattern), end; iter != end; ++iter) {
            const std::string token = iter->str();
            if (token.empty()) {
                continue;
            }

            if (reservedWords.contains(token) || intrinsicFunctions.contains(token)) {
                continue;
            }

            if (declaredVariables.contains(token)) {
                continue;
            }

            const std::string suggestion = suggestVariableName(token, variables);
            addMessage(
                ValidationMessage::Severity::Warning,
                "formula:" + std::to_string(formulaIndex),
                "Reference to undeclared variable: " + token,
                "formula identifiers should map to declared model variables",
                suggestion.empty()
                    ? "Declare this variable in the model graph or remove the unresolved identifier."
                    : "Did you mean '" + suggestion + "'?"
            );
            valid = false;
        }
    }

    return valid;
}

bool ModelValidator::validateStructure(const std::vector<std::string>& stage_names) {
    clearMessages();
    
    // Check for duplicate stage names
    for (size_t i = 0; i < stage_names.size(); ++i) {
        for (size_t j = i + 1; j < stage_names.size(); ++j) {
            if (stage_names[i] == stage_names[j]) {
                addMessage(
                    ValidationMessage::Severity::Error,
                    "stage:" + stage_names[i],
                    "Duplicate stage name: " + stage_names[i],
                    "stage ids must be unique within a model",
                    "Rename one stage to a unique id (for example, add a semantic suffix such as '_transport').");
                return false;
            }
        }
    }
    
    return true;
}

bool ModelValidator::isSyntacticallyValid(const std::string& formula) {
    // Simple check: at least some content and balanced parens
    if (formula.empty()) return false;
    
    int paren_count = 0;
    for (char c : formula) {
        if (c == '(') paren_count++;
        else if (c == ')') paren_count--;
        if (paren_count < 0) return false;
    }
    
    return paren_count == 0;
}

std::string ModelValidator::suggestVariableName(const std::string& typo,
                                               const std::vector<std::string>& available) {
    // Simple Levenshtein distance-based suggestion
    int min_distance = INT_MAX;
    std::string best_match;
    
    for (const auto& candidate : available) {
        // Calculate simple edit distance
        int distance = 0;
        size_t max_len = std::max(typo.length(), candidate.length());
        for (size_t i = 0; i < max_len; ++i) {
            char c1 = (i < typo.length()) ? typo[i] : '\0';
            char c2 = (i < candidate.length()) ? candidate[i] : '\0';
            if (c1 != c2) distance++;
        }
        
        if (distance < min_distance) {
            min_distance = distance;
            best_match = candidate;
        }
    }
    
    // Only suggest if distance is small enough
    if (min_distance <= 2) {
        return best_match;
    }
    
    return "";
}

void ModelValidator::addMessage(ValidationMessage::Severity sev,
                               const std::string& loc,
                               const std::string& msg,
                               const std::string& constraint,
                               const std::string& suggest) {
    messages.push_back(ValidationMessage{sev, loc, msg, constraint, suggest});
}

} // namespace ws::gui
