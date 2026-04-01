#pragma once

#include "ws/core/spatial_scheme.hpp"

#include <cstddef>
#include <cstdint>

namespace ws::vectorized {

void clampInPlace(float* data, std::size_t count, float minValue, float maxValue) noexcept;
void add(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;
void subtract(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;
void multiply(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;
void divide(float* destination, const float* lhs, const float* rhs, std::size_t count) noexcept;

void laplacian5Point2D(
    const float* input,
    float* output,
    std::uint32_t width,
    std::uint32_t height,
    BoundaryCondition boundaryCondition) noexcept;

void gradientCentralDifference2D(
    const float* input,
    float* outX,
    float* outY,
    std::uint32_t width,
    std::uint32_t height,
    BoundaryCondition boundaryCondition) noexcept;

} // namespace ws::vectorized
