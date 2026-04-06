#pragma once

#include <string>
#include <stdexcept>
#include <cstdint>

namespace ws {

// =============================================================================
// SI Unit
// =============================================================================

// Represents SI (International System) units with dimensional analysis.
// Uses exponents for each base dimension: kg, m, s, K, A, mol, cd.
class SIUnit {
public:
    int8_t kg{0};   // Kilogram exponent.
    int8_t m{0};    // Meter exponent.
    int8_t s{0};    // Second exponent.
    int8_t K{0};    // Kelvin exponent.
    int8_t A{0};    // Ampere exponent.
    int8_t mol{0};  // Mole exponent.
    int8_t cd{0};   // Candela exponent.

    SIUnit() = default;
    
    // Returns true if the unit is dimensionless (all exponents zero).
    bool isDimensionless() const noexcept;
    // Equality comparison.
    bool operator==(const SIUnit& other) const noexcept;
    // Inequality comparison.
    bool operator!=(const SIUnit& other) const noexcept;
    
    // Multiplies two units.
    SIUnit operator*(const SIUnit& other) const noexcept;
    // Divides two units.
    SIUnit operator/(const SIUnit& other) const noexcept;
    
    // Adds two compatible units (throws if incompatible).
    SIUnit add(const SIUnit& other) const; 
    // Subtracts two compatible units (throws if incompatible).
    SIUnit sub(const SIUnit& other) const; 
    
    // Returns a dimensionless unit (all zeros).
    static SIUnit dimensionless() noexcept;
    
    // Parses unit strings like "kg/(m*s^2)" or "m/s".
    static SIUnit parse(const std::string& unitStr);
    
    // Converts the unit to a string representation.
    std::string toString() const;
};

// =============================================================================
// Unit Mismatch Error
// =============================================================================

// Exception thrown when unit compatibility checks fail.
class UnitMismatchError : public std::runtime_error {
public:
    // Constructor with message.
    UnitMismatchError(const std::string& msg) : std::runtime_error(msg) {}
    // Constructor with two conflicting units and context.
    UnitMismatchError(const SIUnit& a, const SIUnit& b, const std::string& context);
};

} // namespace ws
