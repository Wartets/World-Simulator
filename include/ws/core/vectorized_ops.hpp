#pragma once

#include "ws/core/spatial_scheme.hpp"

#include <cstddef>
#include <cstdint>

namespace ws::vectorized {

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
