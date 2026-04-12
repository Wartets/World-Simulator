#include "ws/core/unit_lint.hpp"

#include <cassert>
#include <string>

using ws::UnitAliasFinding;
using ws::detectDerivedUnitAliases;

int main() {
    {
        const auto findings = detectDerivedUnitAliases("kg/(m*s^2)");
        assert(findings.empty());
    }

    {
        const auto findings = detectDerivedUnitAliases("Pa/s");
        assert(findings.size() == 1u);
        assert(findings.front().alias == "Pa");
        assert(findings.front().recommendedBaseExpression == "kg/(m*s^2)");
    }

    {
        const auto findings = detectDerivedUnitAliases("N*m");
        assert(findings.size() == 1u);
        assert(findings.front().alias == "N");
    }

    {
        const auto findings = detectDerivedUnitAliases("J/(mol*K)");
        assert(findings.size() == 1u);
        assert(findings.front().alias == "J");
    }

    {
        const auto findings = detectDerivedUnitAliases("Pa*Pa");
        assert(findings.size() == 1u);
        assert(findings.front().alias == "Pa");
    }

    return 0;
}
