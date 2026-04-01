#pragma once

#include <string>
#include <stdexcept>
#include <cstdint>

namespace ws {

class SIUnit {
public:
    int8_t kg{0};
    int8_t m{0};
    int8_t s{0};
    int8_t K{0};
    int8_t A{0};
    int8_t mol{0};
    int8_t cd{0};

    SIUnit() = default;
    
    bool isDimensionless() const noexcept;
    bool operator==(const SIUnit& other) const noexcept;
    bool operator!=(const SIUnit& other) const noexcept;
    
    SIUnit operator*(const SIUnit& other) const noexcept;
    SIUnit operator/(const SIUnit& other) const noexcept;
    
    SIUnit add(const SIUnit& other) const; // Throws if incompatible
    SIUnit sub(const SIUnit& other) const; // Throws if incompatible
    
    static SIUnit dimensionless() noexcept;
    
    // Parses strings like "kg/(m*s^2)" or "m/s"
    static SIUnit parse(const std::string& unitStr);
    
    std::string toString() const;
};

class UnitMismatchError : public std::runtime_error {
public:
    UnitMismatchError(const std::string& msg) : std::runtime_error(msg) {}
    UnitMismatchError(const SIUnit& a, const SIUnit& b, const std::string& context);
};

} // namespace ws
