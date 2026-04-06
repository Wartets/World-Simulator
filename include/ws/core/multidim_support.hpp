#pragma once

#include "ws/core/spatial_scheme.hpp"

#include <cstdint>
#include <vector>
#include <numeric>
#include <stdexcept>

namespace ws {

// Generalized multi-dimensional grid support (scaffolding for Phase 10.3)
// Supports 1D to 5D grids with configurable boundary conditions per dimension

// Dimensions for multi-dimensional grids.
struct GridDimensions {
    std::vector<std::uint32_t> sizes;  // [X, Y, Z, W, V] for up to 5D
    std::vector<BoundaryCondition> boundaryConditions;

    explicit GridDimensions() = default;

    // Create 2D grid with Neumann boundary conditions
    explicit GridDimensions(std::uint32_t width, std::uint32_t height)
        : sizes({width, height}), boundaryConditions(2, BoundaryCondition::Neumann) {}

    // Create 3D grid with Neumann boundary conditions
    explicit GridDimensions(std::uint32_t width, std::uint32_t height, std::uint32_t depth)
        : sizes({width, height, depth}), boundaryConditions(3, BoundaryCondition::Neumann) {}

    // Total number of cells
    [[nodiscard]] std::uint64_t cellCount() const noexcept {
        if (sizes.empty()) {
            return 0;
        }
        std::uint64_t count = 1;
        for (const auto size : sizes) {
            count *= static_cast<std::uint64_t>(size);
        }
        return count;
    }

    // Number of dimensions
    [[nodiscard]] std::size_t dimensionCount() const noexcept { return sizes.size(); }

    // Validate grid dimensions
    void validate() const {
        if (sizes.empty()) {
            throw std::invalid_argument("GridDimensions requires at least one dimension");
        }
        if (sizes.size() > 5) {
            throw std::invalid_argument("GridDimensions supports up to 5 dimensions");
        }
        if (boundaryConditions.size() != sizes.size()) {
            throw std::invalid_argument("Each dimension must have a boundary condition");
        }
        for (const auto size : sizes) {
            if (size == 0) {
                throw std::invalid_argument("GridDimensions size must be > 0");
            }
        }
    }
};

// Compute strides for multi-dimensional indexing (row-major order)
struct GridStrides {
    std::vector<std::uint64_t> strides;  // Stride for each dimension

    explicit GridStrides(const GridDimensions& grid) {
        if (grid.sizes.empty()) {
            return;
        }

        strides.resize(grid.sizes.size());
        strides.back() = 1;

        for (int i = static_cast<int>(grid.sizes.size()) - 2; i >= 0; --i) {
            strides[i] = strides[i + 1] * static_cast<std::uint64_t>(grid.sizes[i + 1]);
        }
    }

    // Convert multi-dimensional coordinates to linear index
    [[nodiscard]] std::uint64_t toLinearIndex(const std::vector<std::uint32_t>& coords) const noexcept {
        if (coords.size() != strides.size()) {
            return 0;
        }

        std::uint64_t index = 0;
        for (std::size_t i = 0; i < coords.size(); ++i) {
            index += static_cast<std::uint64_t>(coords[i]) * strides[i];
        }
        return index;
    }

    // Convert linear index to multi-dimensional coordinates
    [[nodiscard]] std::vector<std::uint32_t> toCoordinates(
        std::uint64_t index,
        const GridDimensions& grid) const noexcept {
        std::vector<std::uint32_t> coords(grid.sizes.size());

        for (int i = 0; i < static_cast<int>(grid.sizes.size()); ++i) {
            coords[i] = static_cast<std::uint32_t>((index / strides[i]) % grid.sizes[i]);
        }

        return coords;
    }
};

// Multi-dimensional boundary resolver
class MultiDimBoundaryResolver {
public:
    explicit MultiDimBoundaryResolver(const GridDimensions& grid);

    // Resolve coordinates to valid in-bounds coordinates applying boundary conditions
    [[nodiscard]] std::vector<std::uint32_t> resolveCoordinates(
        const std::vector<std::int64_t>& coords) const;

    // Apply boundary condition for a single dimension
    [[nodiscard]] std::uint32_t resolveDimension(
        std::int64_t coord,
        std::size_t dimension) const;

private:
    const GridDimensions& grid_;
};

} // namespace ws
