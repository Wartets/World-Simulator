#pragma once

#include <string>
#include <vector>

namespace ws {

struct UnitAliasFinding {
    std::string alias;
    std::string recommendedBaseExpression;
};

// Detects derived-unit aliases (for example Pa, N, J) in a unit expression.
// Returns findings in deterministic lexical order by alias.
std::vector<UnitAliasFinding> detectDerivedUnitAliases(const std::string& unitExpression);

} // namespace ws
