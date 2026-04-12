#include "ws/core/unit_lint.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace ws {
namespace {

struct AliasRule {
    const char* alias;
    const char* expansion;
};

constexpr std::array<AliasRule, 8> kAliasRules = {{
    {"C", "A*s"},
    {"Hz", "1/s"},
    {"J", "kg*m^2/s^2"},
    {"N", "kg*m/s^2"},
    {"Pa", "kg/(m*s^2)"},
    {"V", "kg*m^2/(A*s^3)"},
    {"W", "kg*m^2/s^3"},
    {"Wb", "kg*m^2/(A*s^2)"},
}};

std::vector<std::string> tokenizeIdentifiers(const std::string& expression) {
    std::vector<std::string> tokens;
    std::string current;

    for (const char ch : expression) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            current.push_back(ch);
            continue;
        }
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

} // namespace

std::vector<UnitAliasFinding> detectDerivedUnitAliases(const std::string& unitExpression) {
    if (unitExpression.empty() || unitExpression == "1" || unitExpression == "none") {
        return {};
    }

    const auto identifiers = tokenizeIdentifiers(unitExpression);
    if (identifiers.empty()) {
        return {};
    }

    std::unordered_set<std::string> seenAliases;
    std::vector<UnitAliasFinding> findings;

    for (const auto& identifier : identifiers) {
        for (const auto& rule : kAliasRules) {
            if (identifier != rule.alias) {
                continue;
            }
            if (seenAliases.contains(identifier)) {
                continue;
            }
            seenAliases.insert(identifier);
            findings.push_back(UnitAliasFinding{identifier, rule.expansion});
            break;
        }
    }

    std::sort(findings.begin(), findings.end(), [](const UnitAliasFinding& lhs, const UnitAliasFinding& rhs) {
        return lhs.alias < rhs.alias;
    });

    return findings;
}

} // namespace ws
