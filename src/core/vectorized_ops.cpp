#include "ws/core/vectorized_ops.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#if defined(_M_X64) || defined(__SSE2__)
    #include <immintrin.h>
    #define WS_HAS_SSE2 1
#else
    #define WS_HAS_SSE2 0
#endif

namespace ws::vectorized {
namespace {

// Wraps a coordinate value to valid range [0, extent).
// Uses modular arithmetic to handle negative indices.
inline std::uint32_t wrapIndex(const std::int64_t value, const std::uint32_t extent) noexcept {
    const std::int64_t mod = static_cast<std::int64_t>(extent);
    const std::int64_t wrapped = ((value % mod) + mod) % mod;
    return static_cast<std::uint32_t>(wrapped);
}

// Reflects a coordinate value at boundary edges.
// Creates mirror-like boundary behavior for out-of-range indices.
inline std::uint32_t reflectIndex(const std::int64_t value, const std::uint32_t extent) noexcept {
    if (extent == 1u) {
        return 0u;
    }

    const std::int64_t period = static_cast<std::int64_t>(2u * extent - 2u);
    std::int64_t wrapped = value % period;
    if (wrapped < 0) {
        wrapped += period;
    }
    if (wrapped >= static_cast<std::int64_t>(extent)) {
        wrapped = period - wrapped;
    }
    return static_cast<std::uint32_t>(wrapped);
}

// Samples input array at specified coordinates with boundary handling.
// Applies different strategies based on boundary condition enum.
inline float sampleBoundary(
    const float* input,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::int64_t x,
    const std::int64_t y,
    const BoundaryCondition boundaryCondition) noexcept {
    const auto inside = [width, height](const std::int64_t ix, const std::int64_t iy) noexcept {
        return ix >= 0 && iy >= 0 && ix < static_cast<std::int64_t>(width) && iy < static_cast<std::int64_t>(height);
    };

    if (inside(x, y)) {
        return input[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    }

    switch (boundaryCondition) {
        case BoundaryCondition::Periodic: {
            const auto rx = wrapIndex(x, width);
            const auto ry = wrapIndex(y, height);
            return input[static_cast<std::size_t>(ry) * width + rx];
        }
        case BoundaryCondition::Reflecting: {
            const auto rx = reflectIndex(x, width);
            const auto ry = reflectIndex(y, height);
            return input[static_cast<std::size_t>(ry) * width + rx];
        }
        case BoundaryCondition::Dirichlet:
        case BoundaryCondition::Absorbing:
            return 0.0f;
        case BoundaryCondition::Neumann: {
            const auto rx = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(width) - 1));
            const auto ry = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(height) - 1));
            return input[static_cast<std::size_t>(ry) * width + rx];
        }
    }

    return 0.0f;
}

// Computes 5-point Laplacian for interior row cells.
// Uses centered difference approximation for second derivatives.
inline void laplacianInteriorScalar(
    const float* input,
    float* output,
    const std::uint32_t width,
    const std::uint32_t y) noexcept {
    const std::size_t row = static_cast<std::size_t>(y) * width;
    const float* north = input + row - width;
    const float* center = input + row;
    const float* south = input + row + width;
    float* out = output + row;

    for (std::uint32_t x = 1; x + 1 < width; ++x) {
        const std::size_t idx = static_cast<std::size_t>(x);
        out[idx] = north[idx] + south[idx] + center[idx - 1] + center[idx + 1] - 4.0f * center[idx];
    }
}

// Computes central difference gradient for interior row cells.
// Uses forward/backward differences for x and y components.
inline void gradientInteriorScalar(
    const float* input,
    float* outX,
    float* outY,
    const std::uint32_t width,
    const std::uint32_t y) noexcept {
    const std::size_t row = static_cast<std::size_t>(y) * width;
    const float* north = input + row - width;
    const float* center = input + row;
    const float* south = input + row + width;
    float* dx = outX + row;
    float* dy = outY + row;

    for (std::uint32_t x = 1; x + 1 < width; ++x) {
        const std::size_t idx = static_cast<std::size_t>(x);
        dx[idx] = 0.5f * (center[idx + 1] - center[idx - 1]);
        dy[idx] = 0.5f * (south[idx] - north[idx]);
    }
}

} // namespace

// Clamps all values in data array to [minValue, maxValue] range.
// Uses SSE2 intrinsics when available for vectorized processing.
void clampInPlace(float* data, const std::size_t count, const float minValue, const float maxValue) noexcept {
    if (data == nullptr || count == 0u) {
        return;
    }

#if WS_HAS_SSE2
    const __m128 minVec = _mm_set1_ps(minValue);
    const __m128 maxVec = _mm_set1_ps(maxValue);
    std::size_t i = 0;
    for (; i + 4u <= count; i += 4u) {
        __m128 values = _mm_loadu_ps(data + i);
        values = _mm_max_ps(values, minVec);
        values = _mm_min_ps(values, maxVec);
        _mm_storeu_ps(data + i, values);
    }
    for (; i < count; ++i) {
        data[i] = std::clamp(data[i], minValue, maxValue);
    }
#else
    for (std::size_t i = 0; i < count; ++i) {
        data[i] = std::clamp(data[i], minValue, maxValue);
    }
#endif
}

// Element-wise addition of two arrays into destination.
// Uses SSE2 when available for parallel processing of 4 floats.
void add(float* destination, const float* lhs, const float* rhs, const std::size_t count) noexcept {
    if (destination == nullptr || lhs == nullptr || rhs == nullptr || count == 0u) {
        return;
    }

#if WS_HAS_SSE2
    std::size_t i = 0;
    for (; i + 4u <= count; i += 4u) {
        const __m128 a = _mm_loadu_ps(lhs + i);
        const __m128 b = _mm_loadu_ps(rhs + i);
        _mm_storeu_ps(destination + i, _mm_add_ps(a, b));
    }
    for (; i < count; ++i) {
        destination[i] = lhs[i] + rhs[i];
    }
#else
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = lhs[i] + rhs[i];
    }
#endif
}

// Element-wise subtraction of rhs from lhs into destination.
// Uses SSE2 vectorization when available.
void subtract(float* destination, const float* lhs, const float* rhs, const std::size_t count) noexcept {
    if (destination == nullptr || lhs == nullptr || rhs == nullptr || count == 0u) {
        return;
    }

#if WS_HAS_SSE2
    std::size_t i = 0;
    for (; i + 4u <= count; i += 4u) {
        const __m128 a = _mm_loadu_ps(lhs + i);
        const __m128 b = _mm_loadu_ps(rhs + i);
        _mm_storeu_ps(destination + i, _mm_sub_ps(a, b));
    }
    for (; i < count; ++i) {
        destination[i] = lhs[i] - rhs[i];
    }
#else
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = lhs[i] - rhs[i];
    }
#endif
}

// Element-wise multiplication of two arrays into destination.
// Uses SSE2 vectorization for improved throughput.
void multiply(float* destination, const float* lhs, const float* rhs, const std::size_t count) noexcept {
    if (destination == nullptr || lhs == nullptr || rhs == nullptr || count == 0u) {
        return;
    }

#if WS_HAS_SSE2
    std::size_t i = 0;
    for (; i + 4u <= count; i += 4u) {
        const __m128 a = _mm_loadu_ps(lhs + i);
        const __m128 b = _mm_loadu_ps(rhs + i);
        _mm_storeu_ps(destination + i, _mm_mul_ps(a, b));
    }
    for (; i < count; ++i) {
        destination[i] = lhs[i] * rhs[i];
    }
#else
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = lhs[i] * rhs[i];
    }
#endif
}

// Element-wise division of lhs by rhs into destination.
// Uses SSE2 when available; falls back to scalar division.
void divide(float* destination, const float* lhs, const float* rhs, const std::size_t count) noexcept {
    if (destination == nullptr || lhs == nullptr || rhs == nullptr || count == 0u) {
        return;
    }

#if WS_HAS_SSE2
    std::size_t i = 0;
    for (; i + 4u <= count; i += 4u) {
        const __m128 a = _mm_loadu_ps(lhs + i);
        const __m128 b = _mm_loadu_ps(rhs + i);
        _mm_storeu_ps(destination + i, _mm_div_ps(a, b));
    }
    for (; i < count; ++i) {
        destination[i] = lhs[i] / rhs[i];
    }
#else
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = lhs[i] / rhs[i];
    }
#endif
}

// Computes 5-point Laplacian stencil on 2D grid.
// Uses boundary handler for edge cells; optionally uses SSE2 for interior.
void laplacian5Point2D(
    const float* input,
    float* output,
    const std::uint32_t width,
    const std::uint32_t height,
    const BoundaryCondition boundaryCondition) noexcept {
    if (input == nullptr || output == nullptr || width == 0u || height == 0u) {
        return;
    }

    if (width == 1u || height == 1u || boundaryCondition == BoundaryCondition::Periodic) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float center = sampleBoundary(input, width, height, x, y, boundaryCondition);
                const float north = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) - 1, boundaryCondition);
                const float south = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) + 1, boundaryCondition);
                const float east = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, static_cast<std::int64_t>(y), boundaryCondition);
                const float west = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, static_cast<std::int64_t>(y), boundaryCondition);
                output[static_cast<std::size_t>(y) * width + x] = north + south + east + west - 4.0f * center;
            }
        }
        return;
    }

    // Processes top and bottom boundary rows with boundary sampling.
    for (std::uint32_t x = 0; x < width; ++x) {
        const std::size_t top = static_cast<std::size_t>(x);
        const std::size_t bottom = static_cast<std::size_t>(height - 1u) * width + x;
        const float centerTop = input[top];
        const float centerBottom = input[bottom];
        output[top] = sampleBoundary(input, width, height, x, -1, boundaryCondition) +
            sampleBoundary(input, width, height, static_cast<std::int64_t>(x), 1, boundaryCondition) +
            sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, 0, boundaryCondition) +
            sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, 0, boundaryCondition) -
            4.0f * centerTop;
        output[bottom] = sampleBoundary(input, width, height, x, static_cast<std::int64_t>(height), boundaryCondition) +
            sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(height) - 2, boundaryCondition) +
            sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, static_cast<std::int64_t>(height) - 1, boundaryCondition) +
            sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, static_cast<std::int64_t>(height) - 1, boundaryCondition) -
            4.0f * centerBottom;
    }

    // Processes interior rows using vectorized or scalar path.
    for (std::uint32_t y = 1; y + 1 < height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * width;
        output[row] = sampleBoundary(input, width, height, -1, static_cast<std::int64_t>(y), boundaryCondition) +
            sampleBoundary(input, width, height, 1, static_cast<std::int64_t>(y), boundaryCondition) +
            input[row - width] + input[row + width] - 4.0f * input[row];

#if WS_HAS_SSE2
        std::uint32_t x = 1;
        for (; x + 5u < width; x += 4u) {
            const std::size_t idx = row + x;
            __m128 north = _mm_loadu_ps(input + idx - width);
            __m128 south = _mm_loadu_ps(input + idx + width);
            __m128 center = _mm_loadu_ps(input + idx);
            __m128 east = _mm_loadu_ps(input + idx + 1);
            __m128 west = _mm_loadu_ps(input + idx - 1);
            __m128 four = _mm_set1_ps(4.0f);
            __m128 result = _mm_add_ps(north, south);
            result = _mm_add_ps(result, east);
            result = _mm_add_ps(result, west);
            result = _mm_sub_ps(result, _mm_mul_ps(four, center));
            _mm_storeu_ps(output + idx, result);
        }
        for (; x + 1u < width; ++x) {
            const std::size_t idx = row + x;
            output[idx] = input[idx - width] + input[idx + width] + input[idx - 1] + input[idx + 1] - 4.0f * input[idx];
        }
#else
        laplacianInteriorScalar(input, output, width, y);
#endif

        output[row + width - 1u] = sampleBoundary(input, width, height, static_cast<std::int64_t>(width), static_cast<std::int64_t>(y), boundaryCondition) +
            input[row - width + width - 1u] + input[row + width + width - 1u] + input[row + width - 2u] - 4.0f * input[row + width - 1u];
    }
}

// Computes central difference gradient on 2D grid.
// Outputs x and y gradient components; uses SSE2 when available.
void gradientCentralDifference2D(
    const float* input,
    float* outX,
    float* outY,
    const std::uint32_t width,
    const std::uint32_t height,
    const BoundaryCondition boundaryCondition) noexcept {
    if (input == nullptr || outX == nullptr || outY == nullptr || width == 0u || height == 0u) {
        return;
    }

    if (width == 1u || height == 1u || boundaryCondition == BoundaryCondition::Periodic) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float east = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, static_cast<std::int64_t>(y), boundaryCondition);
                const float west = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, static_cast<std::int64_t>(y), boundaryCondition);
                const float north = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) - 1, boundaryCondition);
                const float south = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) + 1, boundaryCondition);
                const std::size_t idx = static_cast<std::size_t>(y) * width + x;
                outX[idx] = 0.5f * (east - west);
                outY[idx] = 0.5f * (south - north);
            }
        }
        return;
    }

    // Processes top and bottom boundary rows with boundary sampling.
    for (std::uint32_t x = 0; x < width; ++x) {
        const std::size_t top = static_cast<std::size_t>(x);
        const std::size_t bottom = static_cast<std::size_t>(height - 1u) * width + x;
        const float eastTop = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, 0, boundaryCondition);
        const float westTop = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, 0, boundaryCondition);
        const float southTop = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), 1, boundaryCondition);
        const float northTop = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), -1, boundaryCondition);
        const float eastBottom = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, static_cast<std::int64_t>(height - 1u), boundaryCondition);
        const float westBottom = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, static_cast<std::int64_t>(height - 1u), boundaryCondition);
        const float southBottom = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(height), boundaryCondition);
        const float northBottom = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(height - 2u), boundaryCondition);
        outX[top] = 0.5f * (eastTop - westTop);
        outY[top] = 0.5f * (southTop - northTop);
        outX[bottom] = 0.5f * (eastBottom - westBottom);
        outY[bottom] = 0.5f * (southBottom - northBottom);
    }

    // Processes interior rows using vectorized or scalar path.
    for (std::uint32_t y = 1; y + 1 < height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * width;
        outX[row] = 0.5f * (sampleBoundary(input, width, height, 1, static_cast<std::int64_t>(y), boundaryCondition) -
                            sampleBoundary(input, width, height, -1, static_cast<std::int64_t>(y), boundaryCondition));
        outY[row] = 0.5f * (input[row + width] - input[row - width]);

#if WS_HAS_SSE2
        std::uint32_t x = 1;
        for (; x + 5u < width; x += 4u) {
            const std::size_t idx = row + x;
            const __m128 center = _mm_loadu_ps(input + idx);
            const __m128 east = _mm_loadu_ps(input + idx + 1);
            const __m128 west = _mm_loadu_ps(input + idx - 1);
            const __m128 north = _mm_loadu_ps(input + idx - width);
            const __m128 south = _mm_loadu_ps(input + idx + width);
            const __m128 half = _mm_set1_ps(0.5f);
            _mm_storeu_ps(outX + idx, _mm_mul_ps(half, _mm_sub_ps(east, west)));
            _mm_storeu_ps(outY + idx, _mm_mul_ps(half, _mm_sub_ps(south, north)));
        }
        for (; x + 1u < width; ++x) {
            const std::size_t idx = row + x;
            outX[idx] = 0.5f * (input[idx + 1] - input[idx - 1]);
            outY[idx] = 0.5f * (input[idx + width] - input[idx - width]);
        }
#else
        gradientInteriorScalar(input, outX, outY, width, y);
#endif

        outX[row + width - 1u] = 0.5f * (sampleBoundary(input, width, height, static_cast<std::int64_t>(width), static_cast<std::int64_t>(y), boundaryCondition) -
                                         input[row + width - 2u]);
        outY[row + width - 1u] = 0.5f * (sampleBoundary(input, width, height, static_cast<std::int64_t>(width - 1u), static_cast<std::int64_t>(y) + 1, boundaryCondition) -
                                         sampleBoundary(input, width, height, static_cast<std::int64_t>(width - 1u), static_cast<std::int64_t>(y) - 1, boundaryCondition));
    }
}

} // namespace ws::vectorized
