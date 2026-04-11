
#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include "ws/core/spatial_scheme.hpp"

namespace ws::vectorized {

namespace detail {
// Unified boundary sampling implementation for both stateless and stateful (object) use.
inline float sampleBoundaryImpl(
    const float* input,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t x,
    std::int64_t y,
    ws::BoundaryCondition boundaryCondition,
    float boundaryValue = 0.0f) noexcept {
    const auto inside = [width, height](std::int64_t ix, std::int64_t iy) noexcept {
        return ix >= 0 && iy >= 0 && ix < static_cast<std::int64_t>(width) && iy < static_cast<std::int64_t>(height);
    };
    if (inside(x, y)) {
        return input[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    }
    switch (boundaryCondition) {
        case ws::BoundaryCondition::Periodic: {
            // Use wrapIndex logic for periodic boundaries
            const auto wrap = [](std::int64_t value, std::uint32_t extent) noexcept {
                if (extent == 0) return 0u;
                const std::int64_t mod = static_cast<std::int64_t>(extent);
                const std::int64_t wrapped = ((value % mod) + mod) % mod;
                return static_cast<std::uint32_t>(wrapped);
            };
            const auto rx = wrap(x, width);
            const auto ry = wrap(y, height);
            return input[static_cast<std::size_t>(ry) * width + rx];
        }
        case ws::BoundaryCondition::Reflecting: {
            // Use reflectIndex logic for reflecting boundaries
            const auto reflect = [](std::int64_t value, std::uint32_t extent) noexcept {
                if (extent <= 1u) return 0u;
                const std::int64_t period = static_cast<std::int64_t>(2u * extent - 2u);
                std::int64_t wrapped = value % period;
                if (wrapped < 0) wrapped += period;
                if (wrapped >= static_cast<std::int64_t>(extent)) wrapped = period - wrapped;
                return static_cast<std::uint32_t>(wrapped);
            };
            const auto rx = reflect(x, width);
            const auto ry = reflect(y, height);
            return input[static_cast<std::size_t>(ry) * width + rx];
        }
        case ws::BoundaryCondition::Dirichlet:
        case ws::BoundaryCondition::Absorbing:
            return boundaryValue;
        case ws::BoundaryCondition::Neumann: {
            const auto cx = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(width) - 1));
            const auto cy = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(height) - 1));
            return input[static_cast<std::size_t>(cy) * width + cx];
        }
    }
    return boundaryValue;
}

} // namespace detail

// Thin wrapper for stateless boundary sampling (for internal and legacy use)
inline float sampleBoundary(
    const float* input,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t x,
    std::int64_t y,
    ws::BoundaryCondition boundaryCondition) noexcept {
    return detail::sampleBoundaryImpl(input, width, height, x, y, boundaryCondition, 0.0f);
}

// Clamps values in place to the specified range.
void clampInPlace(float* data, std::size_t count, float minValue, float maxValue) noexcept;
// Element-wise addition of two arrays into destination.
void add(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;
// Element-wise subtraction of rhs from lhs into destination.
void subtract(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;
// Element-wise multiplication of two arrays into destination.
void multiply(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;
// Element-wise division of lhs by rhs into destination.
void divide(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;

// Computes 5-point stencil Laplacian on a 2D grid.
void laplacian5Point2D(
    const float* input,
    float* output,
    std::uint32_t width,
    std::uint32_t height,
    BoundaryCondition boundaryCondition) noexcept;

// Computes central difference gradient on a 2D grid.
void gradientCentralDifference2D(
    const float* input,
    float* outX,
    float* outY,
    std::uint32_t width,
    std::uint32_t height,
    BoundaryCondition boundaryCondition) noexcept;

} // namespace ws::vectorized
